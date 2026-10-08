#include "core/Config.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using deskpet::core::AppConfig;
using deskpet::core::loadConfigFile;
using deskpet::core::parseConfig;
namespace logging = deskpet::core::logging;

namespace {

bool containsWarning(const std::vector<std::string>& warnings, std::string_view needle) {
    return std::any_of(warnings.begin(), warnings.end(), [&](const std::string& warning) {
        return warning.find(needle) != std::string::npos;
    });
}

}  // namespace

TEST(Config, EmptyText_UsesDefaultsWithoutWarnings) {
    const auto result = parseConfig("");
    const AppConfig defaults;
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.window.width, defaults.window.width);
    EXPECT_EQ(result.config.window.height, defaults.window.height);
    EXPECT_FLOAT_EQ(result.config.character.gravity, defaults.character.gravity);
}

TEST(Config, ValidValues_AreApplied) {
    const auto result = parseConfig(R"(
[window]
width = 256
height = 300
margin_right = 10
always_on_top = false

[character]
gravity = 1200.5
jump_speed = 500
drag_threshold = 8
max_fall_speed = 2000

[renderer]
vsync = false
debug_layer = true
msaa = 8

[log]
level = debug
to_file = no
)");
    EXPECT_TRUE(result.warnings.empty());
    const AppConfig& c = result.config;
    EXPECT_EQ(c.window.width, 256);
    EXPECT_EQ(c.window.height, 300);
    EXPECT_EQ(c.window.marginRight, 10);
    EXPECT_FALSE(c.window.alwaysOnTop);
    EXPECT_FLOAT_EQ(c.character.gravity, 1200.5f);
    EXPECT_FLOAT_EQ(c.character.jumpSpeed, 500.0f);
    EXPECT_FLOAT_EQ(c.character.dragThreshold, 8.0f);
    EXPECT_FLOAT_EQ(c.character.maxFallSpeed, 2000.0f);
    EXPECT_FALSE(c.renderer.vsync);
    EXPECT_TRUE(c.renderer.debugLayer);
    EXPECT_EQ(c.renderer.msaa, 8);
    EXPECT_EQ(c.log.level, logging::Level::Debug);
    EXPECT_FALSE(c.log.toFile);
}

