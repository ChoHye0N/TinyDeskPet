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
constexpr PointI kStartTopLeft{1920 - 40 - 200, 1040 - 200};   // (1680, 840)
constexpr Vec2 kStartFeet{1920.0f - 40.0f - 100.0f, 1040.0f};  // (1780, 1040)

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
    EXPECT_EQ(window_.position, kStartTopLeft);
    EXPECT_EQ(app->character().state(), State::Idle);
    EXPECT_EQ(app->character().position(), kStartFeet);
    EXPECT_TRUE(renderer_.shutdownCalled);
}

TEST_F(ApplicationTest, Startup_SetsHitRegionInsideWindow) {
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    (void)app->run();

    const core::RectI& hit = window_.hitRegion;
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
    EXPECT_EQ(scene.viewport, (core::SizeI{200, 200}));
    EXPECT_FLOAT_EQ(scene.placeholder.center.x, 100.0f);
    EXPECT_GT(scene.placeholder.radiusX, 0.0f);
    EXPECT_LT(scene.placeholder.center.y + scene.placeholder.radiusY, 200.0f);
}

TEST_F(ApplicationTest, Drag_WindowFollowsPointer) {
    // 발보다 40px 위를 잡고 (-80, -100) 만큼 끌기
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 900.0f}}});
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Dragged);
    // 발 = (1700, 940) → 창 좌상단 = (1600, 740)
    EXPECT_EQ(window_.position, (PointI{1600, 740}));
}

TEST_F(ApplicationTest, DragAndRelease_FallsBackToGround) {
    window_.frames.push_back({core::PointerDownEvent{{1780.0f, 1000.0f}, core::MouseButton::Left}});
    window_.frames.push_back({core::PointerMoveEvent{{1700.0f, 500.0f}}});
    for (int i = 0; i < 12; ++i) {
        window_.frames.push_back({});  // 0.2초 멈춤 → 던지기 없이 떨어뜨림 (FR-16)
    }
    window_.frames.push_back({core::PointerUpEvent{{1700.0f, 500.0f}, core::MouseButton::Left}});
    window_.pollsBeforeQuit = 180;  // 3초면 충분히 착지
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Idle);
    EXPECT_FLOAT_EQ(app->character().position().y, 1040.0f);
    EXPECT_EQ(window_.position, (PointI{1600, 840}));

    // 낙하하는 동안 창이 아래로만 움직였는지 확인. 놓은 지점: 발 (1700, 540) → 창 (1600, 340)
    const auto& history = window_.positionHistory;
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

TEST_F(ApplicationTest, MenuJump_MakesCharacterAirborne) {
    window_.frames.push_back({core::PointerUpEvent{{1780.0f, 1000.0f}, core::MouseButton::Right}});
    window_.menuChoice = static_cast<int>(MenuCommand::Jump);
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(app->character().state(), State::Airborne);
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
    EXPECT_EQ(window_.position.y, 900 - 200);
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

    // 발 중앙(0,0,0)이 창 아래쪽 가운데로 투영됨
    const auto clip = deskpet::core::transform({0.0f, 0.0f, 0.0f}, scene.character.viewProjection);
    EXPECT_NEAR(clip.x / clip.w, 0.0f, 1e-4f);
    EXPECT_LT(clip.y / clip.w, -0.9f);
}

TEST_F(ApplicationTest, WithModel_HitRegionCoversProjectedModel) {
    window_.pollsBeforeQuit = 1;
    auto app = makeApp();
    app->setModel(makeHumanoidModel());
    (void)app->run();

    // T포즈(폭 1.5 > 키 1.6 × 창 비율 1)라 거의 창 전체 폭을 차지
    const core::RectI& hit = window_.hitRegion;
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

    EXPECT_EQ(window_.size, (core::SizeI{300, 300}));
    EXPECT_EQ(renderer_.initialSize, (core::SizeI{300, 300}));
    EXPECT_EQ(renderer_.lastScene.viewport, (core::SizeI{300, 300}));
    // 발은 그대로 바닥에, 창 아래쪽이 바닥에 맞음
    EXPECT_EQ(window_.position.y + 300, 1040);
}

TEST_F(ApplicationTest, DpiChanged_ResizesWindowAndRendererKeepingFeet) {
    window_.frames.push_back({core::DpiChangedEvent{2.0f}});
    window_.pollsBeforeQuit = 3;
    auto app = makeApp();
    (void)app->run();

    EXPECT_EQ(window_.size, (core::SizeI{400, 400}));
    EXPECT_EQ(renderer_.lastResize, (core::SizeI{400, 400}));
    EXPECT_EQ(renderer_.lastScene.viewport, (core::SizeI{400, 400}));
    // 발 높이는 그대로. 커진 슬라임이 오른쪽 화면 끝(1920)을 넘지 않을 만큼만 안쪽으로 밀림
    // (FR-17). 창의 투명한 여백은 화면 밖으로 나가도 됨
    EXPECT_EQ(window_.position.y, 1040 - 400);
    EXPECT_LE(window_.position.x + window_.hitRegion.right, 1920);
    EXPECT_LT(window_.position.x, 1780 - 200);  // 발(1780)이 그대로면 넘치므로 안쪽으로 밀림
    EXPECT_LE(window_.hitRegion.right, 400);
    EXPECT_GT(window_.hitRegion.width(), 200);
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
    const int visibleRight = window_.position.x + window_.hitRegion.right;
    EXPECT_LE(visibleRight, 1920);
    EXPECT_GE(visibleRight, 1918);
    EXPECT_GT(window_.position.x + 200, 1920);
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

    const int visibleLeft = window_.position.x + window_.hitRegion.left;
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

    EXPECT_EQ(window_.position, kStartTopLeft);
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
