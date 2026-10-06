#pragma once

// 모든 모듈을 연결하고 메인 루프를 돌리는 애플리케이션 (플랫폼 독립).
// IWindow / IRenderer를 주입받으므로 테스트에서 가짜 구현으로 바꿀 수 있습니다 (ADR-0002).
// 명세: docs/03-detailed-design/app.md

#include "anim/ProceduralAnimator.h"
#include "character/CharacterController.h"
#include "core/Clock.h"
#include "core/Config.h"
#include "core/Events.h"
#include "core/FixedTimestep.h"
#include "platform/IWindow.h"
#include "renderer/IRenderer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace deskpet::app {

enum class MenuCommand : std::uint8_t {
    About = 1,
    // 2: 예전 "점프" (메뉴에서 제거, 더블클릭으로만 점프)
    ResetPosition = 3,
    Quit = 4,
    ToggleVisible = 5,  // 트레이에서 숨기기/보이기 (FR-18)
    ScaleInfo = 6,      // "크기 100%" (정보 표시용, 비활성)
    ScaleUp = 7,        // 크게 (FR-06)
    ScaleDown = 8,      // 작게
};

// 캐릭터 크기 조절 범위 (%, FR-06)
inline constexpr int kMinScalePercent = 50;
inline constexpr int kMaxScalePercent = 200;
inline constexpr int kScaleStepPercent = 10;

enum class ExitCode : std::uint8_t {
    Ok = 0,
    InitializationFailed = 1,
    RendererFailed = 2,
    UnhandledException = 3,  // main_win32.cpp에서만 사용
};

class Application {
public:
    Application(core::AppConfig config, std::unique_ptr<platform::IWindow> window,
                std::unique_ptr<renderer::IRenderer> renderer,
                core::TimeSource timeSource = core::makeSteadyTimeSource());
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // 초기화 → 메인 루프 → 정리. ExitCode 값을 반환합니다.
    [[nodiscard]] int run();
    void requestQuit() noexcept { quitRequested_ = true; }

    // run() 전에 호출. 모델이 있으면 슬라임 대신 그립니다 (ADR-0008).
    void setModel(std::shared_ptr<const model::Model> model);

    [[nodiscard]] const character::CharacterController& character() const noexcept {
        return character_;
    }

    // 현재 캐릭터 크기 (%). 종료할 때 [state] scale로 저장
    [[nodiscard]] int scalePercent() const noexcept { return scalePercent_; }

    // 캐릭터 상자 안에서 캐릭터가 그려질 수 있는 영역 (상자 내부 px). 벽(FR-17) 계산에 씀.
    // 슬라임은 몸 타원을 감싸는 사각형, 모델은 모든 자세의 경계 상자를 투영한 사각형
    [[nodiscard]] core::RectI visibleRect() const;

    // 캐릭터 상자 (화면 좌표). 발 = 상자 아래 가운데. 크기 = 설정 × DPI × 사용자 배율
    [[nodiscard]] core::RectI characterRect() const;

private:
    [[nodiscard]] bool initialize();
    void shutdown();
    [[nodiscard]] bool tick();  // 한 프레임. false면 루프 종료

    void handleEvent(const core::Event& event);
    void onContextMenu(core::PointI screen);
    void executeMenuCommand(int commandId);

    void resetCharacterPosition();
    [[nodiscard]] bool restoreSavedPosition();
    void refreshGround();
    void refreshBounds();
    void applyDpiScale(float scale);
    void setScalePercent(int percent);
    void resizeCharacter();  // boxSize_를 targetBoxSize()로 바꾸고 카메라·벽을 갱신
    [[nodiscard]] core::SizeI targetBoxSize() const;
    void refitCamera();
    void setVisible(bool visible);
    void updateAnimation(float dt);
    void updateOverlay();  // 발이 있는 모니터의 작업 영역으로 오버레이 창을 맞춤 (바뀔 때만)

    [[nodiscard]] core::PointI boxOffset() const;  // 상자 좌상단 (오버레이 내부 px)
    [[nodiscard]] core::Mat4 boxToOverlay() const;  // 상자 기준 클립 좌표 → 오버레이 클립 좌표
    [[nodiscard]] core::RectI modelVisibleRect() const;
    void updateClickThrough();
    [[nodiscard]] renderer::RenderScene buildScene() const;

    core::AppConfig config_;
    float dpiScale_ = 1.0f;
    int scalePercent_ = 100;  // 사용자 크기 배율 (%)
    // 캐릭터 상자: 설정 크기 × DPI 배율 × 사용자 배율 (물리 px). 카메라는 이 상자에 모델을 맞춤.
    // 예전에는 창 크기였지만 이제 창은 화면 전체(오버레이)이고 상자는 창 안에서 발을 따라 움직임
    core::SizeI boxSize_;
    core::RectI overlay_;  // 오버레이 창 = 발이 있는 모니터의 작업 영역 (화면 좌표, ADR-0011)
    bool hidden_ = false;
    bool pointerDown_ = false;  // 왼쪽 버튼을 누르고 있는 동안은 항상 클릭을 받음 (끌기)
    std::shared_ptr<const model::Model> model_;
    std::optional<anim::ProceduralAnimator> animator_;
    anim::AnimationOutput animation_;  // 매 프레임 재사용
    model::Bounds displayBounds_;  // 카메라·클릭 영역 기준 (애니메이션 자세 포함)
    float metersPerPixel_ = 0.0f;  // 발 평면(z = 0)에서 창 1px = 모델 공간 몇 m (흔들림 관성용)
    core::Vec2 lastFeet_;  // 직전에 흔들림에 넘긴 발 위치 (화면 px)
    bool hasLastFeet_ = false;
    float turnRadians_ = 0.0f;  // 걷는 방향으로 돌린 몸 각도 (목표 각도로 서서히 돎)
    core::Mat4 camera_ = core::Mat4::identity();  // model_의 뷰×투영
    std::unique_ptr<platform::IWindow> window_;
    std::unique_ptr<renderer::IRenderer> renderer_;
    core::TimeSource now_;

    character::CharacterController character_;
    core::FixedTimestep timestep_;
    std::vector<core::Event> events_;  // 프레임마다 재사용 (할당 방지)

    double lastTime_ = 0.0;
    bool quitRequested_ = false;
    ExitCode exitCode_ = ExitCode::Ok;
};

}  // namespace deskpet::app
