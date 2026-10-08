#include "anim/AnimTestModels.h"
#include "app/Application.h"
#include "app/Fakes.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using deskpet::app::Application;
using deskpet::app::ExitCode;
using deskpet::app::MenuCommand;
using deskpet::character::State;
using deskpet::core::AppConfig;
using deskpet::core::PointI;
using deskpet::core::Vec2;
using deskpet::renderer::FrameResult;
using deskpet::testing::FakeRenderer;
using deskpet::testing::FakeRendererState;
using deskpet::testing::FakeWindow;
using deskpet::testing::FakeWindowState;
using deskpet::testing::makeFakeTime;

namespace core = deskpet::core;

namespace {

class ApplicationTest : public ::testing::Test {
protected:
    ApplicationTest() {
        config_.window.width = 200;
        config_.window.height = 200;
        config_.window.marginRight = 40;
        window_.workArea = {0, 0, 1920, 1040};
    }

    // 가짜 창·렌더러·시간을 주입한 Application을 만듭니다.
    std::unique_ptr<Application> makeApp() {
        return std::make_unique<Application>(config_, std::make_unique<FakeWindow>(window_),
                                             std::make_unique<FakeRenderer>(renderer_),
                                             makeFakeTime());
    }

    AppConfig config_;
    FakeWindowState window_;
    FakeRendererState renderer_;
};

// 작업 영역 1920x1040, 창 200x200, 오른쪽 여백 40일 때 시작 위치
constexpr PointI kStartTopLeft{1920 - 40 - 200, 1040 - 200};  // (1680, 840) 캐릭터 상자 좌상단
constexpr Vec2 kStartFeet{1920.0f - 40.0f - 100.0f, 1040.0f};  // (1780, 1040)

}  // namespace

namespace {

// 캐릭터 상자 (화면 좌표). 창은 이제 작업 영역 전체(오버레이)라 창 위치 대신 상자로 확인 (ADR-0011)
PointI boxTopLeft(const Application& app) {
    const core::RectI box = app.characterRect();
    return {box.left, box.top};
}

core::SizeI boxSize(const Application& app) {
    const core::RectI box = app.characterRect();
    return {box.width(), box.height()};
}

}  // namespace

TEST_F(ApplicationTest, WindowCreateFailure_ReturnsInitFailedWithoutRenderer) {
    window_.createResult = false;
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::InitializationFailed));
    EXPECT_FALSE(renderer_.initialized);
    EXPECT_EQ(renderer_.renderCount, 0U);
}

TEST_F(ApplicationTest, RendererInitFailure_ReturnsInitFailed) {
    renderer_.initializeResult = false;
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::InitializationFailed));
    EXPECT_FALSE(window_.shown);
}

TEST_F(ApplicationTest, Startup_PlacesCharacterAtBottomRightOnGround) {
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::Ok));
    EXPECT_TRUE(window_.shown);
    EXPECT_EQ(boxTopLeft(*app), kStartTopLeft);
    // 창은 작업 영역 전체를 덮는 오버레이
    EXPECT_EQ(window_.position, (PointI{0, 0}));
    EXPECT_EQ(window_.size, (core::SizeI{1920, 1040}));
    EXPECT_EQ(app->character().state(), State::Idle);
    EXPECT_EQ(app->character().position(), kStartFeet);
    EXPECT_TRUE(renderer_.shutdownCalled);
}

TEST_F(ApplicationTest, Startup_SetsHitRegionInsideWindow) {
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    (void)app->run();

    const core::RectI hit = app->visibleRect();
    EXPECT_GT(hit.width(), 0);
    EXPECT_GT(hit.height(), 0);
    EXPECT_GE(hit.left, 0);
    EXPECT_GE(hit.top, 0);
    EXPECT_LE(hit.right, 200);
    EXPECT_LE(hit.bottom, 200);
}

TEST_F(ApplicationTest, QuitEvent_ExitsBeforeRendering) {
    window_.frames.push_back({core::QuitRequestedEvent{}});
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::Ok));
    EXPECT_EQ(renderer_.renderCount, 0U);
}

