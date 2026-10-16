#include "ui/SettingsUi.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <imgui.h>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace deskpet::ui {
namespace {

// 크기는 96 DPI 기준 px (scale_을 곱해 씀)
constexpr float kWindowWidth = 540.0f;
constexpr float kWindowHeight = 360.0f;
constexpr float kTitleHeight = 44.0f;
constexpr float kTabWidth = 120.0f;
constexpr float kTabHeight = 34.0f;
constexpr float kLabelWidth = 108.0f;  // 내용 탭의 "이름 | 값" 이름 열
constexpr float kIconSize = 16.0f;
constexpr float kPetGap = 24.0f;       // 캐릭터와 창 사이
constexpr float kScreenMargin = 8.0f;  // 화면 가장자리와 창 사이

struct TabInfo {
    SettingsTab tab;
    Icon icon;
    const char* label;
};
constexpr std::array<TabInfo, 6> kTabs = {{
    {SettingsTab::Model, Icon::User, "모델"},
    {SettingsTab::Motion, Icon::Play, "동작"},
    {SettingsTab::Display, Icon::Monitor, "화면"},
    {SettingsTab::Graphics, Icon::Image, "그래픽"},
    {SettingsTab::Advanced, Icon::Sliders, "고급"},
    {SettingsTab::About, Icon::Info, "정보"},
}};

constexpr float kValueWidth = 56.0f;  // 슬라이더 오른쪽 값 글자 칸

ImVec2 offset(ImVec2 a, float x, float y) {
    return {a.x + x, a.y + y};
}

// 왼쪽에 이름(보조 글자색), 그 뒤 같은 줄 kLabelWidth 위치에서 값이 시작
void rowLabel(const char* label, float scale) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kLabelWidth * scale);
}

void infoRow(const char* label, const std::string& value, float scale) {
    if (value.empty()) {
        return;
    }
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(kLabelWidth * scale);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(value.c_str());
    ImGui::PopTextWrapPos();
}

// 켜기/끄기 스위치 (ImGui 체크박스 대신). 눌리면 true
bool toggle(const char* id, bool on, const Palette& p, float scale) {
    const ImVec2 size{36.0f * scale, 20.0f * scale};
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, size);
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float radius = size.y * 0.5f;
    list.AddRectFilled(pos, offset(pos, size.x, size.y), on ? p.accent : p.border, radius);
    const float knobX = on ? pos.x + size.x - radius : pos.x + radius;
    list.AddCircleFilled({knobX, pos.y + radius}, radius - 2.0f * scale, p.knob);
    return clicked;
}

// 얇은 트랙 + 둥근 손잡이 슬라이더 (step 단위로 맞춤). 값이 바뀌면 새 값
std::optional<float> sliderValue(const char* id, float value, float min, float max, float step,
                                 float width, const Palette& p, float scale) {
    const float height = 22.0f * scale;
    const float knob = 8.0f * scale;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, {width, height});
    const float left = pos.x + knob;
    const float right = pos.x + width - knob;
    std::optional<float> changed;
    if (ImGui::IsItemActive() && right > left && max > min && step > 0.0f) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - left) / (right - left), 0.0f, 1.0f);
        const float steps = std::round(t * (max - min) / step);
        const float picked = std::clamp(min + steps * step, min, max);
        if (std::abs(picked - value) > step * 0.01f) {
            changed = picked;
            value = picked;
        }
    }
    const float t = max > min ? std::clamp((value - min) / (max - min), 0.0f, 1.0f) : 0.0f;
    const float x = left + t * (right - left);
    const float y = pos.y + height * 0.5f;
    const float track = 2.0f * scale;
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled({left, y - track}, {right, y + track}, p.border, track);
    list.AddRectFilled({left, y - track}, {x, y + track}, p.accent, track);
    list.AddCircleFilled({x, y}, knob, p.accent);
    return changed;
}

