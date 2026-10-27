#include "ui/Theme.h"

#include <array>
#include <cstddef>

namespace deskpet::ui {
namespace {

// 0xRRGGBB (+ 알파) → ImGui 색 (ImU32는 ABGR 순서)
constexpr ImU32 rgb(unsigned value, unsigned alpha = 0xFF) {
    return IM_COL32((value >> 16U) & 0xFFU, (value >> 8U) & 0xFFU, value & 0xFFU, alpha);
}

ImVec4 toVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

// 밝은 테마: 흰 바탕, 회색 글자, 강조색 하나 (강조 세 칸은 accentFor가 채움)
constexpr Palette kLight{
    rgb(0xFFFFFF), rgb(0x2C2C2A), rgb(0x6B6A65), rgb(0xE3E1DA), rgb(0xF4F3EF),
    rgb(0xECEAE4), rgb(0xD4537E), rgb(0xFBEAF0), rgb(0x72243E), rgb(0xFFFFFF),
};

// 어두운 테마: Windows 앱 모드가 어둡게일 때
constexpr Palette kDark{
    rgb(0x232321), rgb(0xEDECE8), rgb(0xA8A69E), rgb(0x3A3936), rgb(0x2D2C2A),
    rgb(0x363532), rgb(0xE07A9E), rgb(0x4B1528), rgb(0xF4C0D1), rgb(0xFFFFFF),
};

// 강조색마다 (강조, 옅은 바탕, 옅은 바탕 위 글자). 밝은 테마는 진한 강조 + 아주 옅은 바탕,
// 어두운 테마는 밝은 강조 + 아주 어두운 바탕
struct AccentShades {
    unsigned accent;
    unsigned soft;
    unsigned text;
};
constexpr std::array<AccentShades, 5> kLightAccents = {{
    {0xD4537E, 0xFBEAF0, 0x72243E},  // Pink
    {0x7F77DD, 0xEEEDFE, 0x3C3489},  // Purple
    {0x378ADD, 0xE6F1FB, 0x0C447C},  // Blue
    {0x1D9E75, 0xE1F5EE, 0x085041},  // Green
    {0xD85A30, 0xFAECE7, 0x712B13},  // Orange
}};
constexpr std::array<AccentShades, 5> kDarkAccents = {{
    {0xE07A9E, 0x4B1528, 0xF4C0D1},
    {0x9C95E8, 0x26215C, 0xCECBF6},
    {0x5FA2E6, 0x042C53, 0xB5D4F4},
    {0x3FB68E, 0x04342C, 0x9FE1CB},
    {0xE57A55, 0x4A1B0C, 0xF5C4B3},
}};

}  // namespace

Palette palette(bool dark, core::AccentColor accent) {
    Palette p = dark ? kDark : kLight;
    const auto& shades = dark ? kDarkAccents : kLightAccents;
    const auto index = static_cast<std::size_t>(accent);
    if (index < shades.size()) {
        p.accent = rgb(shades[index].accent);
        p.accentSoft = rgb(shades[index].soft);
        p.accentText = rgb(shades[index].text);
    }
    return p;
}

void applyTheme(ImGuiStyle& style, bool dark, core::AccentColor accent) {
    style = ImGuiStyle();
    style.WindowRounding = 12.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.WindowPadding = {16.0f, 14.0f};
    style.FramePadding = {10.0f, 6.0f};
    style.ItemSpacing = {10.0f, 10.0f};
    style.ItemInnerSpacing = {8.0f, 6.0f};
    style.ScrollbarSize = 10.0f;
    style.FontSizeBase = kFontSize;

    const Palette p = palette(dark, accent);
    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = toVec4(p.text);
    c[ImGuiCol_TextDisabled] = toVec4(p.textSecondary);
    c[ImGuiCol_WindowBg] = toVec4(p.windowBg);
    c[ImGuiCol_ChildBg] = toVec4(rgb(0, 0));
    c[ImGuiCol_PopupBg] = toVec4(p.windowBg);
    c[ImGuiCol_Border] = toVec4(p.border);
    c[ImGuiCol_BorderShadow] = toVec4(rgb(0, 0));
    c[ImGuiCol_FrameBg] = toVec4(p.windowBg);
    c[ImGuiCol_FrameBgHovered] = toVec4(p.frameHover);
    c[ImGuiCol_FrameBgActive] = toVec4(p.frameHover);
    c[ImGuiCol_Button] = toVec4(p.frame);
    c[ImGuiCol_ButtonHovered] = toVec4(p.frameHover);
    c[ImGuiCol_ButtonActive] = toVec4(p.accentSoft);
    c[ImGuiCol_Header] = toVec4(p.accentSoft);
    c[ImGuiCol_HeaderHovered] = toVec4(p.frameHover);
    c[ImGuiCol_HeaderActive] = toVec4(p.accentSoft);
    c[ImGuiCol_ScrollbarBg] = toVec4(rgb(0, 0));
    c[ImGuiCol_ScrollbarGrab] = toVec4(p.border);
    c[ImGuiCol_ScrollbarGrabHovered] = toVec4(p.textSecondary);
    c[ImGuiCol_ScrollbarGrabActive] = toVec4(p.textSecondary);
    c[ImGuiCol_CheckMark] = toVec4(p.accent);
    c[ImGuiCol_SliderGrab] = toVec4(p.accent);
    c[ImGuiCol_SliderGrabActive] = toVec4(p.accent);
    c[ImGuiCol_Separator] = toVec4(p.border);
    c[ImGuiCol_NavCursor] = toVec4(p.accent);
}

}  // namespace deskpet::ui