TEST(Config, CommentsWhitespaceAndCase_AreHandled) {
    const auto result = parseConfig(
        "# 주석\r\n"
        "; 주석\r\n"
        "  [ WINDOW ]  \r\n"
        "  Width   =   320   ; 줄 끝 주석\r\n"
        "HEIGHT=240 # 다른 형식의 주석\r\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.window.width, 320);
    EXPECT_EQ(result.config.window.height, 240);
}

TEST(Config, Utf8Bom_IsIgnored) {
    const auto result = parseConfig("\xEF\xBB\xBF[window]\nwidth = 128\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.window.width, 128);
}

TEST(Config, UnknownKey_WarnsAndIsIgnored) {
    const auto result = parseConfig("[window]\ncolor = red\n");
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_TRUE(containsWarning(result.warnings, "window.color"));
}

TEST(Config, OutOfRangeValue_WarnsAndKeepsDefault) {
    const auto result = parseConfig("[window]\nwidth = 5\n");
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_TRUE(containsWarning(result.warnings, "2행"));
    EXPECT_EQ(result.config.window.width, AppConfig{}.window.width);
}

TEST(Config, NonNumericValue_WarnsAndKeepsDefault) {
    const auto result = parseConfig("[character]\ngravity = fast\n");
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_FLOAT_EQ(result.config.character.gravity, AppConfig{}.character.gravity);
}

TEST(Config, TrailingGarbageInNumber_IsRejected) {
    const auto result = parseConfig("[window]\nwidth = 200px\n");
    EXPECT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(result.config.window.width, AppConfig{}.window.width);
}

TEST(Config, MalformedLines_WarnWithLineNumbers) {
    const auto result = parseConfig("[window\nthis line has no equals\n");
    ASSERT_EQ(result.warnings.size(), 2U);
    EXPECT_TRUE(containsWarning(result.warnings, "1행"));
    EXPECT_TRUE(containsWarning(result.warnings, "2행"));
}

TEST(Config, InvalidBoolAndLevel_Warn) {
    const auto result = parseConfig("[window]\nalways_on_top = maybe\n[log]\nlevel = loud\n");
    EXPECT_EQ(result.warnings.size(), 2U);
    EXPECT_TRUE(result.config.window.alwaysOnTop);
    EXPECT_EQ(result.config.log.level, logging::Level::Info);
}

TEST(Config, DuplicateKey_LastValueWins) {
    const auto result = parseConfig("[window]\nwidth = 100\nwidth = 150\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.window.width, 150);
}

TEST(Config, MissingFile_ReturnsDefaultsWithWarning) {
    const auto result = loadConfigFile("this/file/does/not/exist.ini");
    ASSERT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(result.config.window.width, AppConfig{}.window.width);
}

TEST(Config, ModelPath_DefaultsToEmptyAndIsReadAsText) {
    EXPECT_TRUE(parseConfig("").config.model.path.empty());

    const auto result = parseConfig("[model]\npath = models/zmd_EM.vrm   ; 주석\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.model.path, "models/zmd_EM.vrm");
}

TEST(Config, MotionClips_DefaultToEmptyAndAreReadPerMotion) {
    const auto& defaults = parseConfig("").config.animation;
    EXPECT_TRUE(defaults.idleClip.empty());
    EXPECT_TRUE(defaults.walkClip.empty());

    const auto result = parseConfig(
        "[animation]\nidle_clip = motions/idle.vrma\nwalk_clip = motions/walk.vmd\n"
        "dragged_clip = motions/dangle.fbx\nairborne_clip = motions/fall.vrma\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.animation.idleClip, "motions/idle.vrma");
    EXPECT_EQ(result.config.animation.walkClip, "motions/walk.vmd");
    EXPECT_EQ(result.config.animation.draggedClip, "motions/dangle.fbx");
    EXPECT_EQ(result.config.animation.airborneClip, "motions/fall.vrma");
}

TEST(Config, LastPosition_IsOptionalAndReadWhenPresent) {
    EXPECT_FALSE(parseConfig("").config.state.lastPosition().has_value());

    const auto result = parseConfig("[state]\nlast_x = -120\nlast_y = 900\n");
    EXPECT_TRUE(result.warnings.empty());
    ASSERT_TRUE(result.config.state.lastPosition().has_value());
    EXPECT_EQ(*result.config.state.lastPosition(), (deskpet::core::PointI{-120, 900}));

    // 한쪽만 있으면 위치로 쓰지 않음
    EXPECT_FALSE(parseConfig("[state]\nlast_x = 5\n").config.state.lastPosition().has_value());
}

// ---------------------------------------------------------------------------
// 설정 쓰기 (마지막 위치 저장)
// ---------------------------------------------------------------------------

using deskpet::core::setIniValue;

TEST(SetIniValue, ReplacesExistingValueAndKeepsComment) {
    const std::string text = "[State]\nlast_x = 1   ; 주석\nlast_y = 2\n";
    EXPECT_EQ(setIniValue(text, "state", "last_x", "42"),
              "[State]\nlast_x = 42   ; 주석\nlast_y = 2\n");
}

TEST(SetIniValue, AddsKeyToEndOfExistingSection) {
    const std::string text = "[state]\nlast_x = 1\n\n[log]\nlevel = info\n";
    EXPECT_EQ(setIniValue(text, "state", "last_y", "7"),
              "[state]\nlast_x = 1\nlast_y = 7\n\n[log]\nlevel = info\n");
}

TEST(SetIniValue, AddsNewSectionAtEnd) {
    EXPECT_EQ(setIniValue("[log]\nlevel = info", "state", "last_x", "3"),
              "[log]\nlevel = info\n\n[state]\nlast_x = 3\n");
    EXPECT_EQ(setIniValue("", "state", "last_x", "3"), "[state]\nlast_x = 3\n");
}

TEST(SetIniValue, KeepsCrlfLineEndings) {
    EXPECT_EQ(setIniValue("[state]\r\nlast_x = 1\r\n", "state", "last_x", "9"),
              "[state]\r\nlast_x = 9\r\n");
}

TEST(SetIniValue, ResultParsesBack) {
    std::string text = "# 설명\n[window]\nwidth = 300\n";
    text = setIniValue(text, "state", "last_x", "10");
    text = setIniValue(text, "state", "last_y", "20");
    const auto result = parseConfig(text);
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_EQ(result.config.window.width, 300);
    EXPECT_EQ(*result.config.state.lastPosition(), (deskpet::core::PointI{10, 20}));
}

TEST(Config, AnimationIdleMotion_DefaultsOnAndCanBeDisabled) {
    EXPECT_TRUE(parseConfig("").config.animation.idleMotion);
    const auto result = parseConfig("[animation]\nidle_motion = false\n");
    EXPECT_TRUE(result.warnings.empty());
    EXPECT_FALSE(result.config.animation.idleMotion);
}

TEST(Config, Msaa_DefaultsToFourAndAcceptsOnlyPowersOfTwo) {
    EXPECT_EQ(AppConfig{}.renderer.msaa, 4);
    EXPECT_EQ(parseConfig("[renderer]\nmsaa = 1\n").config.renderer.msaa, 1);  // 1 = 끔

    for (const char* bad : {"3", "0", "16", "-4"}) {
        const auto result = parseConfig(std::string("[renderer]\nmsaa = ") + bad + "\n");
        EXPECT_EQ(result.warnings.size(), 1U) << bad;
        EXPECT_EQ(result.config.renderer.msaa, 4) << bad;
    }
}

TEST(Config, RendererOutline_AcceptsModelAllOff) {
    using deskpet::core::OutlineMode;
    EXPECT_EQ(AppConfig{}.renderer.outline, OutlineMode::Model);
    EXPECT_EQ(parseConfig("[renderer]\noutline = all\n").config.renderer.outline, OutlineMode::All);
    EXPECT_EQ(parseConfig("[renderer]\noutline = OFF\n").config.renderer.outline, OutlineMode::Off);

    const auto result = parseConfig("[renderer]\noutline = thick\n");
    EXPECT_EQ(result.warnings.size(), 1U);
    EXPECT_EQ(result.config.renderer.outline, OutlineMode::Model);
}

TEST(Config, StateScale_IsOptionalPercentWithinRange) {
    EXPECT_FALSE(parseConfig("").config.state.scale.has_value());
    EXPECT_EQ(parseConfig("[state]\nscale = 150\n").config.state.scale, 150);

    const auto result = parseConfig("[state]\nscale = 300\n");  // 범위 50 ~ 200 밖
    EXPECT_EQ(result.warnings.size(), 1U);
    EXPECT_FALSE(result.config.state.scale.has_value());
}