std::optional<int> slider(const char* id, int value, int min, int max, int step, float width,
                          const Palette& p, float scale) {
    if (const std::optional<float> picked =
            sliderValue(id, static_cast<float>(value), static_cast<float>(min),
                        static_cast<float>(max), static_cast<float>(step), width, p, scale)) {
        return static_cast<int>(std::lround(*picked));
    }
    return std::nullopt;
}

// "이름 | 슬라이더 값" 한 줄. 값이 바뀌면 새 값
std::optional<int> sliderRow(const char* label, const char* id, int value, int min, int max,
                             int step, const Palette& p, float scale, const char* unit = "") {
    rowLabel(label, scale);
    const float sliderWidth = ImGui::GetContentRegionAvail().x - kValueWidth * scale;
    const std::optional<int> changed = slider(id, value, min, max, step, sliderWidth, p, scale);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d%s", changed.value_or(value), unit);
    return changed;
}

// 설정 바로 아래의 짧은 설명 (보조 글자색, 줄바꿈). 다음 설정과 띄움
void hint(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
    ImGui::Spacing();
    ImGui::Spacing();
}

// 이름 왼쪽, 스위치 오른쪽 끝. 눌리면 true
bool toggleRow(const char* label, const char* id, bool on, const Palette& p, float scale) {
    const float rowRight = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(rowRight - 36.0f * scale);
    return toggle(id, on, p, scale);
}

// 붙어 있는 선택 버튼들 (하나만 선택). 다른 것을 누르면 그 번호
template <std::size_t N>
std::optional<std::size_t> segmented(const char* id, const std::array<const char*, N>& labels,
                                     std::size_t selected, const Palette& p, float scale) {
    std::optional<std::size_t> picked;
    const float width = ImGui::GetContentRegionAvail().x / static_cast<float>(N);
    const float height = ImGui::GetFrameHeight();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.AddRectFilled(start, offset(start, width * static_cast<float>(N), height), p.frame,
                       8.0f * scale);
    ImGui::PushID(id);
    for (std::size_t i = 0; i < N; ++i) {
        const ImVec2 pos = offset(start, width * static_cast<float>(i), 0.0f);
        ImGui::SetCursorScreenPos(pos);
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::InvisibleButton("##seg", {width, height}) && i != selected) {
            picked = i;
        }
        ImGui::PopID();
        const bool on = i == selected;
        if (on || ImGui::IsItemHovered()) {
            const float inset = 3.0f * scale;
            list.AddRectFilled(offset(pos, inset, inset),
                               offset(pos, width - inset, height - inset),
                               on ? p.accentSoft : p.frameHover, 6.0f * scale);
        }
        const ImVec2 text = ImGui::CalcTextSize(labels[i]);
        list.AddText(offset(pos, (width - text.x) * 0.5f, (height - text.y) * 0.5f),
                     on ? p.accentText : p.text, labels[i]);
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(offset(start, 0.0f, height));
    ImGui::Dummy({0.0f, 0.0f});
    return picked;
}

// 왼쪽에 아이콘이 있는 버튼
bool iconButton(const char* label, Icon icon, const Palette& p, float scale) {
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const float iconSpace = (kIconSize + 8.0f) * scale;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {padding.x + iconSpace, padding.y});
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleVar();
    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    drawIcon(*ImGui::GetWindowDrawList(), icon,
             {itemMin.x + padding.x + kIconSize * scale * 0.5f, (itemMin.y + itemMax.y) * 0.5f},
             kIconSize * scale, p.text);
    return clicked;
}

}  // namespace

SettingsUi::SettingsUi() : context_(ImGui::CreateContext()) {
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 창 위치를 imgui.ini에 저장하지 않음 (열 때마다 캐릭터 옆에)
    io.LogFilename = nullptr;
    // 렌더러 백엔드가 텍스처(글꼴 아틀라스)를 만들고 고친다고 표시. 실제 백엔드(D3D11)는 초기화할
    // 때 같은 값을 다시 씀. 백엔드가 없으면(테스트) 텍스처 요청이 처리되지 않고 남을 뿐
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
}

