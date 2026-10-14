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
#include "ui/SettingsUi.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace deskpet::app {

enum class MenuCommand : std::uint8_t {
    // 1, 2, 6~11: 예전 버전·점프·크기·모델 정보·모델 열기 (설정 창으로 옮겨 메뉴에서 제거)
    ResetPosition = 3,
    Quit = 4,
    ToggleVisible = 5,  // 트레이에서 숨기기/보이기 (FR-18)
    Settings = 12,      // 설정… (ImGui 설정 창, ADR-0016)
};

// 캐릭터 크기 조절 범위 (%, FR-06)
inline constexpr int kMinScalePercent = 10;
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

    // 동작별 모션 클립 (ADR-0013). 비어 있는 동작은 코드로 만든 동작. setModel 앞뒤 어디서나
    using MotionClips = std::array<std::shared_ptr<const model::MotionClip>, anim::kMotionCount>;
    void setMotionClips(MotionClips clips);

    // 자세 경계 상자 (모델 공간 m). 클립이 바뀌면 다시 계산 (standing은 클립과 무관)
    struct PoseBounds {
        model::Bounds all;       // 모든 동작: 벽·깊이 범위
        model::Bounds standing;  // 서 있는 자세: 화면 크기·바닥 기준
        std::array<model::Bounds, anim::kMotionCount> motions{};  // 동작별: 그리기 영역
        // 동작별 실제 정점의 화면 x 범위 (NDC, 이 상자들로 맞춘 카메라 기준): 벽
        std::array<anim::ScreenRange, anim::kMotionCount> screenX{};
    };

    // 모델 파일을 읽는 함수 (실패하면 nullptr). 작업 스레드에서 불리므로 스레드 안전해야 함
    using ModelLoader =
        std::function<std::shared_ptr<const model::Model>(const std::filesystem::path&)>;
    void setModelLoader(ModelLoader loader);
    // 작업 스레드에서 읽고(자세 경계 상자까지), 끝나면 다음 프레임에 교체. 실패하면 지금 모델 유지.
    // 첫 모델을 읽는 동안은 아무것도 그리지 않음 (슬라임이 잠깐 보였다 바뀌지 않게)
    void loadModelAsync(std::filesystem::path path);
    void waitForModelLoad();  // 읽는 중이면 끝날 때까지 기다림
    // 마지막으로 읽기에 성공한 모델 파일 (setModel로 받은 모델이면 비어 있음)
    [[nodiscard]] const std::filesystem::path& modelPath() const noexcept { return modelPath_; }

    // 설정 창에서 고른 모션 클립을 읽는 함수 (configured = 설정 값 "파일#이름", 실패하면 nullptr).
    // 메인 스레드에서 불림 (ADR-0016)
    using ClipLoader = std::function<std::shared_ptr<const model::MotionClip>(
        anim::Motion motion, const std::string& configured, float loopStart)>;
    void setClipLoader(ClipLoader loader);
    // 설정 창에서 바꾼 값을 deskpet.ini에 바로 쓰는 함수
    using SettingsWriter = std::function<void(const std::vector<core::IniValue>& values)>;
    void setSettingsWriter(SettingsWriter writer);
    // 설정 창 글꼴(TrueType 파일)과 테마. run() 전에
    void setUiFont(const std::filesystem::path& file) { settingsUi_.setFont(file); }
    void setUiDarkTheme(bool dark) { settingsUi_.setDarkTheme(dark); }
    void openSettings();
    [[nodiscard]] bool settingsOpen() const noexcept { return settingsUi_.isOpen(); }
    // 테스트·디버그용: 마지막 프레임의 설정 창 (화면 좌표)
    [[nodiscard]] core::RectI settingsRect() const;
    // 테스트용: 설정 창에서 사용자가 한 일을 그대로 처리
    void applySettingsAction(const ui::SettingsAction& action);

    [[nodiscard]] const character::CharacterController& character() const noexcept {
        return character_;
    }

    // 현재 캐릭터 크기 (%). 종료할 때 [state] scale로 저장
    [[nodiscard]] int scalePercent() const noexcept { return scalePercent_; }

    // 캐릭터가 그려질 수 있는 영역 (상자 왼쪽 위 기준 px). 벽(FR-17) 계산에 씀.
    // 슬라임은 몸 타원을 감싸는 사각형, 모델은 모든 자세의 경계 상자를 투영한 사각형 (크기는 서
    // 있는 자세로 맞추므로 큰 동작은 상자 밖으로 나가 음수이거나 상자보다 클 수 있음. 아래는
    // 발까지)
    [[nodiscard]] core::RectI visibleRect() const;
    // 벽(FR-17) 계산에 쓰는 지금 동작의 그려지는 영역 (상자 왼쪽 위 기준 px). 서 있을 때는 서 있는
    // 자세 폭이라 화면 끝에 몸이 닿고, 넓은 동작으로 바뀌면 전환 시간 동안 천천히 넓어짐
    [[nodiscard]] core::RectI wallRect() const;

    // 캐릭터 상자 (화면 좌표). 발 = 상자 아래 가운데. 크기 = 설정 × DPI × 사용자 배율
    [[nodiscard]] core::RectI characterRect() const;