TEST_F(ApplicationTest, RendersSceneWithPlaceholderInsideViewport) {
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    ASSERT_EQ(renderer_.renderCount, 2U);
    const auto& scene = renderer_.lastScene;
    EXPECT_EQ(scene.viewport, (core::SizeI{1920, 1040}));  // 오버레이 전체
    // 슬라임은 캐릭터 상자(좌상단 1680, 840, 200×200) 안, 오버레이 좌표로
    EXPECT_FLOAT_EQ(scene.placeholder.center.x, 1680.0f + 100.0f);
    EXPECT_GT(scene.placeholder.radiusX, 0.0f);
    EXPECT_LT(scene.placeholder.center.y + scene.placeholder.radiusY, 1040.0f);
    EXPECT_GT(scene.placeholder.center.y - scene.placeholder.radiusY, 840.0f);
}

TEST_F(ApplicationTest, Drag_WindowFollowsPointer) {
    // 발보다 40px 위를 잡고 (-80, -100) 만큼 끌기
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 900.0f}}});
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Dragged);
    // 발 = (1700, 940) → 상자 좌상단 = (1600, 740). 창(오버레이)은 그대로
    EXPECT_EQ(boxTopLeft(*app), (PointI{1600, 740}));
    EXPECT_EQ(window_.position, (PointI{0, 0}));
}

TEST_F(ApplicationTest, DragAndRelease_FallsBackToGround) {
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 500.0f}}});
    for (int i = 0; i < 12; ++i) {
        window_.frames.push_back({});  // 0.2초 멈춤 → 던지기 없이 떨어뜨림 (FR-16)
    }
    window_.frames.push_back({core::PointerUpEvent{{1700.0f, 500.0f}, core::MouseButton::Left}});
    window_.pollsBeforeQuit = 180;  // 3초면 충분히 착지
    std::vector<PointI> history;
    const Application* running = nullptr;
    window_.onPoll = [&](int, FakeWindowState&) {
        if (running != nullptr) {
            history.push_back(boxTopLeft(*running));
        }
    };
    auto app = makeApp();
    running = app.get();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Idle);
    EXPECT_FLOAT_EQ(app->character().position().y, 1040.0f);
    EXPECT_EQ(boxTopLeft(*app), (PointI{1600, 840}));

    // 낙하하는 동안 상자가 아래로만 움직였는지 확인. 놓은 지점: 발 (1700, 540) → 상자 (1600, 340)
    const auto releaseIt = std::find(history.begin(), history.end(), PointI{1600, 340});
    ASSERT_NE(releaseIt, history.end());
    EXPECT_TRUE(std::is_sorted(releaseIt, history.end(),
                               [](const PointI& a, const PointI& b) { return a.y < b.y; }));
}

TEST_F(ApplicationTest, DoubleClick_Jumps) {
    window_.frames.push_back({core::DoubleClickEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Airborne);
    EXPECT_LT(app->character().position().y, 1040.0f);
}

TEST_F(ApplicationTest, RightClick_ShowsMenuAndQuitSelectionExits) {
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::Quit);
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::Ok));
    EXPECT_EQ(window_.menuShownCount, 1);
    EXPECT_EQ(renderer_.renderCount, 0U);

    const auto& menu = window_.lastMenu;
    const bool hasQuit = std::any_of(menu.begin(), menu.end(), [](const auto& item) {
        return item.id == static_cast<int>(MenuCommand::Quit) && item.enabled;
    });
    EXPECT_TRUE(hasQuit);
}

// ---------------------------------------------------------------------------
// 캐릭터 크기 조절 (FR-06)
// ---------------------------------------------------------------------------

namespace {

const deskpet::platform::MenuItem* findMenuItem(
    const std::vector<deskpet::platform::MenuItem>& menu, MenuCommand command) {
    const auto it = std::find_if(menu.begin(), menu.end(), [command](const auto& item) {
        return item.id == static_cast<int>(command);
    });
    return it != menu.end() ? &*it : nullptr;
}

}  // namespace

TEST_F(ApplicationTest, ContextMenu_HasScaleItemsButNoJump) {
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    const auto& menu = window_.lastMenu;
    EXPECT_TRUE(std::none_of(menu.begin(), menu.end(),
                             [](const auto& item) { return item.label == "점프"; }));
    const auto* size = findMenuItem(menu, MenuCommand::ScaleInfo);
    ASSERT_NE(size, nullptr);
    EXPECT_EQ(size->label, "크기 100%");
    EXPECT_FALSE(size->enabled);  // 정보 표시용
    ASSERT_NE(findMenuItem(menu, MenuCommand::ScaleUp), nullptr);
    ASSERT_NE(findMenuItem(menu, MenuCommand::ScaleDown), nullptr);
    EXPECT_TRUE(findMenuItem(menu, MenuCommand::ScaleUp)->enabled);
    EXPECT_TRUE(findMenuItem(menu, MenuCommand::ScaleDown)->enabled);
}

