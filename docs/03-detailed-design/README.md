# 상세 설계서

| 항목 | 내용 |
|---|---|
| 문서 ID | DP-DDS-001 |
| 버전 | 0.1.0 |
| 상위 문서 | [SAD](../02-architecture/SAD.md) |

이 폴더는 모듈별 상세 설계를 담습니다. 각 문서는 같은 목차를 따릅니다.

1. **책임** — 이 모듈이 하는 일과 하지 않는 일
2. **파일 구성** — 소스 파일과 역할
3. **공개 인터페이스** — 다른 모듈이 사용하는 타입과 함수 (코드와 1:1 대응)
4. **동작 명세** — 규칙, 상태, 경계 조건
5. **테스트 항목** — 무엇을 어떻게 검증하는가
6. **확장 지점** — 이후 마일스톤에서 구현할 `TODO(M#)` 목록

| 모듈 | 문서 | 플랫폼 독립 | CMake 타깃 |
|---|---|---|---|
| core | [core.md](core.md) | ✅ | `deskpet_core` |
| model | [model.md](model.md) | ✅ | `deskpet_model`, `deskpet_model_thirdparty` |
| anim | [anim.md](anim.md) | ✅ | `deskpet_anim` |
| character | [character.md](character.md) | ✅ | `deskpet_character` |
| platform | [platform.md](platform.md) | API ✅ / 구현 ❌ | `deskpet_platform_api`, `deskpet_platform_win32` |
| renderer | [renderer.md](renderer.md) | API ✅ / 구현 ❌ | `deskpet_renderer_api`, `deskpet_renderer_d3d11` |
| app | [app.md](app.md) | ✅ (진입점 제외) | `deskpet_app`, `DeskPet` |

## 공통 규칙

- 네임스페이스는 `deskpet::<모듈>` 입니다 (예: `deskpet::core`, `deskpet::character`).
- include 경로는 `src/` 기준입니다: `#include "core/Log.h"`.
- 공개 함수 중 반환값을 무시하면 안 되는 것은 `[[nodiscard]]`를 붙입니다.
- 소유권: 단독 소유는 `std::unique_ptr`, COM 객체는 `Microsoft::WRL::ComPtr`, 빌려 쓰는 것은 참조 또는 raw 포인터(소유하지 않음을 의미).
- 자세한 코딩 규칙은 [coding-style.md](../04-conventions/coding-style.md)를 따릅니다.
