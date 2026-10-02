# 학습 노트

프로젝트를 진행하며 배운 것을 짧게 모은 문서입니다. 자세한 내용은 링크된 설계 문서·ADR에 있습니다.
새 작업을 마칠 때마다 해당 절에 한두 줄씩 추가합니다.

- [1. 전체 구조](#1-전체-구조)
- [2. 사용한 기술·알고리즘](#2-사용한-기술알고리즘)
- [3. 버그와 해결](#3-버그와-해결)
- [4. 작업 방식](#4-작업-방식)

---

## 1. 전체 구조

### 모듈과 의존 방향

```text
main_win32.cpp (Windows 진입점: 설정·로그·단일 인스턴스, 구체 객체 조립)
   │
   ▼
app ──────────► character   상태 머신·물리 (Idle/Walk/Dragged/Airborne)
 │   └────────► anim        본 회전 계산·FK·스킨 행렬·표정
 │   └────────► model       VRM·glTF / PMX / FBX → 공통 Model
 │   └────────► platform    IWindow (구현: win32)
 │   └────────► renderer    IRenderer (구현: d3d11)
 ▼
core  로그·설정(INI)·시간·수학(Vec/Mat4/Quat)·이벤트
```

- **규칙**: `core`·`model`·`anim`·`character`·`app`은 Windows 헤더를 쓰지 않음 → Linux CI에서 빌드·테스트로 강제 ([SAD §4.2](../02-architecture/SAD.md))
- **인터페이스 분리**: 앱은 `IWindow`, `IRenderer`만 앎 → 테스트에서 가짜 창·렌더러를 주입 (ADR-0002)
- **도메인과 렌더링 분리**: character와 anim은 서로 모름. app이 `character::Pose` → `anim::AnimationInput`으로 변환

### 한 프레임의 흐름

```text
창 메시지 → core::Event로 변환 → app이 처리 (드래그·메뉴·DPI 변경 …)
→ 고정 시간 간격(1/60s)으로 character.update (ADR-0004)
→ 창 위치 = 캐릭터 발 위치
→ anim.evaluate (상태 → 본 회전 → 스킨 행렬, 표정 가중치)
→ RenderScene 구성 → renderer.render (장면이 같으면 Present 생략)
```

### 데이터의 공통 규약

모든 모델 형식을 **미터, 오른손 좌표계, +Y 위, 정면 +Z, 반시계(CCW) 앞면, UV 원점 왼쪽 위**로 맞춘 뒤 `model::Model` 하나로 다룹니다. 본 이름은 VRM 휴머노이드 이름(`hips`, `leftUpperArm` …)으로 통일 → 애니메이션 코드 하나로 모든 형식이 움직임 (ADR-0009).

---

## 2. 사용한 기술·알고리즘

### 창·합성 (Win32, DirectComposition)

| 기술 | 핵심 |
|---|---|
| 투명 창 | `WS_EX_NOREDIRECTIONBITMAP` + DirectComposition 스왑체인(`CreateSwapChainForComposition`, flip 모델) (ADR-0001) |
| premultiplied alpha | 스왑체인 알파 모드가 premultiplied → 셰이더가 `rgb *= a`로 출력, 블렌드는 `ONE, INV_SRC_ALPHA` |
| 클릭 통과 | `SetWindowRgn`으로 캐릭터 모양 바깥을 창 영역에서 제외 |
| 직접 드래그 | `SetCapture` + `GetMessagePos`(메시지 발생 시점 화면 좌표) (ADR-0003) |
| 트레이 | `Shell_NotifyIconW` + `NOTIFYICON_VERSION_4`, 탐색기 재시작(`TaskbarCreated`) 시 재등록 |
| 단일 인스턴스 | 이름 있는 뮤텍스 `Local\DeskPet.SingleInstance`, `ERROR_ALREADY_EXISTS`면 종료 |
| 고 DPI | 매니페스트 PerMonitorV2, `GetDpiForWindow`, `WM_DPICHANGED` → 창 크기 = 설정 × 배율 |
| 전력 절약 | 장면이 직전과 같으면 Present 생략 + `MsgWaitForMultipleObjectsEx`로 입력이 올 때까지 대기 |

### 물리·상태 (character)

| 기술 | 핵심 |
|---|---|
| 고정 시간 간격 | 프레임 시간을 누적해 1/60초 단위로 갱신 → 결과가 프레임률과 무관, 테스트가 결정적 |
| 반암시적 오일러 | 속도 먼저, 새 속도로 위치 (단순 오일러보다 안정) |
| 던지기 속도 | 포인터 샘플 8칸 링 버퍼, 마지막 이동 기준 0.1초 평균 속도. 멈췄다 놓으면 0 |
| 공기 저항 | `v *= exp(-k·dt)` — dt가 커도 부호가 뒤집히지 않음 |
| 벽 반사 | 가상 데스크톱(`SM_XVIRTUALSCREEN`) 좌우 끝에서 속도 반전 × 반발 계수 |
| 걷기 | 무작위(시드 주입) 대기·걷기 시간, 벽에서 방향 반전 |

### 모델 로딩 (model)

| 기술 | 핵심 |
|---|---|
| 형식 판별 | 매직 바이트(`glTF`, `PMX `, `Kaydara FBX Binary`) 우선, ASCII FBX만 확장자 |
| glTF/VRM | cgltf로 파싱. VRM 확장 JSON은 필요한 키만 괄호 짝 맞추기로 스캔 |
| 바인드 포즈 굽기 | 로드 시 `Σ wᵢ·(역바인드ᵢ × 관절월드ᵢ)`로 정점을 바인드 포즈 모델 공간에 둠 |
| VRM 0.x | 정면 −Z → Y축 180° 회전 = (x, z) 부호 반전 |
| PMX | 직접 파싱. 범위 검사 리더(넘치면 실패 플래그, 예외 없음), UTF-16 → UTF-8, 가변 인덱스 크기, 본 플래그별 가변 필드. z 반전(왼손 → 오른손) 때문에 삼각형 감기 순서 교환, × 0.08(MMD 단위 → m) |
| FBX | ufbx가 축·단위 변환(`target_axes`, `target_unit_meters`), 다각형 삼각화. UV는 `v' = 1 − v` |
| 스킨 가중치 정리 | 같은 본 합치기 → 큰 순서 4개 → 합 1로 정규화 |
| 텍스처 | PNG/JPEG는 렌더러가 WIC로, TGA는 stb_image로 미리 RGBA로 |
| 텍스처 패딩 | UV 삼각형을 텍셀 격자에 래스터화해 섬 표시 → 바깥으로 한 겹씩 이웃 평균색 번지기(dilation) 16겹 → 이음매 선 제거 |

### 렌더링 (renderer, D3D11)

| 기술 | 핵심 |
|---|---|
| 행렬 규약 | 행 벡터(`p * M`), 행 우선 저장 — DirectXMath와 같음. HLSL은 `row_major`로 그대로 읽음 |
| 카메라 맞춤 | 화각 20° 원근. 경계 상자 앞면이 화면에 꼭 맞는 거리 → 뒤쪽 점은 원근으로 안쪽에 모임 |
| 셰이더 빌드 | `fxc /Fh`로 바이트코드를 헤더로 만들어 실행 파일에 내장 |
| 2단 툰 | `lerp(0.75, 1, smoothstep(-0.05, 0.05, N·L))` |
| 정렬 | 불투명·마스크 먼저(깊이 쓰기), 반투명 나중(깊이 읽기만) |
| GPU 스키닝 | 정점에 본 4개 인덱스·가중치, 스킨 행렬은 구조화 버퍼(상수 버퍼의 행렬 1024개 제한 회피), VS에서 행렬 가중 합 |
| 표정 | 희소 정점 델타를 표정이 바뀔 때만 CPU에서 합산 → 두 번째 정점 스트림 |
| 텍스처 메모리 | 긴 변 512로 축소 + 밉맵 (메모리 169MB → 101MB) |
| 디바이스 손실 | Present가 `DEVICE_REMOVED/RESET`이면 모든 GPU 리소스를 다시 만듦 |

### 애니메이션 (anim)

| 기술 | 핵심 |
|---|---|
| 쿼터니언 | 해밀턴 곱 `a * b` = b 먼저 (행렬 곱과 순서가 반대인 점 주의). `fromTo`로 두 방향 사이 최소 회전 |
| FK | 본마다 "모델 축 기준 델타 회전"을 부모 자세 위에 누적: `Aⱼ = Δⱼ × A부모`, `Pⱼ = P부모 + A부모(pⱼ − p부모)` |
| 스킨 행렬 | `Sⱼ = T(−pⱼ) · R(Aⱼ) · T(Pⱼ)` — 정점이 이미 바인드 포즈에 있으므로 역바인드가 단순 이동 |
| 계층 순서 | 루트까지 깊이로 정렬 → 부모가 항상 먼저 (본 배열 순서와 무관) |
| 팔 내리기 | 위팔의 바인드 방향 → 목표 방향 `fromTo`. T포즈든 A포즈든 같은 결과 |
| 주기 동작 | 걷기 0.6초 주기 사인파로 다리 교차·팔 반대 흔들기 |
| 카메라 경계 | 대기·매달림·공중 자세로 정점을 CPU 스키닝한 경계 상자의 합 |
| 자세 전환 보간 | 직전에 그린 자세 → 새 자세를 0.2초 slerp(smoothstep). q와 −q는 같은 회전이라 내적 부호를 맞춰야 짧은 길로 돎. 착지 반동은 보간 뒤에 더해 약해지지 않게 함 |

### 설정·로그·빌드·CI

| 기술 | 핵심 |
|---|---|
| INI 쓰기 | 줄 단위로 나눠 해당 키의 값만 교체 → 주석·순서·줄바꿈(LF/CRLF) 유지 |
| 로그 회전 | 시작 시 기존 로그를 `.1`로, 실행 중 4MB 한도 |
| CMake | Presets + Ninja Multi-Config, FetchContent는 태그·커밋으로 버전 고정, 서드파티는 경고 옵션을 안 거는 별도 타깃 |
| 정적 분석 | clang-format·clang-tidy 18(CI와 같은 버전), 경고를 오류로 |
| CI | Windows(Debug/Release) + Linux(GCC, ASan/UBSan) + clang-tidy + CodeQL(공개 저장소일 때만) |

---

## 3. 버그와 해결

형식: **증상** → 원인 → 해결 (교훈)

| # | 증상 | 원인 | 해결 | 교훈 |
|---|---|---|---|---|
| 1 | 피부가 파랗게 보임 | WIC 스케일러(Fant)가 출력 형식을 BGRA로 바꿈 → R/B 뒤바뀜 | 축소 뒤 RGBA로 한 번 더 변환 | 기능을 넣은 직후 화면을 직접 확인할 것 |
| 2 | ini·모델을 바꿔도 실행 폴더에 반영 안 됨 | POST_BUILD 복사는 재링크될 때만 실행 | 매 빌드 실행되는 커스텀 타깃으로 복사 | 빌드 단계가 "언제" 실행되는지 확인 |
| 3 | 저장된 위치가 화면 위쪽 밖(y 음수) | 공중에 있을 때 종료하면 그 높이를 저장 | 높이는 항상 바닥으로 저장 | 저장 값은 복원 조건까지 고려 |
| 4 | 대기 중에도 GPU 사용(Present 생략 안 됨) | 숨긴 슬라임의 숨쉬기 값이 매 프레임 장면을 바꿈 | 모델이 있으면 슬라임 값을 기본값으로 고정 | "보이지 않는" 값도 비교 대상이면 영향을 줌. 측정이 잘못 기록됐던 것도 정정 |
| 5 | 몸통 중앙·입 테두리에 얇은 선 | UV 섬 바깥의 검은 여백이 필터링·밉맵·축소 때 섞임 | 텍스처 패딩(섬 색 번지기) | 텍스처 끄기 + 배경 마젠타로 "메시 틈"과 "텍스처"를 구분해 진단 |
| 6 | 셰이더를 고쳐도 결과가 그대로 | 생성된 셰이더 헤더 변경을 Ninja가 추적 못함 → `MeshPass.cpp` 미재컴파일 | `OBJECT_DEPENDS`로 의존성 명시 | 진단이 이상하면 빌드 산출물부터 의심 (`ninja -d explain`) |
| 7 | Release만 한 번 충돌 (재현 안 됨) | 미확정. #6의 옛 셰이더와 새 정점 형식 불일치 가능성 | Release도 PDB 생성해 다음 발생 시 추적 | 재현 안 되는 문제는 추적 수단부터 마련 |
| 8 | fxc가 "Illegal character" | PowerShell 5.1 `Set-Content -Encoding utf8`이 BOM·CRLF를 붙임 | 파일 수정은 Python(`newline=''`)이나 `[IO.File]` 사용 | 도구가 파일 인코딩을 바꾸는지 확인 |
| 9 | CI clang-tidy 실패 | π 리터럴 → `modernize-use-std-numbers` | `std::numbers::pi_v<float>` | 로컬 clang-tidy(Windows)는 MSVC 표준 라이브러리와 맞지 않아 헤더 검사가 불완전 → CI가 기준 |
| 10 | CI CodeQL 실패 | 비공개 저장소는 코드 스캐닝 불가 | 공개 여부를 조회해 비공개면 건너뜀 | 실패 원인이 코드가 아닌 설정일 수 있음 |
| 11 | 메모리 169MB | 2048~4096 텍스처를 원본 크기로 업로드 | 긴 변 512로 축소 디코딩 | 창 크기에 비해 과한 리소스는 줄이기 |

---

## 4. 작업 방식

- **테스트 먼저**: 플랫폼 독립 로직은 GoogleTest로 먼저 테스트를 쓰고 구현. 모델 파일은 저장소에 없으므로 테스트 데이터를 코드로 생성(`GltfBuilder`, `PmxBuilder`, ASCII FBX 문자열)
- **결정은 ADR로**: 설계가 바뀌면 ADR과 상세 설계를 같은 변경에서 갱신
- **GitHub Flow**: `main`에서 짧은 브랜치 → PR → CI 통과 → Squash merge → 브랜치 삭제
- **커밋**: Conventional Commits. 요약 한 줄, 빈 줄, `- ` 항목
- **진단 습관**: 추측 대신 대조 실험(텍스처 끄기, 배경색 바꾸기, 설정 하나만 바꿔 비교)으로 원인을 좁힘