TEST_F(ApplicationTest, ScaleUp_EnlargesWindowAndKeepsFeetOnGround) {
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::ScaleUp);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->scalePercent(), 110);
    EXPECT_EQ(boxSize(*app), (core::SizeI{220, 220}));
    EXPECT_EQ(window_.size, (core::SizeI{1920, 1040}));  // 창(오버레이)은 그대로
    EXPECT_EQ(boxTopLeft(*app).y + 220, 1040);           // 발은 그대로 바닥에
    EXPECT_FLOAT_EQ(app->character().position().x, kStartFeet.x);
}

TEST_F(ApplicationTest, ScaleDown_ShrinksWindow) {
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::ScaleDown);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->scalePercent(), 90);
    EXPECT_EQ(boxSize(*app), (core::SizeI{180, 180}));
}

TEST_F(ApplicationTest, ScaleAtMaximum_DisablesScaleUpAndIgnoresIt) {
    config_.state.scale = 200;
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::ScaleUp);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FALSE(findMenuItem(window_.lastMenu, MenuCommand::ScaleUp)->enabled);
    EXPECT_TRUE(findMenuItem(window_.lastMenu, MenuCommand::ScaleDown)->enabled);
    EXPECT_EQ(app->scalePercent(), 200);
    EXPECT_EQ(boxSize(*app), (core::SizeI{400, 400}));
}

TEST_F(ApplicationTest, ScaleAtMinimum_DisablesScaleDownAndIgnoresIt) {
    config_.state.scale = 50;
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::ScaleDown);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FALSE(findMenuItem(window_.lastMenu, MenuCommand::ScaleDown)->enabled);
    EXPECT_EQ(app->scalePercent(), 50);
    EXPECT_EQ(boxSize(*app), (core::SizeI{100, 100}));
}

TEST_F(ApplicationTest, SavedScale_IsRestoredSnappedToStepAndCombinedWithDpi) {
    config_.state.scale = 137;  // 10% 단위로 맞춤 → 140%
    window_.dpiScale = 1.5f;
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->scalePercent(), 140);
    EXPECT_EQ(boxSize(*app), (core::SizeI{420, 420}));            // 200 × 1.5 × 1.4
    EXPECT_EQ(renderer_.initialSize, (core::SizeI{1920, 1040}));  // 렌더러는 오버레이 크기
    EXPECT_EQ(boxTopLeft(*app).y + 420, 1040);
}

TEST_F(ApplicationTest, RendererFatal_ReturnsRendererFailed) {
    renderer_.results = {FrameResult::Ok, FrameResult::Fatal};
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::RendererFailed));
    EXPECT_EQ(renderer_.renderCount, 2U);
}

TEST_F(ApplicationTest, DeviceRecovered_KeepsRunning) {
    renderer_.results = {FrameResult::DeviceRecovered, FrameResult::Ok};
    window_.pollsBeforeQuit = 4;
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::Ok));
    EXPECT_EQ(renderer_.renderCount, 3U);
}

TEST_F(ApplicationTest, WorkAreaChanged_MovesCharacterToNewGround) {
    // 두 번째 poll 직전에 작업 표시줄이 올라온 상황을 흉내 냄
    window_.onPoll = [](int pollIndex, FakeWindowState& state) {
        if (pollIndex == 1) {
            state.workArea.bottom = 900;
        }
    };
    window_.frames.push_back({});
    window_.frames.push_back({core::WorkAreaChangedEvent{}});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Idle);
    EXPECT_FLOAT_EQ(app->character().position().y, 900.0f);
    EXPECT_EQ(boxTopLeft(*app).y, 900 - 200);
    EXPECT_EQ(window_.size, (core::SizeI{1920, 900}));  // 오버레이도 새 작업 영역으로
}

