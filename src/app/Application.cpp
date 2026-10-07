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

// 저장된 값이 10% 단위가 아니거나 범위 밖이어도 메뉴 단계와 맞도록 정리
int snapScalePercent(int percent) {
    const int snapped = static_cast<int>(
        std::lround(static_cast<float>(percent) / kScaleStepPercent) * kScaleStepPercent);
    return std::clamp(snapped, kMinScalePercent, kMaxScalePercent);
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
    scalePercent_ = snapScalePercent(config_.state.scale.value_or(100));
    boxSize_ = targetBoxSize();
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
    refreshBounds();  // 그려지는 폭이 바뀌었으므로 벽 위치도 다시 계산
}

void Application::refitCamera() {
    if (model_) {
        camera_ = fitCameraToBounds(displayBounds_, boxSize_);
        // 발 평면에서 1m가 화면에서 몇 px인지 → 창이 움직인 px를 모델 공간 m로 바꿀 때 씀
        const core::Vec4 a = core::transform({0.0f, 0.0f, 0.0f}, camera_);
        const core::Vec4 b = core::transform({0.0f, 1.0f, 0.0f}, camera_);
        const float pixelsPerMeter =
            (b.y / b.w - a.y / a.w) * 0.5f * static_cast<float>(boxSize_.height);
        metersPerPixel_ = pixelsPerMeter > 0.0f ? 1.0f / pixelsPerMeter : 0.0f;
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

    // 흔들림 관성: 이번 프레임에 화면에서 움직인 거리 → 모델 공간 m.
    // 화면 y는 아래가 +, 모델 y는 위가 +. 걸을 때 돌린 몸의 반대로 돌려 모델 축에 맞춤
    // (앞으로 걸으면 머리카락이 뒤로 날림). 시뮬레이션 단계가 없던 프레임은 다음으로 넘김
    const core::Vec2 feet = character_.position();
    if (dt > 0.0f) {
        if (hasLastFeet_) {
            const core::Vec2 moved = feet - lastFeet_;
            const core::Vec3 world{moved.x * metersPerPixel_, -moved.y * metersPerPixel_, 0.0f};
            input.movement = core::Quat::axisAngle({0.0f, 1.0f, 0.0f}, -turnRadians_).rotate(world);
        }
        lastFeet_ = feet;
        hasLastFeet_ = true;
    }
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
    // 오버레이 창: 캐릭터가 시작할 모니터의 작업 영역 전체를 덮는 투명 창 (ADR-0011).
    // 캐릭터는 창 안에서 움직이고, 캐릭터가 아닌 픽셀은 클릭이 아래 창으로 통과 (FR-04).
    // 모니터 전체가 아니라 작업 영역인 이유: 모니터를 꽉 채운 항상 위 창은 Windows가 전체 화면
    // 앱으로 보고 알림을 막을 수 있음
    const std::optional<core::PointI> saved = config_.state.lastPosition();
    overlay_ = window_->workAreaAt(saved.value_or(core::PointI{0, 0}));

    platform::WindowDesc desc;
    desc.title = "DeskPet";
    desc.position = {overlay_.left, overlay_.top};
    desc.size = {overlay_.width(), overlay_.height()};
    desc.alwaysOnTop = config_.window.alwaysOnTop;

    if (!window_->create(desc)) {
        core::logging::error("창을 만들 수 없습니다");
        return false;
    }

    // 고 DPI 모니터에서는 캐릭터 크기(96 DPI 기준)를 배율만큼 키움 (DEBT-01)
    dpiScale_ = window_->dpiScale();
    if (targetBoxSize() != boxSize_) {
        boxSize_ = targetBoxSize();
        refitCamera();
        core::logging::info("DPI 배율 {:.2f}, 크기 {}% → 캐릭터 {}x{}", dpiScale_, scalePercent_,
                            boxSize_.width, boxSize_.height);
    }

    renderer::RendererOptions options;
    options.vsync = config_.renderer.vsync;
    options.debugLayer = config_.renderer.debugLayer;
    options.msaaSamples = config_.renderer.msaa;
    options.outline = config_.renderer.outline;
    if (!renderer_->initialize(window_->nativeHandle(), desc.size, options)) {
        core::logging::error("렌더러를 초기화할 수 없습니다");
        return false;
    }

    refreshBounds();
    if (!restoreSavedPosition()) {
        resetCharacterPosition();
    }
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

    updateOverlay();
    updateAnimation(static_cast<float>(steps) * dt);  // 이번 프레임에 진행한 시뮬레이션 시간

    const renderer::FrameResult frame = renderer_->render(buildScene());
    updateClickThrough();
    switch (frame) {
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
                           pointerDown_ = true;
                           character_.onPointerDown(e.screen);
                       }
                   },
                   [this](const core::PointerMoveEvent& e) { character_.onPointerMove(e.screen); },
                   [this](const core::PointerUpEvent& e) {
                       if (e.button == MouseButton::Left) {
                           pointerDown_ = false;
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
                       overlay_ = {};  // 작업 영역 크기가 바뀌었을 수 있으므로 다시 맞춤
                       updateOverlay();
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
    const std::vector<platform::MenuItem> items = {
        makeMenuItem(MenuCommand::About, std::format("DeskPet {}", DESKPET_VERSION_STRING), false),
        platform::MenuItem::makeSeparator(),
        makeMenuItem(MenuCommand::ScaleInfo, std::format("크기 {}%", scalePercent_), false),
        makeMenuItem(MenuCommand::ScaleUp, "크게", scalePercent_ < kMaxScalePercent),
        makeMenuItem(MenuCommand::ScaleDown, "작게", scalePercent_ > kMinScalePercent),
        platform::MenuItem::makeSeparator(),
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
        case MenuCommand::ScaleUp: setScalePercent(scalePercent_ + kScaleStepPercent); break;
        case MenuCommand::ScaleDown: setScalePercent(scalePercent_ - kScaleStepPercent); break;
        case MenuCommand::ResetPosition: resetCharacterPosition(); break;
        case MenuCommand::Quit: requestQuit(); break;
        case MenuCommand::ToggleVisible: setVisible(hidden_); break;
        case MenuCommand::About:
        case MenuCommand::ScaleInfo:
        default: break;  // 0(취소) 포함
    }
}

// ---------------------------------------------------------------------------
// 좌표 변환 (docs/02-architecture/SAD.md §8, app.md §4.5)
// ---------------------------------------------------------------------------

void Application::resetCharacterPosition() {
    const core::RectI area = overlay_;  // 지금 캐릭터가 있는 모니터
    character_.setGround(static_cast<float>(area.bottom));

    const float halfWidth = static_cast<float>(boxSize_.width) / 2.0f;
    const core::Vec2 feet{
        static_cast<float>(area.right - scaled(config_.window.marginRight, dpiScale_)) - halfWidth,
        static_cast<float>(area.bottom),
    };
    character_.teleport(feet);
    updateOverlay();
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

    const core::Vec2 feet{static_cast<float>(saved->x), static_cast<float>(saved->y)};
    character_.teleport(feet);
    updateOverlay();
    refreshGround();            // 그 모니터의 바닥
    character_.teleport(feet);  // 새 바닥 기준으로 서 있을지 떨어질지 다시 판정
    return true;
}

void Application::refreshGround() {
    // 발이 있는 모니터의 작업 영역 바닥 (걷기·던지기로 다른 모니터에 넘어갔을 수 있음)
    const core::Vec2 feet = character_.position();
    character_.setGround(
        static_cast<float>(window_->workAreaAt({roundToInt(feet.x), roundToInt(feet.y)}).bottom));
}

void Application::refreshBounds() {
    // 그려지는 영역(visibleRect)이 가상 데스크톱 밖으로 나가지 않도록 발 x 범위를 정함 (FR-17).
    // 상자 반폭으로 막으면 모델 바깥의 투명한 여백 때문에 화면 끝 앞에서 멈춰 보이므로,
    // 상자의 투명한 부분은 화면 밖으로 나가도 됨. 발은 상자 가로 중앙
    const core::RectI desktop = window_->desktopBounds();
    const core::RectI visible = visibleRect();
    const float center = static_cast<float>(boxSize_.width) / 2.0f;
    character_.setHorizontalBounds(
        static_cast<float>(desktop.left) + (center - static_cast<float>(visible.left)),
        static_cast<float>(desktop.right) - (static_cast<float>(visible.right) - center));
}

void Application::applyDpiScale(float scale) {
    if (!(scale > 0.0f) || scale == dpiScale_) {
        return;
    }
    dpiScale_ = scale;
    resizeCharacter();
    core::logging::info("DPI 배율 변경 {:.2f} → 캐릭터 {}x{}", scale, boxSize_.width,
                        boxSize_.height);
}

void Application::setScalePercent(int percent) {
    percent = std::clamp(percent, kMinScalePercent, kMaxScalePercent);
    if (percent == scalePercent_) {
        return;
    }
    scalePercent_ = percent;
    resizeCharacter();
    core::logging::info("크기 {}% → 캐릭터 {}x{}", scalePercent_, boxSize_.width, boxSize_.height);
}

// 설정 크기(96 DPI 기준) × DPI 배율 × 사용자 배율. 슬라임·모델 모두 상자에 맞춰 그리므로
// 상자 크기만 바꾸면 캐릭터 크기가 바뀜. 물리 상수(중력, 걷기 속도 등)는 그대로
core::SizeI Application::targetBoxSize() const {
    const float scale = dpiScale_ * static_cast<float>(scalePercent_) / 100.0f;
    return {scaled(config_.window.width, scale), scaled(config_.window.height, scale)};
}

void Application::resizeCharacter() {
    // 발 위치는 그대로라 캐릭터는 같은 자리에 서 있음. 창(오버레이)은 그대로
    boxSize_ = targetBoxSize();
    refitCamera();
    refreshBounds();
}

void Application::setVisible(bool visible) {
    hidden_ = !visible;
    if (visible) {
        window_->show();
    } else {
        window_->hide();
    }
}

// 발이 다른 모니터로 넘어갔거나 작업 영역이 바뀌었을 때만 창을 옮김 (매 프레임 옮기지 않음).
// 다른 DPI의 모니터로 옮기면 OS가 WM_DPICHANGED를 보내 캐릭터 크기도 맞춰짐
void Application::updateOverlay() {
    const core::Vec2 feet = character_.position();
    const core::RectI area = window_->workAreaAt({roundToInt(feet.x), roundToInt(feet.y)});
    if (area == overlay_) {
        return;
    }
    overlay_ = area;
    window_->setPosition({area.left, area.top});
    window_->setSize({area.width(), area.height()});
    renderer_->resize({area.width(), area.height()});
}

core::RectI Application::characterRect() const {
    const core::Vec2 feet = character_.position();
    const int left = roundToInt(feet.x - static_cast<float>(boxSize_.width) / 2.0f);
    const int top = roundToInt(feet.y - static_cast<float>(boxSize_.height));
    return {left, top, left + boxSize_.width, top + boxSize_.height};
}

core::PointI Application::boxOffset() const {
    const core::RectI box = characterRect();
    return {box.left - overlay_.left, box.top - overlay_.top};
}

// 카메라는 상자(boxSize_)를 화면 전체로 보고 맞춰져 있음. 투영 뒤 클립 좌표를 상자가 오버레이
// 안에서 차지하는 자리로 옮기는 2D 변환 (x' = x·sx + w·cx). 뷰포트를 상자로 줄이지 않으므로
// 상자 밖으로 나간 머리카락·팔도 잘리지 않음
core::Mat4 Application::boxToOverlay() const {
    const auto overlayW = static_cast<float>(std::max(overlay_.width(), 1));
    const auto overlayH = static_cast<float>(std::max(overlay_.height(), 1));
    const core::PointI offset = boxOffset();
    const float centerX = static_cast<float>(offset.x) + static_cast<float>(boxSize_.width) * 0.5f;
    const float centerY = static_cast<float>(offset.y) + static_cast<float>(boxSize_.height) * 0.5f;
    core::Mat4 m = core::Mat4::identity();
    m.m[0][0] = static_cast<float>(boxSize_.width) / overlayW;
    m.m[1][1] = static_cast<float>(boxSize_.height) / overlayH;
    m.m[3][0] = centerX / overlayW * 2.0f - 1.0f;  // 상자 중심의 NDC (y는 위가 +)
    m.m[3][1] = 1.0f - centerY / overlayH * 2.0f;
    return m;
}

core::RectI Application::visibleRect() const {
    const auto width = static_cast<float>(boxSize_.width);
    const auto height = static_cast<float>(boxSize_.height);
    if (model_) {
        return modelVisibleRect();
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
    rect.left = std::clamp(rect.left, 0, boxSize_.width);
    rect.top = std::clamp(rect.top, 0, boxSize_.height);
    rect.right = std::clamp(rect.right, 0, boxSize_.width);
    rect.bottom = std::clamp(rect.bottom, 0, boxSize_.height);
    return rect;
}

// 커서 아래 픽셀이 캐릭터면 클릭을 받고, 투명하면 아래 창으로 넘김 (FR-04).
// 클릭 통과 중에는 마우스 메시지가 오지 않으므로 커서 위치를 매 프레임 직접 확인.
// 알파는 몇 프레임 늦게 오지만, 커서가 캐릭터에 닿자마자 누르는 경우가 아니면 차이가 없음
void Application::updateClickThrough() {
    constexpr float kHitAlpha = 0.1f;  // 외곽선·안티에일리어싱으로 반투명한 가장자리도 캐릭터로
    if (pointerDown_) {
        window_->setClickThrough(false);  // 끄는 중에는 커서가 투명한 곳으로 가도 계속 받음
        return;
    }
    const core::PointI cursor = window_->cursorPosition();
    const core::PointI local{cursor.x - overlay_.left, cursor.y - overlay_.top};
    const bool insideWindow =
        local.x >= 0 && local.y >= 0 && local.x < overlay_.width() && local.y < overlay_.height();
    if (!insideWindow) {
        window_->setClickThrough(true);
        return;
    }
    const std::optional<renderer::AlphaSample> sample = renderer_->sampleAlpha(local);
    const bool overCharacter = sample && sample->alpha > kHitAlpha;
    window_->setClickThrough(!overCharacter);
}

// 모델 경계 상자 8개 꼭짓점을 화면에 투영한 사각형
core::RectI Application::modelVisibleRect() const {
    const auto width = static_cast<float>(boxSize_.width);
    const auto height = static_cast<float>(boxSize_.height);
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
    rect.left = std::clamp(rect.left, 0, boxSize_.width);
    rect.top = std::clamp(rect.top, 0, boxSize_.height);
    rect.right = std::clamp(rect.right, 0, boxSize_.width);
    rect.bottom = std::clamp(rect.bottom, 0, boxSize_.height);
    return rect;
}

// 3D가 그려질 영역 (오버레이 px): 몸 자세 범위(모든 동작의 경계 상자 투영) ∪ 지금 본 위치
// (흔들리는 머리카락 포함)의 투영 범위, 메시 두께·외곽선 여유를 더함. 렌더러가 3D·MSAA를 이
// 크기로만 그림 → 대기 중에는 상자 크기 정도라 화면 전체·고정 여유보다 훨씬 쌈 (ADR-0011)
core::RectI Application::sceneRegionFor(const core::Mat4& viewProjection) const {
    const core::PointI offset = boxOffset();
    const core::RectI body = visibleRect();
    auto left = static_cast<float>(offset.x + body.left);
    auto top = static_cast<float>(offset.y + body.top);
    auto right = static_cast<float>(offset.x + body.right);
    auto bottom = static_cast<float>(offset.y + body.bottom);
    const auto width = static_cast<float>(overlay_.width());
    const auto height = static_cast<float>(overlay_.height());
    if (animation_.skin.size() == model_->bones.size()) {
        for (std::size_t j = 0; j < model_->bones.size(); ++j) {
            const core::Vec3 posed =
                core::transformPoint(model_->bones[j].position, animation_.skin[j]);
            const core::Vec4 clip = core::transform(posed, viewProjection);
            if (clip.w <= 0.0f) {
                continue;
            }
            const float x = (clip.x / clip.w + 1.0f) * 0.5f * width;
            const float y = (1.0f - clip.y / clip.w) * 0.5f * height;
            left = std::min(left, x);
            right = std::max(right, x);
            top = std::min(top, y);
            bottom = std::max(bottom, y);
        }
    }
    // 본은 관절 중심이라 메시(머리카락 다발 폭, 치마 천)가 그보다 바깥에 있음
    const float pad = std::max(8.0f, 0.08f * static_cast<float>(boxSize_.width));
    return {std::max(roundToInt(left - pad), 0), std::max(roundToInt(top - pad), 0),
            std::min(roundToInt(right + pad), overlay_.width()),
            std::min(roundToInt(bottom + pad), overlay_.height())};
}

renderer::RenderScene Application::buildScene() const {
    const auto width = static_cast<float>(boxSize_.width);
    const auto height = static_cast<float>(boxSize_.height);
    const character::Pose pose = character_.pose();
    const core::PointI offset = boxOffset();

    renderer::RenderScene scene;
    scene.viewport = {overlay_.width(), overlay_.height()};

    // 착지 반동: 세로로 눌리고 가로로 퍼짐 (발 위치는 고정)
    const float unit = std::min(width, height);  // 상자가 세로로 길어도 슬라임 비율 유지
    const float radiusY = unit * kBodyHeightRatio * (1.0f - pose.squash);
    const float radiusX = unit * kBodyWidthRatio * (1.0f + 0.5f * pose.squash);

    renderer::PlaceholderCharacter& body = scene.placeholder;
    body.visible = true;
    body.center = {
        static_cast<float>(offset.x) + width / 2.0f,
        static_cast<float>(offset.y) + height - kFootMargin - radiusY + pose.breathOffset};
    body.radiusX = radiusX;
    body.radiusY = radiusY;
    body.eyesClosed = pose.eyesClosed;

    if (model_) {
        // 숨긴 슬라임 값도 기본값으로 고정: 숨쉬기로 매 프레임 바뀌면 장면 비교가 항상 "다름"이
        // 되어 렌더러가 Present를 생략하지 못함 (DEBT-02)
        body = {};
        body.visible = false;
        // 걷는 방향으로 몸을 돌림 (정면 +Z → +X 쪽이 +각도)
        const core::Mat4 turn = core::Mat4::rotationY(turnRadians_);
        scene.character.model = model_.get();
        // 착지 반동은 슬라임처럼 늘이지 않고 관절로 웅크림 (anim의 applyCrouch)
        scene.character.viewProjection = turn * camera_ * boxToOverlay();
        scene.character.skinMatrices = animation_.skin;
        scene.character.expressionWeights = animation_.expressions;
        scene.sceneRegion = sceneRegionFor(scene.character.viewProjection);
    }

    return scene;
}

}  // namespace deskpet::app
