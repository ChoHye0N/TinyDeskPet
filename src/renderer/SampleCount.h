#pragma once

// MSAA 샘플 수 선택 (플랫폼 독립). 명세: docs/03-detailed-design/renderer.md

namespace deskpet::renderer {

// 요청한 샘플 수가 지원되지 않으면 절반씩 낮춤 (8 → 4 → 2). 1 이하는 MSAA 끔.
// supported(n): 장치가 n 샘플 렌더 타깃을 만들 수 있는지
template <class Supported>
[[nodiscard]] int chooseSampleCount(int requested, Supported&& supported) {
    for (int n = requested; n > 1; n /= 2) {
        if (supported(n)) {
            return n;
        }
    }
    return 1;
}

}  // namespace deskpet::renderer