SettingsUi::~SettingsUi() {
    ImGui::DestroyContext(context_);
}

void SettingsUi::setFont(const std::filesystem::path& file) {
    std::error_code ec;
    if (file.empty() || !std::filesystem::is_regular_file(file, ec)) {
        return;  // ImGui는 없는 파일에서 단언(assert)하므로 먼저 확인
    }
    ImGui::SetCurrentContext(context_);
    const std::u8string path = file.u8string();  // ImGui는 UTF-8 경로를 받음
    ImGui::GetIO().Fonts->AddFontFromFileTTF(std::string(path.begin(), path.end()).c_str(),
                                             kFontSize);
}

void SettingsUi::setDarkTheme(bool dark) {
    dark_ = dark;
    styleDirty_ = true;
}

void SettingsUi::setScale(float scale) {
    scale_ = scale > 0.0f ? scale : 1.0f;
    styleDirty_ = true;
}

void SettingsUi::open(core::RectI avoid) {
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();  // 닫혀 있던 동안의 입력은 버림
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    open_ = true;
    placeWindow_ = true;
    avoid_ = avoid;
}

void SettingsUi::setMousePosition(core::Vec2 position) {
    if (open_) {
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().AddMousePosEvent(position.x, position.y);
    }
}

void SettingsUi::setMouseButton(bool leftDown) {
    if (open_) {
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, leftDown);
    }
}

void SettingsUi::addMouseWheel(float notches) {
    if (open_) {
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().AddMouseWheelEvent(0.0f, notches);
    }
}

void SettingsUi::applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    applyTheme(style, dark_);
    style.ScaleAllSizes(scale_);
    style.FontScaleDpi = scale_;
    styleDirty_ = false;
}

ImDrawData* SettingsUi::frame(float dt, core::SizeI display, const SettingsState& state,
                              std::vector<SettingsAction>& actions) {
    if (!open_ || display.width <= 0 || display.height <= 0) {
        wantsMouse_ = false;
        return nullptr;
    }
    ImGui::SetCurrentContext(context_);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(display.width), static_cast<float>(display.height)};
    io.DeltaTime = dt > 0.0f ? dt : 1.0f / 60.0f;
    if (styleDirty_) {
        applyStyle();
    }
    ImGui::NewFrame();

    const ImVec2 size{kWindowWidth * scale_, kWindowHeight * scale_};
    if (placeWindow_) {
        // 캐릭터 왼쪽에, 자리가 없으면 오른쪽에. 세로는 캐릭터 가운데. 화면 밖으로 나가지 않게
        const float margin = kScreenMargin * scale_;
        const float gap = kPetGap * scale_;
        float x = static_cast<float>(avoid_.left) - gap - size.x;
        if (x < margin) {
            x = static_cast<float>(avoid_.right) + gap;
        }
        x = std::clamp(x, margin, std::max(margin, io.DisplaySize.x - size.x - margin));
        const float y =
            std::clamp(static_cast<float>(avoid_.top + avoid_.bottom) * 0.5f - size.y * 0.5f,
                       margin, std::max(margin, io.DisplaySize.y - size.y - margin));
        ImGui::SetNextWindowPos({x, y}, ImGuiCond_Always);
        placeWindow_ = false;
    }
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);

    bool keepOpen = true;
    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    const bool visible = ImGui::Begin("##settings", nullptr, kFlags);
    ImGui::PopStyleVar();
    if (visible) {
        drawTitle(keepOpen);
        const float bodyHeight = ImGui::GetWindowHeight() - kTitleHeight * scale_;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8.0f * scale_, 10.0f * scale_});
        ImGui::BeginChild("##tabs", {kTabWidth * scale_, bodyHeight},
                          ImGuiChildFlags_AlwaysUseWindowPadding);
        drawTabs();
        ImGui::EndChild();
        ImGui::PopStyleVar();

        const ImVec2 origin = ImGui::GetWindowPos();
        ImGui::GetWindowDrawList()->AddLine(
            offset(origin, kTabWidth * scale_, kTitleHeight * scale_),
            offset(origin, kTabWidth * scale_, ImGui::GetWindowHeight()), palette(dark_).border,
            scale_);

        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {18.0f * scale_, 16.0f * scale_});
        ImGui::BeginChild("##content", {0.0f, bodyHeight}, ImGuiChildFlags_AlwaysUseWindowPadding);
        switch (tab_) {
            case SettingsTab::Model: drawModelTab(state, actions); break;
            case SettingsTab::Motion: drawMotionTab(state, actions); break;
            case SettingsTab::Display: drawDisplayTab(state, actions); break;
            case SettingsTab::Graphics: drawGraphicsTab(state, actions); break;
            case SettingsTab::Advanced: drawAdvancedTab(state, actions); break;
            case SettingsTab::About: drawAboutTab(state); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        const ImVec2 windowSize = ImGui::GetWindowSize();
        windowRect_ = {static_cast<int>(origin.x), static_cast<int>(origin.y),
                       static_cast<int>(origin.x + windowSize.x),
                       static_cast<int>(origin.y + windowSize.y)};
    }
    ImGui::End();
    ImGui::Render();

    // 닫는 프레임은 커서가 닫기 버튼 위라 WantCaptureMouse가 참. 닫힌 뒤에는 frame()이 다시
    // 계산하지 않으므로 그대로 두면 클릭 통과가 계속 꺼져 화면 전체가 클릭을 받음
    wantsMouse_ = keepOpen && io.WantCaptureMouse;
    if (!keepOpen) {
        open_ = false;
    }
    return ImGui::GetDrawData();
}