TEST_F(ApplicationTest, ConfigOptions_ArePassedToRenderer) {
    config_.renderer.vsync = false;
    config_.renderer.debugLayer = true;
    config_.renderer.msaa = 2;
    config_.renderer.outline = core::OutlineMode::All;
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FALSE(renderer_.options.vsync);
    EXPECT_TRUE(renderer_.options.debugLayer);
    EXPECT_EQ(renderer_.options.msaaSamples, 2);
    EXPECT_EQ(renderer_.options.outline, core::OutlineMode::All);
}

// ---------------------------------------------------------------------------
// VRM 모델 표시 (ADR-0008)
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<const deskpet::model::Model> makeHumanoidModel() {
    auto model = std::make_shared<deskpet::model::Model>();
    model->vertices.resize(3);
    model->indices = {0, 1, 2};
    model->bounds = {{-0.75f, 0.0f, -0.15f}, {0.75f, 1.6f, 0.15f}};
    return model;
}

}  // namespace

TEST_F(ApplicationTest, WithoutModel_RendersPlaceholderOnly) {
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    EXPECT_TRUE(renderer_.lastScene.placeholder.visible);
    EXPECT_EQ(renderer_.lastScene.character.model, nullptr);
}

TEST_F(ApplicationTest, WithModel_RendersModelInsteadOfPlaceholder) {
    window_.pollsBeforeQuit = 2;
    const auto model = makeHumanoidModel();
    auto app = makeApp();
    app->setModel(model);
    (void)app->run();

    const auto& scene = renderer_.lastScene;
    EXPECT_FALSE(scene.placeholder.visible);
    EXPECT_EQ(scene.character.model, model.get());

    // 발 중앙(0,0,0)이 캐릭터 상자 아래 가운데로 투영됨 (오버레이 픽셀)
    const auto clip = deskpet::core::transform({0.0f, 0.0f, 0.0f}, scene.character.viewProjection);
    const float px = (clip.x / clip.w + 1.0f) * 0.5f * 1920.0f;
    const float py = (1.0f - clip.y / clip.w) * 0.5f * 1040.0f;
    const core::RectI box = app->characterRect();
    EXPECT_NEAR(px, static_cast<float>(box.left + box.right) * 0.5f, 0.5f);
    EXPECT_NEAR(py, static_cast<float>(box.bottom), 0.5f);

    // 서 있을 때(돌지 않음) 카메라는 모델 정면 +Z 쪽 (림·MatCap 계산용)
    EXPECT_NEAR(scene.character.viewDirection.x, 0.0f, 1e-6f);
    EXPECT_NEAR(scene.character.viewDirection.z, 1.0f, 1e-6f);
}

TEST_F(ApplicationTest, WithModel_HitRegionCoversProjectedModel) {
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    app->setModel(makeHumanoidModel());
    (void)app->run();

    // T포즈(폭 1.5 > 키 1.6 × 창 비율 1)라 거의 창 전체 폭을 차지
    const core::RectI hit = app->visibleRect();
    EXPECT_GE(hit.left, 0);
    EXPECT_LE(hit.right, 200);
    EXPECT_GT(hit.width(), 180);
    EXPECT_GT(hit.height(), 150);
    EXPECT_LE(hit.bottom, 200);
}

// ---------------------------------------------------------------------------
// 트레이 아이콘 (FR-18)
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, Startup_ShowsTrayIcon) {
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    (void)app->run();
    EXPECT_EQ(window_.trayTooltip, "DeskPet");
}

TEST_F(ApplicationTest, TrayMenu_SharesContextMenuItems) {
    window_.frames.push_back({core::TrayMenuRequestedEvent{{1900, 1060}}});
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    ASSERT_EQ(window_.menuShownCount, 1);
    const auto& menu = window_.lastMenu;
    EXPECT_TRUE(std::any_of(menu.begin(), menu.end(), [](const auto& item) {
        return item.id == static_cast<int>(MenuCommand::ToggleVisible);
    }));
    EXPECT_TRUE(std::any_of(menu.begin(), menu.end(), [](const auto& item) {
        return item.id == static_cast<int>(MenuCommand::Quit);
    }));
}

