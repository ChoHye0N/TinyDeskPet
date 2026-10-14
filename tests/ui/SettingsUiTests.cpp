#include "ui/SettingsUi.h"

#include <gtest/gtest.h>

#include <vector>

using deskpet::core::SizeI;
using deskpet::core::Vec2;
using deskpet::ui::SettingsAction;
using deskpet::ui::SettingsState;
using deskpet::ui::SettingsTab;
using deskpet::ui::SettingsUi;

namespace {

constexpr SizeI kDisplay{1600, 900};
constexpr deskpet::core::RectI kCharacter{1200, 500, 1400, 900};  // 캐릭터가 그려지는 영역
constexpr float kTitleHeight = 44.0f;  // 96 DPI 기준 제목 줄 높이 (SettingsUi.cpp)

// 한 프레임 진행하고 그 프레임의 동작을 돌려줌
std::vector<SettingsAction> step(SettingsUi& ui, const SettingsState& state = {}) {
    std::vector<SettingsAction> actions;
    (void)ui.frame(1.0f / 60.0f, kDisplay, state, actions);
    return actions;
}

// 마우스로 한 점을 눌렀다 뗌 (ImGui는 프레임 단위로 입력을 처리하므로 프레임마다 나눔)
std::vector<SettingsAction> click(SettingsUi& ui, Vec2 point, const SettingsState& state = {}) {
    ui.setMousePosition(point);
    (void)step(ui, state);
    ui.setMouseButton(true);
    (void)step(ui, state);
    ui.setMouseButton(false);
    return step(ui, state);
}

}  // namespace

TEST(SettingsUi, Closed_DrawsNothingAndIgnoresMouse) {
    SettingsUi ui;
    std::vector<SettingsAction> actions;
    EXPECT_EQ(ui.frame(1.0f / 60.0f, kDisplay, {}, actions), nullptr);
    EXPECT_FALSE(ui.wantsMouse());
    EXPECT_TRUE(actions.empty());
}

TEST(SettingsUi, Open_IsPlacedBesideTheCharacterInsideTheScreen) {
    SettingsUi ui;
    ui.open(kCharacter);
    std::vector<SettingsAction> actions;
    EXPECT_NE(ui.frame(1.0f / 60.0f, kDisplay, {}, actions), nullptr);
    const auto rect = ui.windowRect();
    EXPECT_LE(rect.right, kCharacter.left);  // 캐릭터 왼쪽
    EXPECT_GE(rect.left, 0);
    EXPECT_GE(rect.top, 0);
    EXPECT_LE(rect.bottom, kDisplay.height);
}

TEST(SettingsUi, NearTheLeftEdge_IsPlacedRightOfTheCharacter) {
    SettingsUi ui;
    ui.open({20, 200, 220, 600});
    (void)step(ui);
    EXPECT_GE(ui.windowRect().left, 220);  // 캐릭터 오른쪽
}

TEST(SettingsUi, MouseOverWindow_IsCapturedOnlyThere) {
    SettingsUi ui;
    ui.open(kCharacter);
    (void)step(ui);
    const auto rect = ui.windowRect();
    ui.setMousePosition({static_cast<float>(rect.left + rect.right) / 2.0f,
                         static_cast<float>(rect.top + rect.bottom) / 2.0f});
    (void)step(ui);
    EXPECT_TRUE(ui.wantsMouse());
    ui.setMousePosition({1500.0f, 850.0f});  // 캐릭터 쪽
    (void)step(ui);
    EXPECT_FALSE(ui.wantsMouse());
}

TEST(SettingsUi, CloseButton_ClosesWindow) {
    SettingsUi ui;
    ui.open(kCharacter);
    (void)step(ui);
    const auto rect = ui.windowRect();
    (void)click(ui, {static_cast<float>(rect.right) - kTitleHeight / 2.0f,
                     static_cast<float>(rect.top) + kTitleHeight / 2.0f});
    EXPECT_FALSE(ui.isOpen());
    // 닫기 버튼 위에서 닫혔어도 마우스를 놓아줌 (남으면 화면 전체가 클릭을 받아 캐릭터를 못 누름)
    EXPECT_FALSE(ui.wantsMouse());
    std::vector<SettingsAction> actions;
    EXPECT_EQ(ui.frame(1.0f / 60.0f, kDisplay, {}, actions), nullptr);
}

TEST(SettingsUi, ClickingTab_SelectsIt) {
    SettingsUi ui;
    ui.open(kCharacter);
    (void)step(ui);
    const auto rect = ui.windowRect();
    // 세 번째 탭(화면): 제목 아래 안쪽 여백 10, 탭 높이 34, 간격 4
    const float y =
        static_cast<float>(rect.top) + kTitleHeight + 10.0f + 2.0f * (34.0f + 4.0f) + 17.0f;
    (void)click(ui, {static_cast<float>(rect.left) + 60.0f, y});
    EXPECT_EQ(ui.tab(), SettingsTab::Display);
}

TEST(SettingsUi, DraggingTitle_MovesWindow) {
    SettingsUi ui;
    ui.open(kCharacter);
    (void)step(ui);
    const auto before = ui.windowRect();
    const Vec2 grab{static_cast<float>(before.left) + 150.0f,
                    static_cast<float>(before.top) + kTitleHeight / 2.0f};
    ui.setMousePosition(grab);
    (void)step(ui);
    ui.setMouseButton(true);
    (void)step(ui);
    ui.setMousePosition({grab.x - 100.0f, grab.y + 50.0f});
    (void)step(ui);
    ui.setMouseButton(false);
    (void)step(ui);
    (void)step(ui);
    EXPECT_EQ(ui.windowRect().left, before.left - 100);
    EXPECT_EQ(ui.windowRect().top, before.top + 50);
}
