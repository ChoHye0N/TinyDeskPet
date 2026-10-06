# 기여 가이드

빌드 방법, 작업 흐름(브랜치·커밋·PR), 릴리스, 문제 해결을 한곳에 모은 문서입니다.
CI 파이프라인의 구조는 [CI/CD 구현 설명](docs/05-devops/ci-cd-implementation.md)을 보세요.

## 1. 처음 한 번만

| 도구 | 비고 |
|---|---|
| Visual Studio 2022 이상 | **C++를 사용한 데스크톱 개발** 워크로드 (CMake·Ninja·Windows SDK 포함). **언어 팩에 영어**도 설치 (§5 참고) |
| Git for Windows, Python 3.10 이상 | pre-commit 훅, 포맷·정적 분석 스크립트 |
| 그래픽 도구 (선택) | 설정 → 시스템 → 선택적 기능. D3D11 디버그 레이어 (없으면 Debug에서 경고 후 끄고 실행) |
| GitHub CLI (선택) | PR·릴리스를 명령줄에서 |

```bash
pip install -r requirements-dev.txt     # clang-format, clang-tidy (CI와 같은 버전)
git config core.hooksPath .githooks     # 커밋 전 포맷 자동 검사
```

## 2. 빌드와 실행

**Visual Studio**: 폴더 열기 → 구성 **Windows x64 (MSVC)** → 시작 항목 **DeskPet.exe** → F5. 테스트는 테스트 탐색기.

**명령줄**: 반드시 **x64 Native Tools Command Prompt for VS**에서 실행합니다. 일반 "Developer Command Prompt"는 32비트라 configure가 멈춥니다.

| 하고 싶은 일 | 명령 |
|---|---|
| 구성 → 빌드 → 테스트 → zip 한 번에 | `cmake --workflow --preset windows-release` |
| Debug 빌드·테스트 | `cmake --preset windows-msvc` → `cmake --build --preset windows-debug` → `ctest --preset windows-debug` |
| CI와 똑같이 | `cmake --workflow --preset ci-windows-debug` / `ci-windows-release` |
| Linux/WSL (플랫폼 독립 모듈, ASan/UBSan) | `cmake --workflow --preset ci-linux` |
| 포맷 / 정적 분석 | `python scripts/format.py [--check]` / (Linux) `python3 scripts/run_clang_tidy.py --build-dir build/linux-gcc` |

- 실행 파일 옆 `deskpet.ini`는 빌드할 때 `config/deskpet.ini`로 덮어씁니다. 실험은 실행 폴더 쪽을 고치세요.
- 로그는 실행 파일 옆 `deskpet.log` (`[log] level = debug`로 자세히). GPU 프레임 분석은 PIX 또는 RenderDoc, 디바이스 손실 테스트는 관리자 권한으로 `dxcap -forcetdr`.

## 3. 작업 흐름

```text
1. main에서 브랜치 생성      git switch -c feat/throw-velocity
2. 테스트 먼저 → 구현 → 커밋 (설계가 바뀌면 상세 설계·ADR, 끝나면 학습 노트도 같은 PR에서)
3. 푸시 후 PR 생성           gh pr create --fill
4. 자동 병합 예약            gh pr merge --auto --squash   (main 보호: CI 통과 시 Squash 병합)
```

| 브랜치 접두사 | 용도 |
|---|---|
| `feat/` · `fix/` · `refactor/` | 기능 · 버그 수정 · 동작 변화 없는 구조 개선 |
| `docs/` · `ci/` · `build/` | 문서 · 파이프라인 · 빌드 설정 |

커밋 메시지는 Conventional Commits입니다. **요약 한 줄, 빈 줄, `- ` 항목**만 쓰고 설명 문단은 쓰지 않습니다.

```text
<type>(<scope>): <요약, 50자 이내>

- <바꾼 것 1>
- <바꾼 것 2>
```

- type: `feat`, `fix`, `refactor`, `test`, `docs`, `build`, `ci`, `perf`, `chore`
- scope: `core`, `model`, `anim`, `character`, `platform`, `renderer`, `app`, `ci`, `docs`

## 4. 버전과 릴리스

`VERSION` 파일이 버전의 유일한 원천입니다 (SemVer, 마일스톤 하나 = MINOR 하나, M6 완료 시 1.0.0). 버그 수정 릴리스는 PATCH를 올립니다.

```text
[ ] 1. release/X.Y.Z 브랜치에서 VERSION 수정
[ ] 2. CHANGELOG.md: [Unreleased] → [X.Y.Z] - YYYY-MM-DD, 하단 비교 링크 추가
[ ] 3. PR → CI 통과 → Squash 병합
[ ] 4. git switch main && git pull
[ ] 5. git tag -a vX.Y.Z -m "DeskPet X.Y.Z" && git push origin vX.Y.Z
[ ] 6. Actions → Release 성공 확인, Releases에서 zip을 받아 깨끗한 폴더에서 실행
```

릴리스 워크플로가 실패하면(태그와 `VERSION` 불일치, CHANGELOG 섹션 없음, 빌드 실패) 고친 뒤 태그를 다시 만듭니다: `git tag -d vX.Y.Z` → `git push --delete origin vX.Y.Z` → 위 4~5단계. 이미 배포된 태그는 바꾸지 않고 다음 PATCH로 냅니다.

## 5. 문제 해결

| 증상 | 원인 | 해결 |
|---|---|---|
| `code should be clang-formatted` | 포맷 위반 | `python scripts/format.py` 후 커밋 |
| `[readability-identifier-naming,-warnings-as-errors]` | 명명 규칙 위반 ([코딩 규칙](docs/04-conventions/coding-style.md)) | 이름 수정. 정당한 예외면 `// NOLINT(검사이름): 이유` |
| Linux: `windows.h: No such file or directory` | 플랫폼 독립 모듈(core/model/anim/character/app)에서 Windows 헤더 사용 | `platform/win32` 또는 `renderer/d3d11`로 이동 |
| Linux: `AddressSanitizer` / `runtime error:` | 메모리 오류 / 정의되지 않은 동작 | 로그의 스택 트레이스(파일:줄)를 따라가 수정 |
| Windows: `error C2220` | `/WX`로 경고가 오류가 됨 | 바로 위의 실제 경고(C4xxx) 수정 |
| `No tests were found!!!` | 새 테스트 파일 미등록 | `tests/CMakeLists.txt`에 추가 |
| `FetchContent ... Failed to clone` | 일시적 네트워크 문제 | Re-run failed jobs |
| configure가 `64비트 MSVC가 아닙니다`로 멈춤, 또는 링크 오류 `__stdcall`·`__thiscall`·`__purecall` | 32비트 Developer Command Prompt에서 configure | x64 Native Tools 프롬프트에서 `cmake --preset <이름> --fresh` → `--clean-first` 빌드 |
| Debug 종료 시 `Run-Time Check Failure #2 - Stack around the variable ... was corrupted`, 헤더를 고쳤는데 반영 안 됨 | 한국어 MSVC의 `/showIncludes` 머리말이 콘솔 코드 페이지마다 달라 Ninja가 헤더 의존성을 놓침 (configure 때 경고) | 당장: `--clean-first` 빌드. 근본: VS 언어 팩 **영어** 설치 후 `--fresh` (프리셋의 `VSLANG=1033`) |
| CodeQL 경고 | Security → Code scanning | 실제 문제면 수정 PR, 오탐이면 이유와 함께 Dismiss |