TEST_F(ApplicationTest, ToggleVisible_HidesAndStopsRenderingThenShowsAgain) {
    window_.menuChoice = static_cast<int>(MenuCommand::ToggleVisible);
    window_.frames.push_back({core::TrayMenuRequestedEvent{{1900, 1060}}});  // 숨기기
    for (int i = 0; i < 5; ++i) {
        window_.frames.push_back({});
    }
    std::size_t rendersWhileHidden = 0;
    window_.onPoll = [&](int pollIndex, FakeWindowState& state) {
        if (pollIndex == 5) {
            EXPECT_FALSE(state.visible);
            rendersWhileHidden = renderer_.renderCount;
            state.frames.push_front({core::TrayMenuRequestedEvent{{1900, 1060}}});  // 보이기
        }
    };
    window_.pollsBeforeQuit = 10;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(rendersWhileHidden, 0U);  // 첫 프레임에 바로 숨김 → 렌더링 없음
    EXPECT_GE(window_.waitCount, 4);
    EXPECT_TRUE(window_.visible);
    EXPECT_GT(renderer_.renderCount, 0U);
}

// ---------------------------------------------------------------------------
// 고 DPI (DEBT-01)
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, StartupDpiScale_ScalesWindowAndRenderer) {
    window_.dpiScale = 1.5f;
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(boxSize(*app), (core::SizeI{300, 300}));
    EXPECT_EQ(renderer_.initialSize, (core::SizeI{1920, 1040}));
    EXPECT_EQ(renderer_.lastScene.viewport, (core::SizeI{1920, 1040}));
    // 발은 그대로 바닥에, 창 아래쪽이 바닥에 맞음
    EXPECT_EQ(boxTopLeft(*app).y + 300, 1040);
}

TEST_F(ApplicationTest, DpiChanged_ResizesWindowAndRendererKeepingFeet) {
    window_.frames.push_back({core::DpiChangedEvent{2.0f}});
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(boxSize(*app), (core::SizeI{400, 400}));
    EXPECT_EQ(renderer_.lastScene.viewport, (core::SizeI{1920, 1040}));
    // 발 높이는 그대로. 커진 슬라임이 오른쪽 화면 끝(1920)을 넘지 않을 만큼만 안쪽으로 밀림
    // (FR-17). 창의 투명한 여백은 화면 밖으로 나가도 됨
    EXPECT_EQ(boxTopLeft(*app).y, 1040 - 400);
    EXPECT_LE(boxTopLeft(*app).x + app->visibleRect().right, 1920);
    EXPECT_LT(boxTopLeft(*app).x, 1780 - 200);  // 발(1780)이 그대로면 넘치므로 안쪽으로 밀림
    EXPECT_LE(app->visibleRect().right, 400);
    EXPECT_GT(app->visibleRect().width(), 200);
}

// ---------------------------------------------------------------------------
// 화면 경계 (FR-17)
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, DraggedPastDesktopEdge_StaysInsideAfterRelease) {
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{2500.0f, 1000.0f}}});
    for (int i = 0; i < 12; ++i) {
        window_.frames.push_back({});
    }
    window_.frames.push_back({core::PointerUpEvent{{2500.0f, 1000.0f}, core::MouseButton::Left}});
    window_.pollsBeforeQuit = 60;
    auto app = makeApp();
    (void)app->run();

    // 막는 기준은 창이 아니라 그려지는 영역: 그 오른쪽 끝이 모니터 끝에 맞음.
    // 창의 투명한 여백은 화면 밖으로 나가도 됨 (창 반폭으로 막으면 끝에서 멈춰 보임)
    const int visibleRight = boxTopLeft(*app).x + app->visibleRect().right;
    EXPECT_LE(visibleRight, 1920);
    EXPECT_GE(visibleRight, 1918);
    EXPECT_GT(boxTopLeft(*app).x + 200, 1920);
}

TEST_F(ApplicationTest, ModelDraggedPastLeftEdge_VisibleEdgeMeetsDesktopEdge) {
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{-500.0f, 1000.0f}}});
    for (int i = 0; i < 12; ++i) {
        window_.frames.push_back({});
    }
    window_.frames.push_back({core::PointerUpEvent{{-500.0f, 1000.0f}, core::MouseButton::Left}});
    window_.pollsBeforeQuit = 60;
    auto app = makeApp();
    app->setModel(std::make_shared<deskpet::model::Model>(deskpet::test::makeSkeletonModel()));
    (void)app->run();

    const int visibleLeft = boxTopLeft(*app).x + app->visibleRect().left;
    EXPECT_GE(visibleLeft, 0);
    EXPECT_LE(visibleLeft, 2);
}