// 제목 줄: 발바닥 아이콘, 제목, 닫기. 제목 줄을 끌면 창이 움직임 (창은 NoMove라 직접 옮김)
void SettingsUi::drawTitle(bool& keepOpen) const {
    const Palette& p = palette(dark_);
    const float s = scale_;
    const float height = kTitleHeight * s;
    const ImVec2 origin = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    ImDrawList& list = *ImGui::GetWindowDrawList();

    ImGui::SetCursorPos({0.0f, 0.0f});
    ImGui::InvisibleButton("##drag", {width - height, height});
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(offset(origin, delta.x, delta.y));
    }
    drawIcon(list, Icon::Paw, offset(origin, 24.0f * s, height * 0.5f), 18.0f * s, p.accent);
    list.AddText(offset(origin, 42.0f * s, (height - ImGui::GetFontSize()) * 0.5f), p.text,
                 "TinyDeskPet 설정");

    ImGui::SetCursorPos({width - height, 0.0f});
    if (ImGui::InvisibleButton("##close", {height, height})) {
        keepOpen = false;
    }
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 center = offset(origin, width - height * 0.5f, height * 0.5f);
    if (hovered) {
        list.AddCircleFilled(center, 13.0f * s, p.frameHover);
    }
    drawIcon(list, Icon::Close, center, kIconSize * s, hovered ? p.text : p.textSecondary);
    list.AddLine(offset(origin, 0.0f, height), offset(origin, width, height), p.border, s);
    ImGui::SetCursorPos({0.0f, height});
}

// 왼쪽 세로 탭: 아이콘 + 글자, 선택된 탭은 강조색 바탕
void SettingsUi::drawTabs() {
    const Palette& p = palette(dark_);
    const float s = scale_;
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = kTabHeight * s;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 4.0f * s});
    for (const TabInfo& info : kTabs) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(info.tab));
        if (ImGui::InvisibleButton("##tab", {width, height})) {
            tab_ = info.tab;
        }
        ImGui::PopID();
        const bool selected = tab_ == info.tab;
        if (selected || ImGui::IsItemHovered()) {
            list.AddRectFilled(pos, offset(pos, width, height),
                               selected ? p.accentSoft : p.frameHover, 8.0f * s);
        }
        const ImU32 color = selected ? p.accentText : p.textSecondary;
        drawIcon(list, info.icon, offset(pos, 18.0f * s, height * 0.5f), kIconSize * s, color);
        list.AddText(offset(pos, 36.0f * s, (height - ImGui::GetFontSize()) * 0.5f), color,
                     info.label);
    }
    ImGui::PopStyleVar();
}

