# ADR-0006: CMake Presets + Ninja Multi-Config 빌드

| 항목 | 내용 |
|---|---|
| 상태 | 승인 |
| 날짜 | 2026-10-01 |
| 관련 요구사항 | NFR-PORT-01, NFR-MAINT-02 |

## 맥락

- Visual Studio에서 편하게 개발하면서도, CI(GitHub Actions)와 Linux에서 **같은 명령**으로 빌드하고 싶습니다.
- "내 PC에서는 되는데 CI에서는 안 된다"를 줄이려면 빌드 옵션이 한 곳에 정의되어야 합니다.

## 고려한 대안

| 대안 | 장점 | 단점 |
|---|---|---|
| A. Visual Studio 솔루션(.sln/.vcxproj) | VS에서 가장 익숙함 | Linux 빌드 불가, 설정이 XML에 흩어짐, 머지 충돌 잦음 |
| B. CMake + Visual Studio 생성기 | VS 친화적 | 생성기 이름이 VS 버전(2022, 2026...)에 묶임. CI 러너 이미지가 바뀌면 깨짐 |
| **C. CMake + `CMakePresets.json` + Ninja Multi-Config** | VS "폴더 열기"에서 프리셋을 그대로 인식, CI와 로컬이 같은 프리셋 사용, VS 버전에 독립적 | 명령줄 빌드 시 개발자 명령 프롬프트(MSVC 환경)가 필요 |

## 결정

**C**를 선택합니다.

- 구성(configure) 프리셋: `windows-msvc`, `linux-gcc`, `linux-gcc-asan`, 그리고 CI 전용 `ci-windows`, `ci-linux`
- 워크플로(workflow) 프리셋: 구성 → 빌드 → 테스트 → 패키지를 한 명령으로 실행 (`cmake --workflow --preset ci-windows`)
- 외부 의존성(GoogleTest)은 `FetchContent`로 가져오고 버전을 고정합니다.
- MSVC 런타임은 정적 링크(`/MT`)하여 배포 시 재배포 패키지 설치가 필요 없게 합니다.

## 결과

- 좋아지는 점: CI가 실패하면 로컬에서 **똑같은 명령**으로 재현할 수 있습니다.
- 감수하는 점: CMake 문법을 익혀야 합니다. 프리셋 파일 스키마 버전 6 이상이므로 CMake 3.25 이상이 필요합니다.
- 후속 작업: 의존성이 늘어나면 vcpkg 매니페스트 모드로 전환을 검토합니다 (새 ADR 작성).