// ---------------------------------------------------------------------------
// 변화 없을 때 Present 생략 (DEBT-02)
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, SkippedFrame_WaitsForEventsInsteadOfSpinning) {
    renderer_.results = {FrameResult::Skipped, FrameResult::Skipped, FrameResult::Ok};
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();

    EXPECT_EQ(app->run(), static_cast<int>(ExitCode::Ok));
    EXPECT_EQ(window_.waitCount, 2);
}

// ---------------------------------------------------------------------------
// 마지막 위치 복원
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, SavedPosition_IsRestoredAndCharacterFallsToGround) {
    config_.state.lastX = 600;
    config_.state.lastY = 500;
    window_.pollsBeforeQuit = 120;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FLOAT_EQ(app->character().position().x, 600.0f);
    EXPECT_FLOAT_EQ(app->character().position().y, 1040.0f);
}

TEST_F(ApplicationTest, SavedPositionOutsideDesktop_IsIgnored) {
    config_.state.lastX = 5000;  // 모니터가 빠졌거나 해상도가 바뀐 경우
    config_.state.lastY = 500;
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(boxTopLeft(*app), kStartTopLeft);
}

// ---------------------------------------------------------------------------
// 애니메이션 (ADR-0010)
// ---------------------------------------------------------------------------

namespace {

std::shared_ptr<const deskpet::model::Model> makeSkinnedModel() {
    return std::make_shared<deskpet::model::Model>(deskpet::test::makeSkeletonModel());
}

constexpr std::size_t kSurprised = static_cast<std::size_t>(deskpet::model::Expression::Surprised);

}  // namespace

TEST_F(ApplicationTest, SkinnedModel_SceneHasSkinMatrixPerBone) {
    window_.pollsBeforeQuit = 3;
    const auto model = makeSkinnedModel();
    auto app = makeApp();
    app->setModel(model);
    (void)app->run();

    EXPECT_EQ(renderer_.lastScene.character.skinMatrices.size(), model->bones.size());
    EXPECT_FLOAT_EQ(renderer_.lastScene.character.expressionWeights[kSurprised], 0.0f);
}

TEST_F(ApplicationTest, DraggingModel_ShowsSurprisedExpression) {
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 700.0f}}});
    window_.pollsBeforeQuit = 20;  // 표정도 자세 전환(0.2초)에 맞춰 서서히 바뀜
    auto app = makeApp();
    app->setModel(makeSkinnedModel());
    (void)app->run();

    ASSERT_EQ(app->character().state(), State::Dragged);
    EXPECT_FLOAT_EQ(renderer_.lastScene.character.expressionWeights[kSurprised], 1.0f);
}

TEST_F(ApplicationTest, IdleMotion_AnimatesUnlessDisabled) {
    window_.pollsBeforeQuit = 10;
    auto app = makeApp();
    app->setModel(makeSkinnedModel());
    (void)app->run();
    EXPECT_NE(renderer_.previousScene, renderer_.lastScene);  // 숨쉬기 동작

    config_.animation.idleMotion = false;
    window_.pollCount = 0;
    auto still = makeApp();
    still->setModel(makeSkinnedModel());
    (void)still->run();
    // 대기 중 변화가 없어야 렌더러가 Present를 생략할 수 있음 (DEBT-02)
    EXPECT_EQ(renderer_.previousScene, renderer_.lastScene);
}

// ---------------------------------------------------------------------------
// 픽셀 단위 클릭 통과 (FR-04): 커서 아래 픽셀이 투명하면 클릭을 아래 창으로 넘김
// ---------------------------------------------------------------------------

namespace {

// 창 내부 (100~200, 100~200)만 불투명한 캐릭터
float squareCharacter(core::PointI p) {
    return p.x >= 100 && p.x < 200 && p.y >= 100 && p.y < 200 ? 1.0f : 0.0f;
}

// 매 poll마다 커서를 창 좌상단 + offset에 둠
std::function<void(int, deskpet::testing::FakeWindowState&)> cursorAt(core::PointI offset) {
    return [offset](int, deskpet::testing::FakeWindowState& w) {
        w.cursor = {w.position.x + offset.x, w.position.y + offset.y};
    };
}

}  // namespace

