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
    Jump = 2,
    ResetPosition = 3,
    Quit = 4,
    ToggleVisible = 5,  // 트레이에서 숨기기/보이기 (FR-18)
};

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
    void refitCamera();
    void setVisible(bool visible);
    void updateAnimation(float dt);
    void syncWindowToCharacter();

    [[nodiscard]] core::PointI windowTopLeftFor(core::Vec2 feet) const;
    [[nodiscard]] core::RectI hitRegion() const;
    [[nodiscard]] core::RectI modelHitRegion() const;
    [[nodiscard]] renderer::RenderScene buildScene() const;

    core::AppConfig config_;
    float dpiScale_ = 1.0f;
    core::SizeI windowSize_;  // 설정 크기 × DPI 배율 (물리 px)
    bool hidden_ = false;
    std::shared_ptr<const model::Model> model_;
    std::optional<anim::ProceduralAnimator> animator_;
    anim::AnimationOutput animation_;  // 매 프레임 재사용
    model::Bounds displayBounds_;  // 카메라·클릭 영역 기준 (애니메이션 자세 포함)
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
