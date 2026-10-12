#include "app/Ground.h"

#include <algorithm>
#include <cstddef>

namespace deskpet::app {
namespace {

bool contains(const core::RectI& r, float x, float y) {
    return x >= static_cast<float>(r.left) && x < static_cast<float>(r.right) &&
           y >= static_cast<float>(r.top) && y < static_cast<float>(r.bottom);
}

}  // namespace

float groundBelow(core::Vec2 feet, float stepUp, float floor,
                  std::span<const core::RectI> windows) {
    float ground = floor;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const core::RectI& w = windows[i];
        const auto top = static_cast<float>(w.top);
        if (feet.x < static_cast<float>(w.left) || feet.x >= static_cast<float>(w.right) ||
            top < feet.y - stepUp || top >= ground) {
            continue;
        }
        // 윗변 바로 위 한 점이 앞 창 안이면 가려진 곳 (그 창 위에 설 수 없음)
        const bool covered = std::any_of(
            windows.begin(), windows.begin() + static_cast<std::ptrdiff_t>(i),
            [&](const core::RectI& front) { return contains(front, feet.x, top - 1.0f); });
        if (!covered) {
            ground = top;
        }
    }
    return ground;
}

}  // namespace deskpet::app
