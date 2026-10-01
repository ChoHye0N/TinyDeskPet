# 코딩 규칙

자동으로 검사할 수 있는 규칙은 도구(`.clang-format`, `.clang-tidy`, 컴파일러 경고)에 맡기고, 이 문서는 도구가 잡지 못하는 **판단 기준**을 정리합니다.

## 1. 언어와 표준

- C++20. 컴파일러 확장 사용 금지 (`CMAKE_CXX_EXTENSIONS OFF`, MSVC `/permissive-`).
- 소스 파일 인코딩은 **UTF-8 (BOM 없음)**, 줄바꿈은 LF (`.gitattributes`가 강제). MSVC는 `/utf-8`로 컴파일합니다.

## 2. 명명 규칙

| 대상 | 규칙 | 예 |
|---|---|---|
| 타입 (class, struct, enum, alias) | PascalCase | `CharacterController`, `RenderScene` |
| 인터페이스 | `I` 접두사 + PascalCase | `IWindow`, `IRenderPass` |
| 함수, 메서드 | camelCase | `pollEvents()`, `setGround()` |
| 지역 변수, 매개변수 | camelCase | `frameSeconds` |
| 비공개 멤버 변수 | camelCase + 뒤에 `_` | `position_`, `swapChain_` |
| 공개 구조체 필드 | camelCase (밑줄 없음) | `WindowDesc::alwaysOnTop` |
| 상수 (`constexpr`) | `k` 접두사 | `kFootMargin` |
| 열거형 값 | PascalCase | `State::Airborne` |
| 매크로 | `DESKPET_` 접두사 + 대문자 | `DESKPET_VERSION_STRING` |
| 네임스페이스 | 소문자 | `deskpet::core` |
| 파일 | 주 타입 이름과 동일 | `CharacterController.h/.cpp` |

## 3. 헤더

- `#pragma once` 사용.
- include 순서 (clang-format이 그룹별로 정렬):
  1. 대응하는 헤더 (`.cpp`의 첫 줄)
  2. 프로젝트 헤더 (`"core/Log.h"`)
  3. 서드파티 헤더 (`<gtest/gtest.h>`)
  4. 시스템·표준 헤더 (`<windows.h>`, `<vector>`)
- 헤더에서 `using namespace` 금지.
- Windows 헤더는 `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `UNICODE`가 CMake에서 정의됩니다. 소스에서 다시 정의하지 않습니다.

## 4. 소유권과 수명

| 상황 | 사용 |
|---|---|
| 단독 소유 | `std::unique_ptr<T>` |
| COM 객체 | `Microsoft::WRL::ComPtr<T>` (`Release` 직접 호출 금지) |
| 빌려 쓰기 (소유하지 않음) | `T&`, `const T&`, 또는 raw `T*` |
| 공유 소유 | 꼭 필요할 때만 `std::shared_ptr<T>`, 사용 이유를 주석으로 |
| Win32 핸들 | 소유 클래스의 소멸자에서 해제 (RAII) |

- `new`/`delete` 직접 사용 금지 (`std::make_unique`).
- 복사하면 안 되는 클래스(창, 렌더러)는 복사·이동 생성자를 `= delete`.

## 5. 오류 처리

[SAD §9](../02-architecture/SAD.md#9-오류-처리와-로깅-정책)를 따릅니다. 요약:

- 우리 코드는 예외를 던지지 않습니다. 실패 가능한 함수는 `[[nodiscard]] bool` 또는 결과 타입을 반환합니다.
- HRESULT는 `d3d11::check(hr, "설명")`로 검사합니다. 실패 원인이 로그에 남아야 합니다.
- 불변식 검사는 `assert`. 사용자 입력·파일 내용 검증에는 `assert`를 쓰지 않습니다.

## 6. 성능 습관

- 프레임마다 실행되는 코드에서 메모리 할당을 피합니다 (예: `events_` 벡터를 멤버로 두고 `clear()`로 재사용).
- 꺼진 로그 레벨은 문자열을 만들지 않습니다 (`logging::debug(...)`가 내부에서 먼저 검사).
- GPU 리소스(브러시, 버퍼)는 `create()`에서 만들고 `execute()`에서는 재사용합니다.

## 7. 주석

- 주석은 **왜**를 설명합니다. 코드가 무엇을 하는지는 코드가 말하게 합니다.
- 학습용 프로젝트이므로 DirectX/Win32의 비직관적인 동작에는 근거(문서 링크, ADR 번호)를 남깁니다.
- 미구현 지점은 `TODO(M번호): 설명` 형식. 로드맵과 연결됩니다.
- 주석 언어는 한국어를 기본으로 하되, API 이름·에러 코드는 원문 그대로 씁니다.

## 8. 포맷팅과 정적 분석

| 도구 | 설정 파일 | 실행 |
|---|---|---|
| clang-format 18 | `.clang-format` | `python scripts/format.py` (수정) / `--check` (검사) |
| clang-tidy 18 | `.clang-tidy` | `python scripts/run_clang_tidy.py --build-dir build/linux-gcc` |
| 컴파일러 경고 | `cmake/CompilerOptions.cmake` | MSVC `/W4`, GCC `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`. CI에서는 오류로 취급 |

도구 버전이 다르면 포맷 결과가 달라질 수 있어, 로컬과 CI 모두 **pip로 같은 버전**을 설치해 씁니다 (`pip install -r requirements-dev.txt`).

## 9. 테스트

- 테스트 파일은 `tests/<모듈>/<대상>Tests.cpp`.
- 테스트 이름: `TEST(대상, 상황_기대결과)` 예: `TEST(CharacterController, ReleaseAboveGround_FallsAndLands)`.
- 하나의 테스트는 하나의 동작만 검증합니다. 실패 메시지만 보고 원인을 알 수 있어야 합니다.
- 시간·창·렌더러는 가짜(Fake)로 주입합니다. 실제 시계를 읽는 테스트는 만들지 않습니다.
