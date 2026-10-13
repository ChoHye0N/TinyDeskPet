#pragma once

// 설정 창 아이콘 (ui 모듈 내부 전용). 아이콘 글꼴 파일 없이 ImGui 그리기 함수로 단순한 선
// 아이콘을 그림 → 용량·라이선스 부담 없음 (ADR-0016)

#include <cstdint>
#include <imgui.h>

namespace deskpet::ui {

enum class Icon : std::uint8_t { Paw, Close, User, Play, Monitor, Info, Folder, ChevronDown };

// center를 중심으로 size×size 안에 그림. 선 두께는 size에 비례
void drawIcon(ImDrawList& list, Icon icon, ImVec2 center, float size, ImU32 color);

}  // namespace deskpet::ui
