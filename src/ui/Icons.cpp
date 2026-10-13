#include "ui/Icons.h"

#include <array>
#include <numbers>

namespace deskpet::ui {
namespace {

constexpr float kPi = std::numbers::pi_v<float>;

ImVec2 at(ImVec2 center, float size, float x, float y) {
    // (x, y)는 아이콘 상자 안의 비율 (-0.5 ~ 0.5)
    return {center.x + x * size, center.y + y * size};
}

void paw(ImDrawList& list, ImVec2 c, float s, ImU32 color) {
    list.AddCircleFilled(at(c, s, 0.0f, 0.16f), s * 0.22f, color);  // 발바닥
    constexpr std::array<std::array<float, 2>, 4> kToes = {
        {{-0.30f, -0.08f}, {-0.12f, -0.30f}, {0.12f, -0.30f}, {0.30f, -0.08f}}};
    for (const auto& [x, y] : kToes) {
        list.AddCircleFilled(at(c, s, x, y), s * 0.10f, color);
    }
}

void user(ImDrawList& list, ImVec2 c, float s, ImU32 color, float thick) {
    list.AddCircle(at(c, s, 0.0f, -0.16f), s * 0.18f, color, 0, thick);
    // 어깨: 아래쪽이 열린 반원
    list.PathArcTo(at(c, s, 0.0f, 0.42f), s * 0.32f, kPi, 2.0f * kPi);
    list.PathStroke(color, thick);
}

void monitor(ImDrawList& list, ImVec2 c, float s, ImU32 color, float thick) {
    list.AddRect(at(c, s, -0.42f, -0.34f), at(c, s, 0.42f, 0.18f), color, s * 0.06f, thick);
    list.AddLine(at(c, s, 0.0f, 0.18f), at(c, s, 0.0f, 0.36f), color, thick);
    list.AddLine(at(c, s, -0.20f, 0.38f), at(c, s, 0.20f, 0.38f), color, thick);
}

void info(ImDrawList& list, ImVec2 c, float s, ImU32 color, float thick) {
    list.AddCircle(c, s * 0.42f, color, 0, thick);
    list.AddCircleFilled(at(c, s, 0.0f, -0.18f), thick * 0.9f, color);
    list.AddLine(at(c, s, 0.0f, -0.04f), at(c, s, 0.0f, 0.22f), color, thick);
}

void folder(ImDrawList& list, ImVec2 c, float s, ImU32 color, float thick) {
    const std::array<ImVec2, 6> points = {at(c, s, -0.42f, -0.28f), at(c, s, -0.12f, -0.28f),
                                          at(c, s, 0.0f, -0.16f),   at(c, s, 0.42f, -0.16f),
                                          at(c, s, 0.42f, 0.32f),   at(c, s, -0.42f, 0.32f)};
    list.AddPolyline(points.data(), static_cast<int>(points.size()), color, thick,
                     ImDrawFlags_Closed);
}

}  // namespace

void drawIcon(ImDrawList& list, Icon icon, ImVec2 center, float size, ImU32 color) {
    const float thick = size * 0.09f;
    switch (icon) {
        case Icon::Paw: paw(list, center, size, color); break;
        case Icon::Close:
            list.AddLine(at(center, size, -0.28f, -0.28f), at(center, size, 0.28f, 0.28f), color,
                         thick);
            list.AddLine(at(center, size, 0.28f, -0.28f), at(center, size, -0.28f, 0.28f), color,
                         thick);
            break;
        case Icon::User: user(list, center, size, color, thick); break;
        case Icon::Play:
            list.AddTriangle(at(center, size, -0.24f, -0.32f), at(center, size, 0.34f, 0.0f),
                             at(center, size, -0.24f, 0.32f), color, thick);
            break;
        case Icon::Monitor: monitor(list, center, size, color, thick); break;
        case Icon::Info: info(list, center, size, color, thick); break;
        case Icon::Folder: folder(list, center, size, color, thick); break;
        case Icon::ChevronDown: {
            const std::array<ImVec2, 3> points = {at(center, size, -0.28f, -0.14f),
                                                  at(center, size, 0.0f, 0.14f),
                                                  at(center, size, 0.28f, -0.14f)};
            list.AddPolyline(points.data(), static_cast<int>(points.size()), color, thick);
            break;
        }
    }
}

}  // namespace deskpet::ui
