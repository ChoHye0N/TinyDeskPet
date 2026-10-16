#include "app/Application.h"

#include "app/CameraFit.h"
#include "app/Ground.h"
#include "core/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>
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
// 동작이 바뀌어 그려지는 폭이 넓어질 때 벽이 따라가는 속도 (상자 폭/s). 상자 폭의 30%가 0.2초
constexpr float kWallSpeedBoxesPerSecond = 1.5f;
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

// 캐릭터 상태 → 애니메이션 동작 (character와 anim이 서로를 모르도록 app이 변환, SAD 규칙 R3)
// 클립을 붙여 자세 경계 상자를 잼 (모든 동작 + 걸을 때 돌린 몸, 서 있는 자세, 동작별).
// 무거우므로 모델 로딩·클립 변경은 작업 스레드에서 부름
Application::PoseBounds poseBoundsFor(anim::ProceduralAnimator& animator,
                                      const Application::MotionClips& clips, core::SizeI box) {
    for (std::size_t m = 0; m < clips.size(); ++m) {
        animator.setClip(static_cast<anim::Motion>(m), clips[m]);
    }
    Application::PoseBounds bounds;
    bounds.standing = animator.standingBounds();
    // 화면 x 범위는 카메라에 따라 다름. 카메라의 가로 투영은 서 있는 자세와 상자의 가로세로
    // 비율로만 정해지므로(모든 동작 상자는 깊이 범위에만 쓰임) 먼저 서 있는 자세로 카메라를 맞춘
    // 뒤, 자세마다 한 번의 스키닝으로 동작별 상자와 화면 범위를 함께 잼. 크기(%)·DPI가 바뀌어도
    // 그대로
    const anim::PoseExtents extents = animator.poseExtents(
        fitCameraToBounds(bounds.standing, box, bounds.standing), kWalkTurnRadians);
    bounds.motions = extents.motions;
    bounds.screenX = extents.screenX;
    bounds.all = bounds.motions[0];
    for (const model::Bounds& b : bounds.motions) {  // displayBounds와 같음 (다시 재지 않음)
        bounds.all.min = {std::min(bounds.all.min.x, b.min.x), std::min(bounds.all.min.y, b.min.y),
                          std::min(bounds.all.min.z, b.min.z)};
        bounds.all.max = {std::max(bounds.all.max.x, b.max.x), std::max(bounds.all.max.y, b.max.y),
                          std::max(bounds.all.max.z, b.max.z)};
    }
    return bounds;
}