private:
    [[nodiscard]] bool initialize();
    void shutdown();
    [[nodiscard]] bool tick();  // 한 프레임. false면 루프 종료

    void handleEvent(const core::Event& event);
    void onDoubleClick();
    void onPointerUp(const core::PointerUpEvent& event);
    void onContextMenu(core::PointI screen);
    void executeMenuCommand(int commandId);

    void resetCharacterPosition();
    [[nodiscard]] bool restoreSavedPosition();
    void refreshGround();
    [[nodiscard]] float groundAt(core::Vec2 feet, float stepUp);  // 창 목록을 갱신할 수 있음
    void refreshBounds();  // 벽을 지금 동작 폭으로 바로 맞춤 (모델·크기·작업 영역이 바뀐 뒤)
    void updateWalls(float dt);  // 동작이 바뀌어 폭이 넓어지면 천천히 따라감 (매 프레임)
    [[nodiscard]] core::RectI targetWallRect() const;
    void applyWalls(bool keepAtWall = false);
    void applyDpiScale(float scale);
    void setScalePercent(int percent);
    void resizeCharacter();  // boxSize_를 targetBoxSize()로 바꾸고 카메라·벽을 갱신
    [[nodiscard]] core::SizeI targetBoxSize() const;
    void applyAnimation();    // 클립 연결, 자세 경계 상자·카메라·벽 다시 계산
    void applyLoadedModel();  // 작업 스레드가 끝났으면 결과로 교체
    void openModelFile();     // 메뉴 "모델 열기…"
    [[nodiscard]] std::optional<core::Vec3> lookTarget() const;  // 커서 → 모델 공간 목표
    void refitCamera();
    void setVisible(bool visible);
    void applyVisibility();  // 사용자 숨김·자동 숨김을 합쳐 창을 보이거나 숨김
    void updateFullscreenHiding();  // 전체 화면 앱이 있으면 자동 숨김 (0.5초마다 확인)
    void updateAnimation(float dt);
    void updateOverlay();  // 발이 있는 모니터의 작업 영역으로 오버레이 창을 맞춤 (바뀔 때만)

    [[nodiscard]] core::PointI boxOffset() const;  // 상자 좌상단 (오버레이 내부 px)
    [[nodiscard]] core::Mat4 boxToOverlay() const;  // 상자 기준 클립 좌표 → 오버레이 클립 좌표
    // 모델 공간 경계 상자 8개 꼭짓점을 투영한 사각형 (상자 왼쪽 위 기준 px, 아래는 발까지)
    [[nodiscard]] core::RectI projectedRect(const model::Bounds& bounds) const;
    void updateClickThrough();
    [[nodiscard]] core::RectI sceneRegionFor(const core::Mat4& viewProjection) const;
    [[nodiscard]] renderer::RenderScene buildScene() const;

    // 설정 창 (ApplicationSettings.cpp)
    [[nodiscard]] ImDrawData* updateSettings(float dt);  // 닫혀 있으면 nullptr
    [[nodiscard]] ui::SettingsState settingsState() const;
    [[nodiscard]] renderer::RendererOptions rendererOptions() const;  // config_.renderer 그대로
    void applyMovement();  // config_.character → 캐릭터 물리·걷기 (실행 중 변경)
    void refreshClipChoices();  // 모델 파일 안 애니메이션 목록 (모델이 바뀌면)
    void refreshAnimationAsync();  // 클립이 바뀜 → 작업 스레드에서 자세 경계 상자 다시 계산
    void writeSettings(const std::vector<core::IniValue>& values) const;

    // 설정 창이 ImGui 컨텍스트를 소유하고 렌더러 백엔드가 그 컨텍스트를 씀 → 렌더러보다 먼저
    // 선언해 나중에 파괴되게 함
    ui::SettingsUi settingsUi_;
    bool uiPointer_ = false;  // 설정 창 위에서 누른 버튼을 떼기 전까지 캐릭터에 넘기지 않음
    std::uint64_t uiFrame_ = 0;  // 열려 있는 동안 매 프레임 올려 Present를 생략하지 않게 함
    ClipLoader clipLoader_;
    SettingsWriter settingsWriter_;
    std::vector<ui::ClipChoice> clipChoices_;  // modelPath_ 파일 안 애니메이션 (+ 기본 동작)
    std::filesystem::path clipChoicesPath_;  // clipChoices_를 만든 모델 파일

    core::AppConfig config_;
    float dpiScale_ = 1.0f;
    int scalePercent_ = 100;  // 사용자 크기 배율 (%)
    // 캐릭터 상자: 설정 크기 × DPI 배율 × 사용자 배율 (물리 px). 카메라는 이 상자에 모델을 맞춤.
    // 예전에는 창 크기였지만 이제 창은 화면 전체(오버레이)이고 상자는 창 안에서 발을 따라 움직임
    core::SizeI boxSize_;
    core::RectI overlay_;  // 오버레이 창 = 발이 있는 모니터의 작업 영역 (화면 좌표, ADR-0011)
    bool hidden_ = false;      // 사용자가 메뉴로 숨김
    bool autoHidden_ = false;  // 전체 화면 앱 때문에 자동으로 숨김
    double nextFullscreenCheck_ = 0.0;
    std::vector<core::RectI> surfaces_;  // 다른 창들 (발판 후보, 0.1초마다 갱신)
    double nextSurfaceCheck_ = 0.0;
    bool pointerDown_ = false;  // 왼쪽 버튼을 누르고 있는 동안은 항상 클릭을 받음 (끌기)
    std::shared_ptr<const model::Model> model_;
    std::optional<anim::ProceduralAnimator> animator_;
    MotionClips clips_;

    // 작업 스레드 로딩 결과: 모델과, 같은 클립으로 미리 계산한 자세 경계 상자
    struct LoadedModel {
        std::filesystem::path path;
        std::shared_ptr<const model::Model> model;
        PoseBounds bounds;
    };
    ModelLoader modelLoader_;
    std::future<LoadedModel> pendingModel_;
    std::filesystem::path modelPath_;
    anim::AnimationOutput animation_;  // 매 프레임 재사용
    PoseBounds poseBounds_;
    float metersPerPixel_ = 0.0f;  // 발 평면(z = 0)에서 창 1px = 모델 공간 몇 m (흔들림 관성용)
    // 벽 계산에 쓰는 그려지는 영역의 왼쪽·오른쪽 (상자 왼쪽 기준 px). 지금 동작 폭을 따라감
    float wallLeft_ = 0.0f;
    float wallRight_ = 0.0f;
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
