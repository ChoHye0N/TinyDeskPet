#include "ui/Theme.h"

namespace deskpet::ui {
namespace {

// 0xRRGGBB (+ 알파) → ImGui 색 (ImU32는 ABGR 순서)
constexpr ImU32 rgb(unsigned value, unsigned alpha = 0xFF) {
    return IM_COL32((value >> 16U) & 0xFFU, (value >> 8U) & 0xFFU, value & 0xFFU, alpha);
}

ImVec4 toVec4(ImU32 color) {
    return ImGui::ColorConvertU32ToFloat4(color);
}

// 밝은 테마: 흰 바탕, 회색 글자, 분홍 강조 하나
constexpr Palette kLight{
    rgb(0xFFFFFF), rgb(0x2C2C2A), rgb(0x6B6A65), rgb(0xE3E1DA), rgb(0xF4F3EF),
    rgb(0xECEAE4), rgb(0xD4537E), rgb(0xFBEAF0), rgb(0x72243E), rgb(0xFFFFFF),
};

// 어두운 테마: Windows 앱 모드가 어둡게일 때
constexpr Palette kDark{
    rgb(0x232321), rgb(0xEDECE8), rgb(0xA8A69E), rgb(0x3A3936), rgb(0x2D2C2A),
    rgb(0x363532), rgb(0xE07A9E), rgb(0x4B1528), rgb(0xF4C0D1), rgb(0xFFFFFF),
};

}  // namespace

const Palette& palette(bool dark) {
    return dark ? kDark : kLight;
}

void applyTheme(ImGuiStyle& style, bool dark) {
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

    const Palette& p = palette(dark);
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