anim::Motion motionFor(character::State state) {
    switch (state) {
        case character::State::Idle: return anim::Motion::Idle;
        case character::State::Walk: return anim::Motion::Walk;
        case character::State::Dragged: return anim::Motion::Dragged;
        case character::State::Airborne: return anim::Motion::Airborne;
        case character::State::Interact: return anim::Motion::Interact;
    }
    return anim::Motion::Idle;
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

// 작업 스레드가 this를 쓰지 않지만(클립·로더를 복사), 결과를 버리기 전에 끝나길 기다림
Application::~Application() {
    waitForModelLoad();
}

void Application::setModel(std::shared_ptr<const model::Model> model) {
    model_ = std::move(model);
    animator_.reset();
    if (model_) {
        animator_.emplace(*model_);
    }
    applyAnimation();
}

void Application::setMotionClips(MotionClips clips) {
    clips_ = std::move(clips);
    if (animator_) {
        animator_.emplace(*model_);  // 전환·흔들림 상태도 처음부터
    }
    applyAnimation();
}

void Application::setModelLoader(ModelLoader loader) {
    modelLoader_ = std::move(loader);
}

void Application::loadModelAsync(std::filesystem::path path) {
    if (!modelLoader_) {
        return;
    }
    waitForModelLoad();  // 앞선 요청이 있으면 그 결과는 버림 (마지막 요청만 반영)
    pendingModel_ = std::async(std::launch::async, [loader = modelLoader_, clips = clips_,
                                                    path = std::move(path), box = boxSize_] {
        LoadedModel loaded{path, loader(path), {}};
        if (loaded.model) {
            // 경계 상자 계산(정점 CPU 스키닝)도 무거우므로 여기서. 같은
            // 클립으로 계산해야 맞음
            anim::ProceduralAnimator animator(*loaded.model);
            loaded.bounds = poseBoundsFor(animator, clips, box);
        }
        return loaded;
    });
}

// 설정 창에서 클립을 바꿈: 지금 모델 그대로 자세 경계 상자만 작업 스레드에서 다시 계산하고, 끝나면
// applyLoadedModel이 새 클립으로 교체 (큰 모델은 경계 상자 계산이 수백 ms라 프레임이 멈추지 않게)
void Application::refreshAnimationAsync() {
    if (pendingModel_.valid()) {
        pendingModel_.wait();  // 읽던 모델을 먼저 반영 (덮어쓰면 그 결과를 잃음)
        applyLoadedModel();
    }
    if (!model_) {
        return;  // 클립은 모델을 읽을 때 붙음
    }
    pendingModel_ = std::async(
        std::launch::async, [model = model_, clips = clips_, path = modelPath_, box = boxSize_] {
            anim::ProceduralAnimator animator(*model);
            return LoadedModel{path, model, poseBoundsFor(animator, clips, box)};
        });
}

// 여백 채우기 방식이 바뀜: 지금 모델을 복사해 텍스처만 작업 스레드에서 다시 준비하고, 자세 경계
// 상자는 그대로 씀 (텍스처와 무관). 끝나면 applyLoadedModel이 교체. 처리기가 실패하면 파일을
// 다시 읽음 (loadModelAsync와 같은 결과)
void Application::retextureAsync(core::TexturePadding padding) {
    if (pendingModel_.valid()) {
        pendingModel_.wait();  // 읽던 모델을 먼저 반영 (덮어쓰면 그 결과를 잃음)
        applyLoadedModel();
    }
    if (!model_) {
        return;
    }
    pendingModel_ =
        std::async(std::launch::async, [processor = textureProcessor_, loader = modelLoader_,
                                        model = model_, clips = clips_, path = modelPath_,
                                        bounds = poseBounds_, box = boxSize_, padding] {
            auto copy = std::make_shared<model::Model>(*model);
            if (processor(*copy, padding)) {
                return LoadedModel{path, std::move(copy), bounds};
            }
            LoadedModel loaded{path, loader && !path.empty() ? loader(path) : nullptr, {}};
            if (loaded.model) {
                anim::ProceduralAnimator animator(*loaded.model);
                loaded.bounds = poseBoundsFor(animator, clips, box);
            }
            return loaded;
        });
}

void Application::waitForModelLoad() {
    if (pendingModel_.valid()) {
        pendingModel_.wait();
    }
}

void Application::applyLoadedModel() {
    if (!pendingModel_.valid() ||
        pendingModel_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    LoadedModel loaded = pendingModel_.get();
    if (!loaded.model) {
        return;  // 실패 이유는 로더가 로그에 남김. 지금 모델 유지
    }
    model_ = std::move(loaded.model);
    modelPath_ = std::move(loaded.path);
    animator_.emplace(*model_);
    for (std::size_t m = 0; m < clips_.size(); ++m) {
        animator_->setClip(static_cast<anim::Motion>(m), clips_[m]);
    }
    poseBounds_ = loaded.bounds;
    hasLastFeet_ = false;  // 흔들림 관성이 이전 모델 기준으로 튀지 않게
    refitCamera();
    refreshBounds();
}

void Application::openModelFile() {
    if (std::optional<std::filesystem::path> path = window_->showOpenModelDialog()) {
        loadModelAsync(std::move(*path));
    }
}

void Application::applyAnimation() {
    if (animator_) {
        // T포즈 폭 대신 실제로 취할 자세 + 걸을 때 돌린 몸 기준 (꼬리·치마가 옆으로 나옴)
        poseBounds_ = poseBoundsFor(*animator_, clips_, boxSize_);
    }
    refitCamera();
    refreshBounds();  // 그려지는 폭이 바뀌었으므로 벽 위치도 다시 계산
}

void Application::refitCamera() {
    if (model_) {
        const model::Bounds& fit = poseBounds_.standing;
        const model::Bounds& all = poseBounds_.all;
        core::logging::debug(
            "화면 맞춤 경계 (m): 서 있는 자세 x {:.2f} ~ {:.2f}, y {:.2f} ~ {:.2f} / 모든 동작 x "
            "{:.2f} ~ {:.2f}, y {:.2f} ~ {:.2f}, z {:.2f} ~ {:.2f}",
            fit.min.x, fit.max.x, fit.min.y, fit.max.y, all.min.x, all.max.x, all.min.y, all.max.y,
            all.min.z, all.max.z);
        // 크기·바닥은 서 있는 자세(클립과 무관)로: 모든 동작 상자로 정하면 클립을 바꿀 때마다
        // 캐릭터 크기가 바뀌고, 잠깐 웅크리는 순간이 있으면 평소 그만큼 떠 보임. 상자 밖으로
        // 뻗는 동작은 그리는 영역·벽 계산이 모든 동작 상자를 쓰므로 잘리지 않음
        camera_ = fitCameraToBounds(fit, boxSize_, all);
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
    input.motion = motionFor(pose.state);
    input.time = pose.time;
    input.motionSerial = pose.actionSerial;
    input.squash = pose.squash;
    input.blink = pose.eyesClosed;
    input.idleMotion = config_.animation.idleMotion;
    input.lookTarget = lookTarget();

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

// 커서를 바라볼 목표 (모델 공간). 화면에서 머리 → 커서 차이를 발 평면의 m로 바꾸고, 커서는
// 화면 앞쪽(카메라 쪽)에 있다고 보고 모델 키의 0.6배만큼 앞에 둠. 멀리 있으면 정면을 봄
std::optional<core::Vec3> Application::lookTarget() const {
    constexpr float kRadiusBoxHeights = 3.0f;  // 이 거리(상자 높이 배수) 안의 커서만 바라봄
    constexpr float kDepthHeights = 0.6f;
    const int head = model_ ? model_->findBone(model::HumanBone::Head) : -1;
    if (!config_.animation.lookAt || hidden_ || autoHidden_ || head < 0 ||
        metersPerPixel_ <= 0.0f) {
        return std::nullopt;
    }
    const auto h = static_cast<std::size_t>(head);
    core::Vec3 headModel = model_->bones[h].position;
    if (h < animation_.skin.size()) {
        headModel = core::transformPoint(headModel, animation_.skin[h]);  // 직전 프레임 자세
    }
    const core::Mat4 viewProjection =
        core::Mat4::rotationY(turnRadians_) * camera_ * boxToOverlay();
    const core::Vec4 clip = core::transform(headModel, viewProjection);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }
    const float headX = static_cast<float>(overlay_.left) +
                        (clip.x / clip.w + 1.0f) * 0.5f * static_cast<float>(overlay_.width());
    const float headY = static_cast<float>(overlay_.top) +
                        (1.0f - clip.y / clip.w) * 0.5f * static_cast<float>(overlay_.height());
    const core::PointI cursor = window_->cursorPosition();
    const float dx = static_cast<float>(cursor.x) - headX;
    const float dy = static_cast<float>(cursor.y) - headY;
    const float radius = kRadiusBoxHeights * static_cast<float>(boxSize_.height);
    if (dx * dx + dy * dy > radius * radius) {
        return std::nullopt;
    }
    // 화면 기준 (오른쪽 +X, 위 +Y, 카메라 쪽 +Z) → 몸을 돌린 만큼 반대로 돌려 모델 축으로
    const float depth = kDepthHeights * poseBounds_.standing.size().y;
    const core::Vec3 offset{dx * metersPerPixel_, -dy * metersPerPixel_, depth};
    return headModel + core::Quat::axisAngle({0.0f, 1.0f, 0.0f}, -turnRadians_).rotate(offset);
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
    desc.title = "TinyDeskPet";
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

    if (!renderer_->initialize(window_->nativeHandle(), desc.size, rendererOptions())) {
        core::logging::error("렌더러를 초기화할 수 없습니다");
        return false;
    }

    refreshBounds();
    if (!restoreSavedPosition()) {
        resetCharacterPosition();
    }
    window_->show();
    if (!window_->showTrayIcon("TinyDeskPet")) {
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
    applyLoadedModel();
    updateFullscreenHiding();
    if (hidden_ || autoHidden_) {
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
    updateWalls(static_cast<float>(frameSeconds));
    for (int i = 0; i < steps; ++i) {
        character_.update(dt);
    }

    updateOverlay();
    updateAnimation(static_cast<float>(steps) * dt);  // 이번 프레임에 진행한 시뮬레이션 시간

    renderer::RenderScene scene = buildScene();
    if (ImDrawData* settings = updateSettings(static_cast<float>(frameSeconds))) {
        scene.ui = settings;
        scene.uiFrame = ++uiFrame_;  // 열려 있는 동안은 매 프레임 그림 (커서·위젯 반응)
    }
    const renderer::FrameResult frame = renderer_->render(scene);
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

    // 설정 창 위에서 누른 왼쪽 버튼은 떼기 전까지 설정 창으로만 (캐릭터를 끌거나 점프하지 않음)
    const auto pressOnSettings = [this](MouseButton button) {
        if (button == MouseButton::Left && settingsUi_.wantsMouse()) {
            uiPointer_ = true;
            settingsUi_.setMouseButton(true);
        }
        return uiPointer_;
    };
    std::visit(Overloaded{
                   [this, pressOnSettings](const core::PointerDownEvent& e) {
                       if (!pressOnSettings(e.button) && e.button == MouseButton::Left) {
                           pointerDown_ = true;
                           character_.onPointerDown(e.screen);
                       }
                   },
                   [this](const core::PointerMoveEvent& e) {
                       if (!uiPointer_) {
                           character_.onPointerMove(e.screen);
                       }
                   },
                   [this](const core::PointerUpEvent& e) { onPointerUp(e); },
                   [this, pressOnSettings](const core::DoubleClickEvent& e) {
                       // 두 번째 누름이 WM_LBUTTONDBLCLK로 오므로 설정 창에는 누름으로 넘김
                       if (!pressOnSettings(e.button) && e.button == MouseButton::Left) {
                           onDoubleClick();
                       }
                   },
                   [this](const core::MouseWheelEvent& e) { settingsUi_.addMouseWheel(e.notches); },
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

// 더블클릭 = 상호작용: 상호작용 클립이 있으면 제자리에서 한 번 재생, 없으면 점프 (기본 동작)
void Application::onDoubleClick() {
    const auto& clip = clips_[static_cast<std::size_t>(anim::Motion::Interact)];
    if (model_ && clip && clip->duration() > 0.0f) {
        (void)character_.interact(clip->duration());
    } else {
        (void)character_.jump();
    }
}

void Application::onPointerUp(const core::PointerUpEvent& event) {
    if (event.button == core::MouseButton::Left) {
        if (uiPointer_) {
            uiPointer_ = false;
            settingsUi_.setMouseButton(false);
            return;
        }
        pointerDown_ = false;
        // 다른 모니터로 옮겼을 수 있으므로 놓기 전에 바닥을 다시 계산
        refreshGround();
        character_.onPointerUp(event.screen);
    } else if (event.button == core::MouseButton::Right && !settingsUi_.wantsMouse()) {
        onContextMenu({roundToInt(event.screen.x), roundToInt(event.screen.y)});
    }
}

// ---------------------------------------------------------------------------
// 메뉴 (docs/03-detailed-design/app.md §4.4)
// ---------------------------------------------------------------------------

void Application::onContextMenu(core::PointI screen) {
    // 자주 쓰거나 급할 때 필요한 것만. 크기·모델 정보·모델 열기는 설정 창에 (ADR-0016)
    const std::vector<platform::MenuItem> items = {
        makeMenuItem(MenuCommand::Settings, "설정…"),
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
        case MenuCommand::ResetPosition: resetCharacterPosition(); break;
        case MenuCommand::Quit: requestQuit(); break;
        case MenuCommand::ToggleVisible: setVisible(hidden_); break;
        case MenuCommand::Settings: openSettings(); break;
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
    // 바닥을 먼저 정하고 옮김: 옮긴 뒤 바닥을 구하면 teleport가 옛 바닥에 발을 붙여 버려
    // 엉뚱한 위치(창 윗변 등) 기준으로 바닥을 찾게 됨. 공중에서 시작하므로 발 아래만 봄
    character_.setGround(groundAt(feet, 0.0f));
    character_.teleport(feet);
    updateOverlay();
    return true;
}

void Application::refreshGround() {
    // 서 있는 동안은 밟고 있는 창이 조금 올라가도 따라감. 떨어지는 중에는 발 아래 창만
    // (창 앞에 있다가 떨어졌을 때 그 창 위로 순간 이동하지 않게)
    constexpr float kStepUpPixels = 24.0f;
    const character::State state = character_.state();
    const bool standing = state == character::State::Idle || state == character::State::Walk ||
                          state == character::State::Interact;
    character_.setGround(
        groundAt(character_.position(), standing ? kStepUpPixels * dpiScale_ : 0.0f));
}

// feet 아래 바닥: 그 모니터의 작업 영역 바닥(걷기·던지기로 다른 모니터에 넘어갔을 수 있음)과
// 다른 창의 윗변 중 가장 높은 것
float Application::groundAt(core::Vec2 feet, float stepUp) {
    const auto floor =
        static_cast<float>(window_->workAreaAt({roundToInt(feet.x), roundToInt(feet.y)}).bottom);
    if (!config_.character.walkOnWindows) {
        return floor;
    }
    // 다른 창 위에 설 수 있음. 창 목록은 0.1초마다만 다시 읽음 (모든 창을 훑으므로)
    constexpr double kSurfaceInterval = 0.1;
    if (lastTime_ >= nextSurfaceCheck_) {
        surfaces_ = window_->windowSurfaces();
        nextSurfaceCheck_ = lastTime_ + kSurfaceInterval;
    }
    return groundBelow(feet, stepUp, floor, surfaces_);
}

void Application::refreshBounds() {
    const core::RectI target = targetWallRect();
    wallLeft_ = static_cast<float>(target.left);
    wallRight_ = static_cast<float>(target.right);
    applyWalls();
}

// 벽의 기준 = 지금 동작의 그려지는 폭. 모든 동작 중 가장 넓은 폭(걸을 때 돌린 몸, 팔을 벌리는
// 끌기 자세)으로 막으면 서 있을 때 그만큼 화면 끝에서 떠 보이고, 그 차이가 모델마다 다름
core::RectI Application::targetWallRect() const {
    if (!model_) {
        return visibleRect();
    }
    const auto motion = static_cast<std::size_t>(motionFor(character_.state()));
    core::RectI rect = projectedRect(poseBounds_.motions[motion]);  // 위아래
    const anim::ScreenRange& x = poseBounds_.screenX[motion];
    if (x.min <= x.max) {
        // 좌우는 실제 정점을 투영한 범위 (상자 꼭짓점이면 앞쪽 모서리가 원근으로 넓게 잡힘)
        const auto width = static_cast<float>(boxSize_.width);
        rect.left = static_cast<int>(std::floor((x.min + 1.0f) * 0.5f * width));
        rect.right = static_cast<int>(std::ceil((x.max + 1.0f) * 0.5f * width));
    }
    return rect;
}

core::RectI Application::wallRect() const {
    const core::RectI target = targetWallRect();
    return {roundToInt(wallLeft_), target.top, roundToInt(wallRight_), target.bottom};
}

// 폭이 바뀌면 자세 전환 시간쯤에 걸쳐 따라감 → 화면 끝에서 걷기·끌기로 바뀌면 캐릭터가 순간
// 이동하지 않고 안쪽으로 미끄러져 들어오고, 다시 좁아지면 벽에 붙어 있던 캐릭터는 화면 끝까지 따라
// 나감 (넓은 끌기 자세로 막혔다가 놓았을 때 틈이 남지 않게)
void Application::updateWalls(float dt) {
    const core::RectI target = targetWallRect();
    const float step =
        kWallSpeedBoxesPerSecond * static_cast<float>(boxSize_.width) * std::max(dt, 0.0f);
    const auto left = static_cast<float>(target.left);
    const auto right = static_cast<float>(target.right);
    wallLeft_ = core::moveTowards(wallLeft_, left, step);
    wallRight_ = core::moveTowards(wallRight_, right, step);
    applyWalls(true);
}

void Application::applyWalls(bool keepAtWall) {
    // 그려지는 영역이 가상 데스크톱 밖으로 나가지 않도록 발 x 범위를 정함 (FR-17).
    // 상자 반폭으로 막으면 모델 바깥의 투명한 여백 때문에 화면 끝 앞에서 멈춰 보이므로,
    // 상자의 투명한 부분은 화면 밖으로 나가도 됨. 발은 상자 가로 중앙
    const core::RectI desktop = window_->desktopBounds();
    const float center = static_cast<float>(boxSize_.width) / 2.0f;
    character_.setHorizontalBounds(static_cast<float>(desktop.left) + (center - wallLeft_),
                                   static_cast<float>(desktop.right) - (wallRight_ - center),
                                   keepAtWall);
}

void Application::applyDpiScale(float scale) {
    if (!(scale > 0.0f) || scale == dpiScale_) {
        return;
    }
    dpiScale_ = scale;
    resizeCharacter();
    settingsUi_.setScale(scale);
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
    applyVisibility();
}

void Application::applyVisibility() {
    if (!hidden_ && !autoHidden_) {
        window_->show();
    } else {
        window_->hide();
    }
}

// 항상 위 오버레이가 전체 화면 게임·영상을 가리지 않도록, 캐릭터가 있는 모니터에 전체 화면 앱이
// 떠 있는 동안 자동으로 숨김. 창 상태를 묻는 비용이 있어 0.5초마다만 확인 (lastTime_ 기준)
void Application::updateFullscreenHiding() {
    constexpr double kCheckSeconds = 0.5;
    if (lastTime_ < nextFullscreenCheck_) {
        return;
    }
    nextFullscreenCheck_ = lastTime_ + kCheckSeconds;
    const core::Vec2 feet = character_.position();
    const bool fullscreen =
        window_->fullscreenAppActive({roundToInt(feet.x), roundToInt(feet.y) - 1});
    if (fullscreen == autoHidden_) {
        return;
    }
    autoHidden_ = fullscreen;
    if (fullscreen) {
        core::logging::info("전체 화면 앱이 있어 숨깁니다");
    } else {
        core::logging::info("전체 화면 앱이 닫혀 다시 보입니다");
    }
    applyVisibility();
}

// 발이 다른 모니터로 넘어갔거나 작업 영역이 바뀌었을 때만 창을 옮김 (매 프레임 옮기지 않음).
// 다른 DPI의 모니터로 옮기면 OS가 WM_DPICHANGED를 보내 캐릭터 크기도 맞춰짐
// 캐릭터가 두 모니터에 걸치면 두 작업 영역을 합친 사각형으로 넓혀 양쪽 모두 그림.
// 몸 범위(visibleRect)의 왼쪽·오른쪽 아래와 위 가운데가 있는 모니터를 합침
void Application::updateOverlay() {
    const core::RectI box = characterRect();
    const core::RectI body = visibleRect();
    const int bottom = box.top + body.bottom - 1;
    core::RectI area = window_->workAreaAt({box.left + body.left, bottom});
    for (const core::PointI p :
         {core::PointI{box.left + body.right - 1, bottom},
          core::PointI{box.left + (body.left + body.right) / 2, box.top + body.top}}) {
        const core::RectI other = window_->workAreaAt(p);
        area = {std::min(area.left, other.left), std::min(area.top, other.top),
                std::max(area.right, other.right), std::max(area.bottom, other.bottom)};
    }
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
        return projectedRect(poseBounds_.all);
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
    // 끄는 중에는 커서가 투명한 곳으로 가도 계속 받음. 설정 창 위도 받음 (알파 읽기는 2~3프레임
    // 늦어서, 창이 막 열렸거나 움직인 직후에도 바로 클릭되게)
    if (pointerDown_ || uiPointer_ || settingsUi_.wantsMouse()) {
        window_->setClickThrough(false);
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

core::RectI Application::projectedRect(const model::Bounds& b) const {
    const auto width = static_cast<float>(boxSize_.width);
    const auto height = static_cast<float>(boxSize_.height);

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

    // 좌우·위는 상자로 자르지 않음: 크기는 서 있는 자세로 맞추므로 팔을 벌리거나 뛰어오르는 동작은
    // 상자 밖으로 나감. 벽·그리는 영역이 그 부분까지 담아야 화면 끝에서 넘거나 잘리지 않음.
    // 아래는 발(상자 바닥)까지: 그보다 낮은 자세는 바닥 아래라 보이지 않고, 모니터 판정은 발 기준
    return {roundToInt(left), roundToInt(top), roundToInt(right),
            std::min(roundToInt(bottom), boxSize_.height)};
}

// 3D가 그려질 영역 (오버레이 px): 지금 동작의 자세 범위(그 동작 경계 상자 투영) ∪ 지금 본 위치
// (흔들리는 머리카락, 동작 전환 중 자세 포함)의 투영 범위, 메시 두께·외곽선 여유를 더함.
// 렌더러가 3D·MSAA를 이 크기로만 그림 → 대기 중에는 상자 크기 정도라 화면 전체·고정 여유보다
// 훨씬 쌈 (ADR-0011). 모든 동작 상자를 쓰면 크게 팔을 드는 클립 하나 때문에 늘 넓게 그림
core::RectI Application::sceneRegionFor(const core::Mat4& viewProjection) const {
    const core::PointI offset = boxOffset();
    const core::RectI body = projectedRect(
        poseBounds_.motions[static_cast<std::size_t>(motionFor(character_.pose().state))]);
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

    if (!model_ && pendingModel_.valid()) {
        // 첫 모델을 읽는 중: 슬라임이 잠깐 보였다가 모델로 바뀌면 어색하므로 아무것도 그리지 않음
        body = {};
        body.visible = false;
    }
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
        // 카메라(+Z)를 모델 공간으로: 몸을 돌린 만큼 반대로 돌림 (림·MatCap용)
        scene.character.viewDirection =
            core::Quat::axisAngle({0.0f, 1.0f, 0.0f}, -turnRadians_).rotate({0.0f, 0.0f, 1.0f});
        scene.character.skinMatrices = animation_.skin;
        scene.character.expressionWeights = animation_.expressions;
        scene.character.morphWeights = animation_.morphWeights;
        scene.sceneRegion = sceneRegionFor(scene.character.viewProjection);
    }

    return scene;
}

}  // namespace deskpet::app