TEST_F(ApplicationTest, CursorOverOpaquePixel_MakesWindowClickable) {
    renderer_.alphaAt = squareCharacter;
    window_.onPoll = cursorAt({150, 150});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();
    EXPECT_FALSE(window_.clickThrough);
}

TEST_F(ApplicationTest, CursorOverTransparentPixel_PassesClicksThrough) {
    renderer_.alphaAt = squareCharacter;
    window_.onPoll = cursorAt({20, 20});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();
    EXPECT_TRUE(window_.clickThrough);
}

TEST_F(ApplicationTest, CursorOutsideWindow_PassesClicksThrough) {
    renderer_.alphaAt = [](core::PointI) { return 1.0f; };  // 창 안이 전부 불투명이어도
    window_.onPoll = cursorAt({-50, 150});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();
    EXPECT_TRUE(window_.clickThrough);
}

TEST_F(ApplicationTest, WhileDragging_StaysClickableOverTransparentPixels) {
    // 끄는 동안 커서가 투명한 곳(캐릭터 가장자리 바깥)으로 가도 계속 이벤트를 받아야 함
    renderer_.alphaAt = squareCharacter;
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 900.0f}}});
    window_.onPoll = cursorAt({20, 20});
    window_.pollsBeforeQuit = 5;
    auto app = makeApp();
    (void)app->run();
    EXPECT_FALSE(window_.clickThrough);
}

TEST_F(ApplicationTest, ClickThroughState_ChangesOnlyWhenNeeded) {
    // 매 프레임 창 스타일을 바꾸지 않음 (같은 상태면 그대로)
    renderer_.alphaAt = squareCharacter;
    window_.onPoll = cursorAt({20, 20});
    window_.pollsBeforeQuit = 20;
    auto app = makeApp();
    (void)app->run();
    EXPECT_LE(window_.clickThroughChanges, 1);
}

// ---------------------------------------------------------------------------
// 화면 전체 오버레이 (ADR-0011)
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, Overlay_DoesNotMoveWhileCharacterMoves) {
    // 끌어도 창은 그대로, 캐릭터 상자만 움직임 (매 프레임 SetWindowPos 없음)
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{900.0f, 600.0f}}});
    window_.pollsBeforeQuit = 4;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(boxTopLeft(*app), (PointI{800, 440}));
    EXPECT_TRUE(window_.positionHistory.empty());
}

TEST_F(ApplicationTest, Overlay_FollowsCharacterToAnotherMonitor) {
    // 오른쪽에 두 번째 모니터. 발이 넘어가면 창이 그 모니터의 작업 영역으로 옮겨감
    window_.monitors = {{0, 0, 1920, 1040}, {1920, 0, 3840, 1040}};
    window_.desktopBounds = {0, 0, 3840, 1080};
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{2500.0f, 1000.0f}}});
    window_.pollsBeforeQuit = 4;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(window_.position, (PointI{1920, 0}));
    EXPECT_EQ(window_.size, (core::SizeI{1920, 1040}));
    EXPECT_EQ(renderer_.lastResize, (core::SizeI{1920, 1040}));
    // 상자는 새 오버레이 기준 좌표로 그려짐 (발 x 2500 → 오버레이 안 580)
    EXPECT_FLOAT_EQ(renderer_.lastScene.placeholder.center.x, 2500.0f - 1920.0f);
}

TEST_F(ApplicationTest, Overlay_StartsOnMonitorOfSavedPosition) {
    window_.monitors = {{0, 0, 1920, 1040}, {1920, 0, 3840, 1040}};
    window_.desktopBounds = {0, 0, 3840, 1080};
    config_.state.lastX = 3000;
    config_.state.lastY = 1040;
    window_.pollsBeforeQuit = 2;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(window_.position, (PointI{1920, 0}));
    EXPECT_TRUE(window_.positionHistory.empty());  // 처음부터 그 모니터에 만들어짐
}

