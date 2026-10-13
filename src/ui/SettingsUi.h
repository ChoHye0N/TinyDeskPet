#pragma once

// 설정 창 (Dear ImGui, 플랫폼 독립). 명세: docs/03-detailed-design/ui.md, ADR-0016
//
// 앱이 매 프레임 마우스 상태를 넣고 frame()을 부르면, 창이 열려 있을 때 그릴 데이터(ImDrawData)와
// 사용자가 한 일(SettingsAction)을 돌려줍니다. 앱 상태는 SettingsState로 받아 보여 주기만 하고,
// 바꾸는 것은 앱이 SettingsAction을 처리해서 합니다 (상태를 두 곳에서 맞출 일이 없음).
// 입력은 마우스만 받습니다: 오버레이 창은 키보드 포커스를 받지 않음 (클릭해도 다른 창의 포커스를
// 빼앗지 않게)

#include "core/Math.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct ImDrawData;
struct ImGuiContext;

namespace deskpet::ui {

// 동작 클립 고르기 항목. value가 비면 코드로 만든 기본 동작
struct ClipChoice {
    std::string label;
    std::string value;  // 설정 [animation] *_clip에 쓸 값 ("파일#클립 이름")
};

struct MotionSetting {
    std::string name;     // "대기" 등
    std::string current;  // 지금 값 (choices의 value 중 하나)
    std::vector<ClipChoice> choices;
};

inline constexpr std::size_t kMotionSettingCount = 4;  // 대기, 걷기, 끌려 다닐 때, 공중

// 설정 창이 보여 줄 앱 상태 (매 프레임 앱이 채움)
struct SettingsState {
    std::string version;
    // 모델
    bool modelLoading = false;
    std::string modelName;  // 비면 "모델 없음"
    std::string modelAuthor;
    std::string modelLicense;
    std::string modelFile;  // 파일 이름 (UTF-8)
    // 동작
    std::array<MotionSetting, kMotionSettingCount> motions;
    // 화면
    int scalePercent = 100;
    int minScalePercent = 50;
    int maxScalePercent = 200;
    int scaleStepPercent = 10;
    bool lookAt = true;
    bool walkOnWindows = true;
    bool idleMotion = true;
};

struct SettingsAction {
    enum class Kind : std::uint8_t {
        OpenModel,         // 모델 열기 대화상자
        SetClip,           // motion 동작의 클립 = text
        SetScale,          // 크기 = number (%)
        SetLookAt,         // flag
        SetWalkOnWindows,  // flag
        SetIdleMotion,     // flag
    };
    Kind kind = Kind::OpenModel;
    std::size_t motion = 0;
    std::string text;
    int number = 0;
    bool flag = false;
};

enum class SettingsTab : std::uint8_t { Model, Motion, Display, About };

class SettingsUi {
public:
    SettingsUi();
    ~SettingsUi();

    SettingsUi(const SettingsUi&) = delete;
    SettingsUi& operator=(const SettingsUi&) = delete;
    SettingsUi(SettingsUi&&) = delete;
    SettingsUi& operator=(SettingsUi&&) = delete;

    // 글꼴 (TrueType 파일, 예: 맑은 고딕). 비거나 못 읽으면 ImGui 내장 글꼴(영문만). 첫 frame 전에
    void setFont(const std::filesystem::path& file);
    void setDarkTheme(bool dark);
    // DPI 배율 (1.0 = 96 DPI): 글자·여백·모서리를 함께 키움
    void setScale(float scale);

    // avoid: 가리지 않을 영역 = 캐릭터가 그려지는 곳 (오버레이 내부 px). 그 왼쪽에, 자리가 없으면
    // 오른쪽에 띄우고 화면 안에 들어오도록 옮김
    void open(core::RectI avoid);
    void close() noexcept { open_ = false; }
    [[nodiscard]] bool isOpen() const noexcept { return open_; }

    // 마우스 (오버레이 내부 px). 매 프레임 frame() 전에
    void setMousePosition(core::Vec2 position);
    void setMouseButton(bool leftDown);
    void addMouseWheel(float notches);  // 위로 굴리면 +

    // 커서가 설정 창 위에 있거나 창 위젯을 누르고 있음 (직전 프레임 기준). true면 앱은 그 마우스
    // 입력을 캐릭터에 넘기지 않음
    [[nodiscard]] bool wantsMouse() const noexcept { return wantsMouse_; }

    // 한 프레임을 만듦. 닫혀 있으면 nullptr (그릴 것 없음). actions 뒤에 사용자가 한 일을 추가.
    // 반환 포인터는 다음 frame 호출 전까지 유효 (렌더러 백엔드가 그리면서 텍스처 상태를 고치므로
    // const가 아님)
    [[nodiscard]] ImDrawData* frame(float dt, core::SizeI display, const SettingsState& state,
                                    std::vector<SettingsAction>& actions);

    // 테스트·디버그용: 마지막 프레임의 창 위치·크기 (오버레이 내부 px)
    [[nodiscard]] core::RectI windowRect() const noexcept { return windowRect_; }
    [[nodiscard]] SettingsTab tab() const noexcept { return tab_; }
    void selectTab(SettingsTab tab) noexcept { tab_ = tab; }

private:
    void applyStyle();
    void drawTitle(bool& keepOpen) const;
    void drawTabs();
    void drawModelTab(const SettingsState& state, std::vector<SettingsAction>& actions) const;
    void drawMotionTab(const SettingsState& state, std::vector<SettingsAction>& actions) const;
    void drawDisplayTab(const SettingsState& state, std::vector<SettingsAction>& actions) const;
    void drawAboutTab(const SettingsState& state);

    ImGuiContext* context_ = nullptr;
    bool open_ = false;
    bool placeWindow_ = false;  // 다음 프레임에 창 위치를 정함 (열 때마다)
    core::RectI avoid_;
    bool dark_ = false;
    float scale_ = 1.0f;
    bool styleDirty_ = true;
    bool wantsMouse_ = false;
    core::RectI windowRect_;
    SettingsTab tab_ = SettingsTab::Motion;
};

}  // namespace deskpet::ui