void SettingsUi::drawModelTab(const SettingsState& state,
                              std::vector<SettingsAction>& actions) const {
    const float s = scale_;
    if (state.modelLoading) {
        ImGui::TextDisabled("모델을 불러오는 중…");
    } else if (state.modelName.empty()) {
        ImGui::TextDisabled("모델 없음");
    } else {
        ImGui::PushFont(nullptr, kTitleFontSize);
        ImGui::TextUnformatted(state.modelName.c_str());
        ImGui::PopFont();
    }
    infoRow("작가", state.modelAuthor, s);
    infoRow("라이선스", state.modelLicense, s);
    infoRow("파일", state.modelFile, s);
    ImGui::Spacing();
    if (iconButton("모델 열기…", Icon::Folder, palette(dark_), s)) {
        actions.push_back({SettingsAction::Kind::OpenModel, 0, {}, 0, false});
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "VRM, glTF, PMX, FBX 모델을 열 수 있습니다. 라이선스는 모델 제작자가 정한 "
        "이용 조건입니다.");
    ImGui::PopTextWrapPos();
}

void SettingsUi::drawMotionTab(const SettingsState& state,
                               std::vector<SettingsAction>& actions) const {
    const float s = scale_;
    bool anyChoice = false;
    for (std::size_t m = 0; m < state.motions.size(); ++m) {
        const MotionSetting& motion = state.motions[m];
        anyChoice = anyChoice || motion.choices.size() > 1;
        ImGui::PushID(static_cast<int>(m));
        rowLabel(motion.name.c_str(), s);
        const auto current = std::ranges::find(motion.choices, motion.current, &ClipChoice::value);
        const std::string& preview =
            current != motion.choices.end() ? current->label : motion.current;
        ImGui::SetNextItemWidth(-1.0f);
        // 기본 화살표 버튼(진한 사각형) 대신 옅은 선 화살표를 직접 그림
        const bool opened = ImGui::BeginCombo(
            "##clip", preview.c_str(), ImGuiComboFlags_HeightLarge | ImGuiComboFlags_NoArrowButton);
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        drawIcon(*ImGui::GetWindowDrawList(), Icon::ChevronDown,
                 {itemMax.x - 16.0f * s, (itemMin.y + itemMax.y) * 0.5f}, 14.0f * s,
                 palette(dark_).textSecondary);
        if (opened) {
            for (std::size_t c = 0; c < motion.choices.size(); ++c) {
                const ClipChoice& choice = motion.choices[c];
                const bool selected = choice.value == motion.current;
                ImGui::PushID(static_cast<int>(c));
                if (ImGui::Selectable(choice.label.c_str(), selected) && !selected) {
                    actions.push_back({SettingsAction::Kind::SetClip, m, choice.value, 0, false});
                }
                ImGui::PopID();
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (motion.duration > 0.0f) {
            // 클립을 처음 한 번 끝까지 재생한 뒤 이 시각부터 반복 (ADR-0014)
            rowLabel("  반복 시작", s);
            const float sliderWidth = ImGui::GetContentRegionAvail().x - kValueWidth * s;
            const float shown = std::min(motion.loopStart, motion.duration);
            const std::optional<float> picked = sliderValue("##loop", shown, 0.0f, motion.duration,
                                                            0.1f, sliderWidth, palette(dark_), s);
            if (picked) {
                SettingsAction action{SettingsAction::Kind::SetLoopStart, m, {}, 0, false};
                action.value = *picked;
                actions.push_back(action);
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%.1f초", picked.value_or(shown));
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", anyChoice ? "모델 파일 안의 애니메이션에서 고릅니다. 기본 동작은 "
                                          "코드로 만든 동작입니다. 상호작용은 더블클릭할 때 한 번 "
                                          "재생합니다."
                                        : "이 모델 파일에는 애니메이션이 없습니다. 다른 모션 "
                                          "파일은 deskpet.ini의 [animation]에서 지정합니다.");
    ImGui::PopTextWrapPos();
}

void SettingsUi::drawDisplayTab(const SettingsState& state,
                                std::vector<SettingsAction>& actions) const {
    const Palette& p = palette(dark_);
    const float s = scale_;
    if (const std::optional<int> scale =
            sliderRow("크기", "##scale", state.scalePercent, state.minScalePercent,
                      state.maxScalePercent, state.scaleStepPercent, p, s, "%")) {
        actions.push_back({SettingsAction::Kind::SetScale, 0, {}, *scale, false});
    }
    ImGui::Spacing();

    struct ToggleInfo {
        const char* id;
        const char* label;
        bool value;
        SettingsAction::Kind kind;
    };
    const std::array<ToggleInfo, 5> rows = {{
        {"##walk", "걷기", state.walk, SettingsAction::Kind::SetWalk},
        {"##walkOnWindows", "다른 창 위를 걷기", state.walkOnWindows,
         SettingsAction::Kind::SetWalkOnWindows},
        {"##lookAt", "커서 바라보기", state.lookAt, SettingsAction::Kind::SetLookAt},
        {"##idleMotion", "대기 중 숨쉬기", state.idleMotion, SettingsAction::Kind::SetIdleMotion},
        {"##alwaysOnTop", "다른 창보다 항상 위에", state.alwaysOnTop,
         SettingsAction::Kind::SetAlwaysOnTop},
    }};
    for (const ToggleInfo& row : rows) {
        if (toggleRow(row.label, row.id, row.value, p, s)) {
            actions.push_back({row.kind, 0, {}, 0, !row.value});
        }
    }
}

void SettingsUi::drawGraphicsTab(const SettingsState& state,
                                 std::vector<SettingsAction>& actions) const {
    const Palette& p = palette(dark_);
    const float s = scale_;
    if (toggleRow("수직 동기화", "##vsync", state.vsync, p, s)) {
        actions.push_back({SettingsAction::Kind::SetVsync, 0, {}, 0, !state.vsync});
    }
    hint("화면 주사율에 맞춰 그립니다. 끄면 CPU 사용률이 크게 오릅니다.");

    ImGui::TextDisabled("안티에일리어싱");
    constexpr std::array<int, 4> kSamples = {1, 2, 4, 8};
    const auto msaaIndex =
        static_cast<std::size_t>(std::ranges::find(kSamples, state.msaa) - kSamples.begin());
    if (const auto picked = segmented("##msaa", std::array<const char*, 4>{"끔", "2x", "4x", "8x"},
                                      msaaIndex, p, s)) {
        actions.push_back({SettingsAction::Kind::SetMsaa, 0, {}, kSamples[*picked], false});
    }
    hint("높을수록 테두리가 매끄럽지만 GPU 부담이 늘어납니다.");

    ImGui::TextDisabled("외곽선");
    // 버튼 순서 ↔ core::OutlineMode (열거형 순서와 다름)
    constexpr std::array<core::OutlineMode, 5> kOutlines = {
        core::OutlineMode::Auto, core::OutlineMode::Model, core::OutlineMode::All,
        core::OutlineMode::Silhouette, core::OutlineMode::Off};
    const auto outlineIndex =
        static_cast<std::size_t>(std::ranges::find(kOutlines, state.outline) - kOutlines.begin());
    if (const auto picked =
            segmented("##outline",
                      std::array<const char*, 5>{"자동", "모델 지정", "모든 재질", "외곽만", "끔"},
                      outlineIndex, p, s)) {
        actions.push_back(
            {SettingsAction::Kind::SetOutline, 0, {}, static_cast<int>(kOutlines[*picked]), false});
    }
    hint(
        "자동: 모델에 외곽선 정보가 있으면 모델 지정, 없으면 외곽만. 외곽만: 캐릭터 바깥 "
        "테두리에만 "
        "그려 눈·입에 선이 생기지 않습니다.");

    // 외곽선을 끄면 두께는 의미가 없으므로 흐리게 하고 누를 수 없게 함
    const bool outlineOff = state.outline == core::OutlineMode::Off;
    ImGui::BeginDisabled(outlineOff);
    if (const std::optional<int> scale =
            sliderRow("외곽선 두께", "##outlineScale", state.outlineScale, 0, 300, 10, p, s, "%")) {
        actions.push_back({SettingsAction::Kind::SetOutlineScale, 0, {}, *scale, false});
    }
    ImGui::EndDisabled();
    hint("모델이 정한 굵기에 곱합니다. 0%면 외곽선을 그리지 않습니다.");

    ImGui::TextDisabled("텍스처 여백 채우기");
    if (const auto picked =
            segmented("##padding", std::array<const char*, 3>{"자동", "모두 채움", "끔"},
                      static_cast<std::size_t>(state.texturePadding), p, s)) {
        actions.push_back(
            {SettingsAction::Kind::SetTexturePadding, 0, {}, static_cast<int>(*picked), false});
    }
    hint("모델 색이 이상하면 바꿔 보세요. 바꾸면 모델을 다시 읽습니다.");
}

void SettingsUi::drawAdvancedTab(const SettingsState& state,
                                 std::vector<SettingsAction>& actions) const {
    const Palette& p = palette(dark_);
    const float s = scale_;
    const MovementSetting& m = state.movement;
    struct SliderInfo {
        const char* label;
        const char* id;
        int value;
        int min;
        int max;
        int step;
        SettingsAction::Kind kind;
    };
    const std::array<SliderInfo, 3> rows = {{
        {"중력", "##gravity", m.gravity, 500, 6000, 100, SettingsAction::Kind::SetGravity},
        {"걷기 속도", "##walkSpeed", m.walkSpeed, 20, 200, 5, SettingsAction::Kind::SetWalkSpeed},
        {"끌기 시작", "##drag", m.dragThreshold, 0, 20, 1, SettingsAction::Kind::SetDragThreshold},
    }};
    for (const SliderInfo& row : rows) {
        if (const std::optional<int> value =
                sliderRow(row.label, row.id, row.value, row.min, row.max, row.step, p, s)) {
            actions.push_back({row.kind, 0, {}, *value, false});
        }
    }
    ImGui::Spacing();
    if (ImGui::Button("기본값으로")) {
        actions.push_back({SettingsAction::Kind::ResetMovement, 0, {}, 0, false});
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled(
        "중력(px/s²)·걷기 속도(px/s)는 캐릭터 움직임입니다. 끌기 시작(px)만큼 움직여야 누른 것을 "
        "끌기로 봅니다.");
    ImGui::PopTextWrapPos();
}

void SettingsUi::drawAboutTab(const SettingsState& state) {
    ImGui::PushFont(nullptr, kTitleFontSize);
    ImGui::TextUnformatted("TinyDeskPet");
    ImGui::PopFont();
    ImGui::TextDisabled("버전 %s", state.version.c_str());
    ImGui::Spacing();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted("C++20과 Direct3D 11로 만든 가벼운 데스크톱 펫입니다.");
    ImGui::TextDisabled("github.com/ChoHye0N/TinyDeskPet");
    ImGui::Spacing();
    ImGui::TextDisabled("설정은 실행 파일 옆 deskpet.ini에 바로 저장됩니다.");
    ImGui::PopTextWrapPos();
}

}  // namespace deskpet::ui
