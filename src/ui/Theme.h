#pragma once

// 설정 창 테마 (ui 모듈 내부 전용): 둥근 모서리, 넉넉한 여백, 강조색 하나 (ADR-0016).
// 색은 여기 한곳에 모아 두어 강조색이나 어두운 테마를 바꾸기 쉽게 함

#include "core/Config.h"

#include <imgui.h>

namespace deskpet::ui {

// 직접 그리는 위젯(탭, 토글, 슬라이더, 아이콘)이 쓰는 색
struct Palette {
    ImU32 windowBg;
    ImU32 text;
    ImU32 textSecondary;
    ImU32 border;
    ImU32 frame;       // 입력 칸·트랙 바탕
    ImU32 frameHover;  // 마우스를 올린 행·버튼
    ImU32 accent;      // 선택된 값, 켜진 토글, 슬라이더 채움
    ImU32 accentSoft;  // 선택된 탭 바탕
    ImU32 accentText;  // accentSoft 위 글자
    ImU32 knob;        // 토글 손잡이
};

// 밝기(밝은·어두운 테마)와 강조색으로 정한 색
[[nodiscard]] Palette palette(bool dark, core::AccentColor accent);

// 모서리·여백·색을 style에 씀 (크기는 96 DPI 기준, 호출한 쪽이 ScaleAllSizes로 키움)
void applyTheme(ImGuiStyle& style, bool dark, core::AccentColor accent);

// 기본 글자 크기 (px, 96 DPI)
inline constexpr float kFontSize = 15.0f;
inline constexpr float kTitleFontSize = 18.0f;

}  // namespace deskpet::ui
