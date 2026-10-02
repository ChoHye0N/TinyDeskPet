#include "app/Application.h"

#include "app/CameraFit.h"
#include "core/Log.h"
#include "deskpet/Version.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <utility>
#include <variant>

namespace deskpet::app {
namespace {

// 플레이스홀더 캐릭터 비율 (docs/03-detailed-design/app.md §4.5)
constexpr float kBodyWidthRatio = 0.32f;
constexpr float kBodyHeightRatio = 0.27f;
constexpr float kFootMargin = 4.0f;

// Present를 생략했거나 숨김 상태일 때 이벤트를 기다리는 최대 시간 (DEBT-02)
constexpr int kIdleWaitMs = 16;
constexpr int kHiddenWaitMs = 100;

// 걷는 방향으로 몸을 돌리는 각도. 90°면 옆모습만 보이므로 얼굴이 보이게 덜 돌림
constexpr float kWalkTurnRadians = 50.0f * std::numbers::pi_v<float> / 180.0f;
// 몸 돌리기 속도: 정면 ↔ 걷는 방향을 자세 전환과 같은 시간에 돎 (방향을 바꾸면 그 두 배)
constexpr float kTurnSpeed = kWalkTurnRadians / anim::ProceduralAnimator::kTransitionSeconds;

int scaled(int value, float scale) {
    return static_cast<int>(std::lround(static_cast<float>(value) * scale));
}

// std::visit에 여러 람다를 넘기기 위한 도우미. 처리하지 않은 이벤트 타입이 있으면 컴파일 오류.
template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

int roundToInt(float value) {
    return static_cast<int>(std::lround(value));
}

platform::MenuItem makeMenuItem(MenuCommand command, std::string label, bool enabled = true) {
    platform::MenuItem item;
    item.id = static_cast<int>(command);
    item.label = std::move(label);
    item.enabled = enabled;
    return item;
}

}  // namespace

Application::Application(core::AppConfig config, std::unique_ptr<platform::IWindow> window,
                         std::unique_ptr<renderer::IRenderer> renderer, core::TimeSource timeSource)
    : config_(std::move(config)),
      window_(std::move(window)),
      renderer_(std::move(renderer)),
      now_(std::move(timeSource)),
      character_(character::Params::fromConfig(config_.character)) {
    windowSize_ = {config_.window.width, config_.window.height};
}

Application::~Application() = default;

void Application::setModel(std::shared_ptr<const model::Model> model) {
    model_ = std::move(model);
    animator_.reset();
    if (model_) {
        animator_.emplace(*model_);
        // T포즈 폭 대신 실제로 취할 자세 + 걸을 때 돌린 몸 기준 (꼬리·치마가 옆으로 나옴)
        displayBounds_ = animator_->displayBounds(kWalkTurnRadians);
    }
    refitCamera();
}

void Application::refitCamera() {
    if (model_) {
        camera_ = fitCameraToBounds(displayBounds_, windowSize_);
    }
}

// 캐릭터 상태 → 애니메이션 입력 (character와 anim이 서로를 모르도록 app이 변환, SAD 규칙 R3)
void Application::updateAnimation(float dt) {
    if (!animator_) {
        return;
    }
    const character::Pose pose = character_.pose();
    anim::AnimationInput input;
    switch (pose.state) {
        case character::State::Idle: input.motion = anim::Motion::Idle; break;
        case character::State::Walk: input.motion = anim::Motion::Walk; break;
        case character::State::Dragged: input.motion = anim::Motion::Dragged; break;
        case character::State::Airborne: input.motion = anim::Motion::Airborne; break;
    }
    input.time = pose.time;
    input.squash = pose.squash;
    input.blink = pose.eyesClosed;
    input.idleMotion = config_.animation.idleMotion;
    animator_->animate(input, dt, animation_);

    // 걷는 방향으로 몸 돌리기: 목표 각도로 일정한 속도로 돌아감 (즉시 바뀌면 튀어 보임)
    const float targetTurn = static_cast<float>(pose.facing) * kWalkTurnRadians;
    turnRadians_ = core::moveTowards(turnRadians_, targetTurn, kTurnSpeed * dt);
}

int Application::run() {
    if (!initialize()) {
        shutdown();
        return static_cast<int>(ExitCode::InitializationFailed);
    }

    while (tick()) {
    }

    shutdown();
    core::logging::info("종료 (코드 {})", static_cast<int>(exitCode_));
    return static_cast<int>(exitCode_);
}

// ---------------------------------------------------------------------------
// 초기화 / 정리 (docs/03-detailed-design/app.md §4.1)
// ---------------------------------------------------------------------------

bool Application::initialize() {
    platform::WindowDesc desc;
    desc.title = "DeskPet";
    desc.size = {windowSize_.width, windowSize_.height};
    desc.alwaysOnTop = config_.window.alwaysOnTop;

    if (!window_->create(desc)) {
        core::logging::error("창을 만들 수 없습니다");
        return false;
    }

    // 고 DPI 모니터에서는 설정 크기(96 DPI 기준)를 배율만큼 키움 (DEBT-01)
    dpiScale_ = window_->dpiScale();
    if (dpiScale_ != 1.0f) {
        windowSize_ = {scaled(config_.window.width, dpiScale_),
                       scaled(config_.window.height, dpiScale_)};
        window_->setSize(windowSize_);
        refitCamera();
        desc.size = windowSize_;
        core::logging::info("DPI 배율 {:.2f} → 창 {}x{}", dpiScale_, windowSize_.width,
                            windowSize_.height);
    }

    renderer::RendererOptions options;
    options.vsync = config_.renderer.vsync;
    options.debugLayer = config_.renderer.debugLayer;
    options.msaaSamples = config_.renderer.msaa;
    if (!renderer_->initialize(window_->nativeHandle(), desc.size, options)) {
        core::logging::error("렌더러를 초기화할 수 없습니다");
        return false;
    }

    refreshBounds();
    if (!restoreSavedPosition()) {
        resetCharacterPosition();
    }
    window_->setHitRegionEllipse(hitRegion());
    window_->show();
    if (!window_->showTrayIcon("DeskPet")) {
        core::logging::warn("트레이 아이콘을 만들 수 없습니다");
    }

    timestep_.reset();
    lastTime_ = now_();
    core::logging::info("초기화 완료");
    return true;
}

void Application::shutdown() {
    renderer_->shutdown();
}

// ---------------------------------------------------------------------------
// 메인 루프 (docs/02-architecture/SAD.md §6.3)
// ---------------------------------------------------------------------------

bool Application::tick() {
    events_.clear();
    window_->pollEvents(events_);
    for (const core::Event& event : events_) {
        handleEvent(event);
    }
    if (quitRequested_) {
        return false;
    }
    if (hidden_) {
        // 숨김 중에는 갱신·렌더링 없이 메뉴(트레이) 입력만 기다림
        window_->waitForEvents(kHiddenWaitMs);
        lastTime_ = now_();
        return true;
    }

    const double now = now_();
    const double frameSeconds = now - lastTime_;
    lastTime_ = now;

    const int steps = timestep_.advance(frameSeconds);
    const auto dt = static_cast<float>(timestep_.step());
    if (character_.state() != character::State::Dragged) {
        refreshGround();  // 걷기·던지기로 다른 모니터에 넘어갔을 수 있음
    }
    for (int i = 0; i < steps; ++i) {
        character_.update(dt);
    }

    syncWindowToCharacter();
    updateAnimation(static_cast<float>(steps) * dt);  // 이번 프레임에 진행한 시뮬레이션 시간

    switch (renderer_->render(buildScene())) {
        case renderer::FrameResult::Ok:
        case renderer::FrameResult::DeviceRecovered: break;
        case renderer::FrameResult::Skipped:
            // VSync 대기 없이 돌아왔으므로 직접 쉼. 입력이 오면 바로 깨어남
            window_->waitForEvents(kIdleWaitMs);
            break;
        case renderer::FrameResult::Fatal:
            core::logging::error("렌더링을 계속할 수 없어 종료합니다");
            exitCode_ = ExitCode::RendererFailed;
            return false;
    }
    return true;
}

void Application::handleEvent(const core::Event& event) {
    using core::MouseButton;

    std::visit(Overloaded{
                   [this](const core::PointerDownEvent& e) {
                       if (e.button == MouseButton::Left) {
                           character_.onPointerDown(e.screen);
                       }
                   },
                   [this](const core::PointerMoveEvent& e) { character_.onPointerMove(e.screen); },
                   [this](const core::PointerUpEvent& e) {
                       if (e.button == MouseButton::Left) {
                           // 다른 모니터로 옮겼을 수 있으므로 놓기 전에 바닥을 다시 계산
                           refreshGround();
                           character_.onPointerUp(e.screen);
                       } else if (e.button == MouseButton::Right) {
                           onContextMenu({roundToInt(e.screen.x), roundToInt(e.screen.y)});
                       }
                   },
                   [this](const core::DoubleClickEvent& e) {
                       if (e.button == MouseButton::Left) {
                           (void)character_.jump();
                       }
                   },
                   [this](const core::WorkAreaChangedEvent&) {
                       refreshGround();
                       refreshBounds();
                   },
                   [this](const core::QuitRequestedEvent&) { requestQuit(); },
                   [this](const core::TrayMenuRequestedEvent& e) { onContextMenu(e.screen); },
                   [this](const core::DpiChangedEvent& e) { applyDpiScale(e.scale); },
               },
               event);
}

// ---------------------------------------------------------------------------
// 메뉴 (docs/03-detailed-design/app.md §4.4)
// ---------------------------------------------------------------------------

void Application::onContextMenu(core::PointI screen) {
    const bool canJump = character_.state() == character::State::Idle ||
                         character_.state() == character::State::Walk;

    const std::vector<platform::MenuItem> items = {
        makeMenuItem(MenuCommand::About, std::format("DeskPet {}", DESKPET_VERSION_STRING), false),
        platform::MenuItem::makeSeparator(),
        makeMenuItem(MenuCommand::Jump, "점프", canJump),
        makeMenuItem(MenuCommand::ResetPosition, "위치 초기화"),
        makeMenuItem(MenuCommand::ToggleVisible, hidden_ ? "보이기" : "숨기기"),
        platform::MenuItem::makeSeparator(),
        makeMenuItem(MenuCommand::Quit, "종료"),
    };
    // 캐릭터 우클릭과 트레이 아이콘이 같은 메뉴를 씀 (FR-18)

    executeMenuCommand(window_->showContextMenu(items, screen));
}

void Application::executeMenuCommand(int commandId) {
    switch (static_cast<MenuCommand>(commandId)) {
        case MenuCommand::Jump: (void)character_.jump(); break;
        case MenuCommand::ResetPosition: resetCharacterPosition(); break;
        case MenuCommand::Quit: requestQuit(); break;
        case MenuCommand::ToggleVisible: setVisible(hidden_); break;
        case MenuCommand::About:
        default: break;  // 0(취소) 포함
    }
}

// ---------------------------------------------------------------------------
// 좌표 변환 (docs/02-architecture/SAD.md §8, app.md §4.5)
// ---------------------------------------------------------------------------

void Application::resetCharacterPosition() {
    const core::RectI area = window_->workArea();
    character_.setGround(static_cast<float>(area.bottom));

    const float halfWidth = static_cast<float>(windowSize_.width) / 2.0f;
    const core::Vec2 feet{
        static_cast<float>(area.right - scaled(config_.window.marginRight, dpiScale_)) - halfWidth,
        static_cast<float>(area.bottom),
    };
    character_.teleport(feet);
    syncWindowToCharacter();
}

bool Application::restoreSavedPosition() {
    const std::optional<core::PointI> saved = config_.state.lastPosition();
    if (!saved) {
        return false;
    }
    // 모니터 구성이 바뀌어 저장 위치가 화면 밖이면 기본 위치를 씀
    const core::RectI desktop = window_->desktopBounds();
    if (saved->x < desktop.left || saved->x >= desktop.right || saved->y <= desktop.top ||
        saved->y > desktop.bottom) {
        core::logging::info("저장된 위치 ({}, {})가 화면 밖이라 무시합니다", saved->x, saved->y);
        return false;
    }

    // 먼저 창을 그 위치로 옮겨야 해당 모니터의 바닥(작업 영역)을 알 수 있음
    const core::Vec2 feet{static_cast<float>(saved->x), static_cast<float>(saved->y)};
    character_.teleport(feet);
    syncWindowToCharacter();
    refreshGround();
    character_.teleport(feet);  // 새 바닥 기준으로 서 있을지 떨어질지 다시 판정
    syncWindowToCharacter();
    return true;
}

void Application::refreshGround() {
    character_.setGround(static_cast<float>(window_->workArea().bottom));
}

void Application::refreshBounds() {
    // 창이 가상 데스크톱 밖으로 나가지 않도록 발 x 범위를 창 반폭만큼 안쪽으로 (FR-17)
    const core::RectI desktop = window_->desktopBounds();
    const float halfWidth = static_cast<float>(windowSize_.width) / 2.0f;
    character_.setHorizontalBounds(static_cast<float>(desktop.left) + halfWidth,
                                   static_cast<float>(desktop.right) - halfWidth);
}

void Application::applyDpiScale(float scale) {
    if (!(scale > 0.0f) || scale == dpiScale_) {
        return;
    }
    dpiScale_ = scale;
    windowSize_ = {scaled(config_.window.width, scale), scaled(config_.window.height, scale)};
    core::logging::info("DPI 배율 변경 {:.2f} → 창 {}x{}", scale, windowSize_.width,
                        windowSize_.height);

    // 발 위치를 기준으로 창을 다시 놓으므로 캐릭터는 같은 자리에 서 있음
    window_->setSize(windowSize_);
    renderer_->resize(windowSize_);
    refitCamera();
    refreshBounds();
    syncWindowToCharacter();
    window_->setHitRegionEllipse(hitRegion());
}

void Application::setVisible(bool visible) {
    hidden_ = !visible;
    if (visible) {
        window_->show();
    } else {
        window_->hide();
    }
}

void Application::syncWindowToCharacter() {
    window_->setPosition(windowTopLeftFor(character_.position()));
}

core::PointI Application::windowTopLeftFor(core::Vec2 feet) const {
    const auto width = static_cast<float>(windowSize_.width);
    const auto height = static_cast<float>(windowSize_.height);
    return {roundToInt(feet.x - width / 2.0f), roundToInt(feet.y - height)};
}

core::RectI Application::hitRegion() const {
    const auto width = static_cast<float>(windowSize_.width);
    const auto height = static_cast<float>(windowSize_.height);
    if (model_) {
        return modelHitRegion();
    }
    const character::Params& params = character_.params();

    // 숨쉬기·착지 반동으로 몸통이 움직이는 범위까지 포함하도록 여유를 둡니다.
    const float unit = std::min(width, height);  // 창이 세로로 길어도 슬라임 비율 유지
    const float radiusX = unit * kBodyWidthRatio * (1.0f + 0.5f * params.landingSquashAmount);
    const float radiusY = unit * kBodyHeightRatio;
    const float padding = params.breathAmplitude + 2.0f;
    const float centerX = width / 2.0f;
    const float bottom = height - kFootMargin;

    core::RectI rect{
        roundToInt(centerX - radiusX - padding),
        roundToInt(bottom - 2.0f * radiusY - padding),
        roundToInt(centerX + radiusX + padding),
        roundToInt(bottom + padding),
    };
    rect.left = std::clamp(rect.left, 0, windowSize_.width);
    rect.top = std::clamp(rect.top, 0, windowSize_.height);
    rect.right = std::clamp(rect.right, 0, windowSize_.width);
    rect.bottom = std::clamp(rect.bottom, 0, windowSize_.height);
    return rect;
}

// 모델 경계 상자 8개 꼭짓점을 화면에 투영한 사각형 (TODO(M6): 픽셀 알파 기반 히트 테스트)
core::RectI Application::modelHitRegion() const {
    const auto width = static_cast<float>(windowSize_.width);
    const auto height = static_cast<float>(windowSize_.height);
    const model::Bounds& b = displayBounds_;

    float left = width;
    float top = height;
    float right = 0.0f;
    float bottom = 0.0f;
    for (unsigned i = 0; i < 8; ++i) {
        const core::Vec3 corner{(i & 1U) != 0 ? b.max.x : b.min.x,
                                (i & 2U) != 0 ? b.max.y : b.min.y,
                                (i & 4U) != 0 ? b.max.z : b.min.z};
        const core::Vec4 clip = core::transform(corner, camera_);
        // NDC(-1~1, 위가 +) → 창 픽셀(0~크기, 아래가 +)
        const float x = (clip.x / clip.w + 1.0f) * 0.5f * width;
        const float y = (1.0f - clip.y / clip.w) * 0.5f * height;
        left = std::min(left, x);
        right = std::max(right, x);
        top = std::min(top, y);
        bottom = std::max(bottom, y);
    }

    core::RectI rect{roundToInt(left), roundToInt(top), roundToInt(right), roundToInt(bottom)};
    rect.left = std::clamp(rect.left, 0, windowSize_.width);
    rect.top = std::clamp(rect.top, 0, windowSize_.height);
    rect.right = std::clamp(rect.right, 0, windowSize_.width);
    rect.bottom = std::clamp(rect.bottom, 0, windowSize_.height);
    return rect;
}

renderer::RenderScene Application::buildScene() const {
    const auto width = static_cast<float>(windowSize_.width);
    const auto height = static_cast<float>(windowSize_.height);
    const character::Pose pose = character_.pose();

    renderer::RenderScene scene;
    scene.viewport = {windowSize_.width, windowSize_.height};

    // 착지 반동: 세로로 눌리고 가로로 퍼짐 (발 위치는 고정)
    const float unit = std::min(width, height);  // 창이 세로로 길어도 슬라임 비율 유지
    const float radiusY = unit * kBodyHeightRatio * (1.0f - pose.squash);
    const float radiusX = unit * kBodyWidthRatio * (1.0f + 0.5f * pose.squash);

    renderer::PlaceholderCharacter& body = scene.placeholder;
    body.visible = true;
    body.center = {width / 2.0f, height - kFootMargin - radiusY + pose.breathOffset};
    body.radiusX = radiusX;
    body.radiusY = radiusY;
    body.eyesClosed = pose.eyesClosed;

    if (model_) {
        // 숨긴 슬라임 값도 기본값으로 고정: 숨쉬기로 매 프레임 바뀌면 장면 비교가 항상 "다름"이
        // 되어 렌더러가 Present를 생략하지 못함 (DEBT-02)
        body = {};
        body.visible = false;
        // 착지 반동을 모델에도 적용: 발(y=0) 기준으로 세로 압축·가로 확장
        core::Mat4 squash = core::Mat4::identity();
        squash.m[0][0] = squash.m[2][2] = 1.0f + 0.5f * pose.squash;
        squash.m[1][1] = 1.0f - pose.squash;
        // 걷는 방향으로 몸을 돌림 (정면 +Z → +X 쪽이 +각도)
        const core::Mat4 turn = core::Mat4::rotationY(turnRadians_);
        scene.character.model = model_.get();
        scene.character.viewProjection = turn * squash * camera_;
        scene.character.skinMatrices = animation_.skin;
        scene.character.expressionWeights = animation_.expressions;
    }
    // TODO(M4): Pose → 본 행렬·모프 가중치 변환

    return scene;
}

}  // namespace deskpet::app