TEST_F(ApplicationTest, SceneRegion_FitsCurrentPoseNotFixedMargin) {
    // 렌더러는 3D를 이 영역만 그림 → 몸 자세 범위(visibleRect)를 포함하되, 예전 고정 여유
    // (좌우 0.75배·위로 상자 높이)보다 훨씬 작아야 MSAA 비용이 줄어듦
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{900.0f, 700.0f}}});
    window_.pollsBeforeQuit = 4;
    auto app = makeApp();
    app->setModel(makeSkinnedModel());
    (void)app->run();

    const core::RectI region = renderer_.lastScene.sceneRegion;
    const core::RectI box = app->characterRect();  // 오버레이 원점이 (0, 0)이라 좌표가 같음
    const core::RectI body = app->visibleRect();  // 상자 내부 좌표
    EXPECT_LE(region.left, box.left + body.left);
    EXPECT_GE(region.right, box.left + body.right);
    EXPECT_LE(region.top, box.top + body.top);
    EXPECT_GE(region.bottom, box.top + body.bottom);
    EXPECT_LT(region.width(), box.width() * 3 / 2);
    EXPECT_LT(region.height(), box.height() * 3 / 2);
}

TEST_F(ApplicationTest, SceneRegion_IncludesBonesOutsideBodyEnvelope) {
    // 메시 범위 밖으로 뻗은 본(흔들리는 머리카락 끝 등)도 영역에 들어가야 잘리지 않음
    auto model = std::make_shared<deskpet::model::Model>(deskpet::test::makeSkeletonModel());
    deskpet::model::Bone far;
    far.parent = 1;
    far.position = {1.5f, 1.2f, 0.0f};  // 몸 오른쪽 멀리 (정점 없음 → 경계 상자에 없음)
    model->bones.push_back(far);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    app->setModel(model);
    (void)app->run();

    const auto& scene = renderer_.lastScene;
    const auto clip = deskpet::core::transform(far.position, scene.character.viewProjection);
    const float x = (clip.x / clip.w + 1.0f) * 0.5f * 1920.0f;
    EXPECT_GE(static_cast<float>(std::min(scene.sceneRegion.right, 1920)), std::min(x, 1920.0f));
}

TEST_F(ApplicationTest, Overlay_SpansBothMonitorsWhileCrossingBoundary) {
    // 캐릭터가 두 모니터에 걸치면 오버레이를 두 작업 영역을 합친 크기로 → 양쪽 모두 그려짐
    window_.monitors = {{0, 0, 1920, 1040}, {1920, 0, 3840, 1040}};
    window_.desktopBounds = {0, 0, 3840, 1080};
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1920.0f, 1000.0f}}});  // 발이 경계 위
    window_.pollsBeforeQuit = 4;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(window_.position, (PointI{0, 0}));
    EXPECT_EQ(window_.size, (core::SizeI{3840, 1040}));
    EXPECT_EQ(renderer_.lastScene.viewport, (core::SizeI{3840, 1040}));
}

// ---------------------------------------------------------------------------
// 전체 화면 앱(게임·영상·프레젠테이션)이 있으면 자동으로 숨김
// ---------------------------------------------------------------------------

TEST_F(ApplicationTest, FullscreenApp_HidesPetAndStopsRendering) {
    window_.fullscreenApp = true;
    window_.pollsBeforeQuit = 60;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FALSE(window_.visible);
    EXPECT_LT(renderer_.renderCount, 5U);  // 숨긴 뒤에는 그리지 않음
}

TEST_F(ApplicationTest, FullscreenAppClosed_ShowsPetAgain) {
    window_.fullscreenApp = true;
    window_.onPoll = [](int pollIndex, FakeWindowState& w) {
        if (pollIndex == 60) {
            w.fullscreenApp = false;  // 1초 뒤 게임 종료
        }
    };
    window_.pollsBeforeQuit = 120;
    auto app = makeApp();
    (void)app->run();

    EXPECT_TRUE(window_.visible);
    EXPECT_GT(renderer_.renderCount, 20U);
}

TEST_F(ApplicationTest, UserHidden_StaysHiddenAfterFullscreenAppCloses) {
    // 메뉴로 직접 숨긴 상태는 자동 숨김이 풀려도 그대로
    window_.frames.push_back({core::TrayMenuRequestedEvent{{1800, 1000}}});
    window_.menuChoice = static_cast<int>(MenuCommand::ToggleVisible);
    window_.onPoll = [](int pollIndex, FakeWindowState& w) {
        w.fullscreenApp = pollIndex >= 10 && pollIndex < 60;
    };
    window_.pollsBeforeQuit = 120;
    auto app = makeApp();
    (void)app->run();

    EXPECT_FALSE(window_.visible);
}
