# 로컬 개발 환경

| 항목 | 내용 |
|---|---|
| 문서 ID | DP-OPS-004 |
| 대상 | Windows 10/11에서 DeskPet을 처음 빌드하는 개발자 |

## 1. 설치

| 도구 | 설치 방법 | 비고 |
|---|---|---|
| Visual Studio 2022 이상 (Community 무료) | Visual Studio Installer → **C++를 사용한 데스크톱 개발** 워크로드 | 오른쪽 "설치 세부 정보"에서 **Windows용 C++ CMake 도구**, **Windows 11 SDK**가 체크되어 있는지 확인. CMake와 Ninja가 함께 설치됨 |
| Git for Windows | https://git-scm.com | Git Bash 포함 (pre-commit 훅 실행에 사용) |
| Python 3.10 이상 | https://python.org 또는 `winget install Python.Python.3.12` | 포맷·정적 분석·릴리스 스크립트용 |
| 그래픽 도구 (선택) | 설정 → 시스템 → 선택적 기능 → **그래픽 도구** 추가 | D3D11 디버그 레이어. 없으면 Debug 빌드에서 경고만 남기고 끈 채로 실행 |
| GitHub CLI (선택) | `winget install GitHub.cli` | PR·릴리스를 명령줄에서 |

개발 도구 (저장소 폴더에서 한 번):

```bash
pip install -r requirements-dev.txt      # clang-format 18, clang-tidy 18 (CI와 같은 버전)
git config core.hooksPath .githooks      # 커밋 전 포맷 자동 검사
```

## 2. 빌드와 실행

### 2.1 Visual Studio (권장)

1. **파일 → 열기 → 폴더** → 저장소 폴더
2. VS가 `CMakePresets.json`을 읽어 구성합니다 (GoogleTest 다운로드 때문에 처음에는 1분 정도 걸림)
3. 툴바 구성 목록: **Windows x64 (MSVC)**, 빌드 구성: **Debug** 또는 **Release**
4. 시작 항목: **DeskPet.exe** → **F5** (디버깅) / **Ctrl+F5** (디버깅 없이)
5. 테스트: **테스트 → 테스트 탐색기** (GoogleTest 자동 인식)

> 빌드 후 `deskpet.ini`가 실행 파일 옆에 자동 복사됩니다. 설정을 바꿔 보려면 `build\windows-msvc\bin\Debug\deskpet.ini`를 수정하세요 (원본 `config\deskpet.ini`를 고치면 다음 빌드 때 덮어씀).

### 2.2 명령줄

시작 메뉴 → **x64 Native Tools Command Prompt for VS** (또는 **Developer PowerShell for VS**)를 열고:

```bat
cd C:\path\to\deskpet

:: 한 번에: 구성 → 빌드 → 테스트 → zip
cmake --workflow --preset windows-release

:: 단계별
cmake --preset windows-msvc
cmake --build --preset windows-debug
ctest --preset windows-debug
build\windows-msvc\bin\Debug\DeskPet.exe
```

> 일반 PowerShell이나 cmd에서는 `cl.exe`를 찾지 못해 실패합니다. 반드시 개발자 명령 프롬프트를 쓰세요.

### 2.3 Linux / WSL (플랫폼 독립 모듈만)

CI의 Linux 잡을 재현하거나, Windows 없이 로직만 개발할 때:

```bash
sudo apt install g++ cmake ninja-build python3-pip
cmake --workflow --preset ci-linux        # GCC + ASan/UBSan, 경고=오류
```

## 3. 프리셋 요약

| 하고 싶은 일 | 명령 |
|---|---|
| Windows 디버그 빌드 | `cmake --preset windows-msvc` → `cmake --build --preset windows-debug` |
| Windows 테스트 | `ctest --preset windows-debug` |
| 배포 zip 만들기 | `cmake --workflow --preset windows-release` |
| CI와 똑같이 (Windows) | `cmake --workflow --preset ci-windows-release` |
| CI와 똑같이 (Linux) | `cmake --workflow --preset ci-linux` |
| 프리셋 목록 | `cmake --list-presets=all` |

개인 설정(예: 다른 빌드 폴더)은 `CMakeUserPresets.json`을 만들어 쓰세요. `.gitignore`에 등록되어 있어 커밋되지 않습니다.

## 4. 코드 품질 도구

```bash
python scripts/format.py              # 포맷 적용
python scripts/format.py --check      # 포맷 검사 (CI와 동일)
```

clang-tidy는 Linux/WSL에서 실행합니다 (CI도 Linux에서 실행):

```bash
cmake --preset linux-gcc
python3 scripts/run_clang_tidy.py --build-dir build/linux-gcc
```

## 5. 디버깅 팁

| 상황 | 방법 |
|---|---|
| 로그 보기 | Visual Studio **출력** 창 (디버깅 중) 또는 실행 파일 옆 `deskpet.log` |
| 더 자세한 로그 | `deskpet.ini`의 `[log] level = debug` → 캐릭터 상태 전이가 기록됨 |
| D3D11 오류 위치 찾기 | Debug 빌드 + 그래픽 도구 설치 → 디버거가 오류 지점에서 자동으로 멈춤 |
| GPU 프레임 분석 | Visual Studio **그래픽 진단** (Alt+F5) 또는 [PIX](https://devblogs.microsoft.com/pix/) |
| 디바이스 손실 테스트 | 관리자 명령 프롬프트에서 `dxcap -forcetdr` → 펫이 1초 안에 다시 보여야 함 (FR-33) |
| CPU/메모리 측정 | 작업 관리자 **세부 정보** 탭, 또는 Windows Performance Recorder (WPR) |
| 펫이 화면 밖으로 사라짐 | 우클릭 메뉴가 안 보이면 작업 관리자에서 종료 후 재실행 (위치는 저장되지 않음) |

## 6. 폴더 정리

| 폴더 | 내용 | 지워도 되나 |
|---|---|---|
| `build/` | 모든 빌드 산출물 (프리셋별 하위 폴더) | ✅ 다음 구성 때 다시 만들어짐 |
| `.vs/` | Visual Studio 캐시 | ✅ |
| `assets/models/` | 개인 모델 파일 | 커밋되지 않으니 직접 관리 |
