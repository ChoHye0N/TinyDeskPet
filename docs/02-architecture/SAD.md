# 소프트웨어 아키텍처 설계서 (SAD)

| 항목 | 내용 |
|---|---|
| 문서 ID | DP-SAD-001 |
| 버전 | 0.1.0 |
| 상태 | 승인 (M0 기준선) |
| 관련 문서 | [SRS](../01-requirements/SRS.md), [ADR 목록](adr/README.md), [상세 설계](../03-detailed-design/README.md) |

### 변경 이력

| 버전 | 날짜 | 내용 |
|---|---|---|
| 0.1.0 | 2026-10-01 | 최초 작성 |
| 0.2.0 | 2026-10-01 | `model` 모듈 추가, MeshPass (ADR-0008) |
| 0.3.0 | 2026-10-01 | M1: 던지기·경계·걷기, 트레이, 단일 인스턴스, DPI, Present 생략, 위치 저장. DEBT-01·02 해소 |
| 0.4.0 | 2026-10-01 | `model` 다중 형식(VRM/glTF, PMX, FBX)과 휴머노이드 본 통일 (ADR-0009) |
| 0.5.0 | 2026-10-01 | `anim` 모듈: 코드로 계산하는 휴머노이드 동작, GPU 스키닝, 표정 (ADR-0010) |

---

## 1. 목적

이 문서는 DeskPet의 **전체 구조**와 그 구조를 선택한 **이유**를 설명합니다. 개별 클래스의 함수 명세는 [상세 설계](../03-detailed-design/README.md)를 참고하세요.

## 2. 아키텍처 목표

품질 속성을 우선순위 순으로 정리했습니다. 설계가 충돌할 때 위쪽 목표를 따릅니다.

| 순위 | 품질 속성 | 의미 | 이를 위한 결정 |
|---|---|---|---|
| 1 | 성능 | 상주 프로그램이므로 CPU·메모리를 거의 쓰지 않아야 함 | DirectComposition 합성, 캐릭터 크기의 작은 창, VSync 동기화 |
| 2 | 테스트 용이성 | 그래픽·OS 없이 로직을 검증할 수 있어야 함 | 플랫폼/렌더러를 인터페이스로 분리하고 의존성 주입 |
| 3 | 이식성 | 나중에 macOS(Metal) 백엔드를 추가할 수 있어야 함 | 플랫폼 독립 모듈은 Win32 헤더 금지 |
| 4 | 확장성 | 플레이스홀더 → VRM 3D로 바꿀 때 상위 코드 변경 최소화 | 렌더 패스 구조, 렌더러는 `RenderScene` 데이터만 받음 |
| 5 | 학습 가치 | DirectX 파이프라인을 직접 다룸 | 엔진·UI 프레임워크 미사용 |

## 3. 시스템 컨텍스트

DeskPet과 외부 요소의 관계입니다 (C4 모델 Level 1).

```mermaid
flowchart LR
    User(["사용자"])
    subgraph PC["사용자 PC (Windows 10/11)"]
        DeskPet["DeskPet.exe"]
        DWM["Desktop Window Manager<br/>(화면 합성)"]
        GPU["GPU 드라이버<br/>(D3D11)"]
        FS[("실행 파일 폴더<br/>deskpet.ini · 모델 파일")]
    end
    User -- "마우스 입력" --> DeskPet
    DeskPet -- "창 메시지 수신 / 창 이동" --> DWM
    DeskPet -- "그리기 명령" --> GPU
    GPU -- "스왑체인 버퍼" --> DWM
    DWM -- "합성된 화면" --> User
    DeskPet -- "설정·모델 읽기" --> FS
```

## 4. 논리 구조: 레이어와 모듈

### 4.1 레이어 다이어그램

```mermaid
flowchart TB
    subgraph L4["엔트리 포인트 (Windows 전용)"]
        Main["main_win32.cpp<br/>객체 조립 · 설정 로드"]
    end
    subgraph L3["애플리케이션 (플랫폼 독립)"]
        App["app<br/>Application · 메인 루프"]
    end
    subgraph L2["도메인 (플랫폼 독립)"]
        Char["character<br/>상태 머신 · 물리 · 포즈"]
    end
    subgraph L1A["플랫폼 추상화"]
        PApi["platform API<br/>IWindow"]
        RApi["renderer API<br/>IRenderer · RenderScene"]
    end
    subgraph L1B["플랫폼 구현 (Windows 전용)"]
        Win32["platform/win32<br/>Win32Window"]
        D3D["renderer/d3d11<br/>D3D11Renderer · 렌더 패스"]
    end
    subgraph L0["기반 (플랫폼 독립)"]
        Core["core<br/>Log · Config · Clock · Math · Events"]
        Model["model<br/>VRM·glTF / PMX / FBX 로더"]
        Anim["anim<br/>뼈대 FK · 코드 동작"]
    end

    Main --> App
    Main --> Win32
    Main --> D3D
    App --> Char
    App --> PApi
    App --> RApi
    Win32 -. 구현 .-> PApi
    D3D -. 구현 .-> RApi
    Char --> Core
    PApi --> Core
    RApi --> Core
    RApi --> Model
    App --> Model
    App --> Anim
    Anim --> Model
    Model --> Core
    App --> Core
```

### 4.2 의존성 규칙

| 규칙 | 설명 | 강제 방법 |
|---|---|---|
| R1 | 화살표 방향으로만 의존한다. 하위 레이어는 상위를 모른다 | CMake 타깃 링크 관계 |
| R2 | `core`, `model`, `anim`, `character`, `app`은 `<windows.h>`나 DirectX 헤더를 include 하지 않는다 | Linux CI 빌드가 실패하므로 자동 검출 |
| R3 | `character`는 렌더러를 모른다. 렌더러도 `character`를 모른다. 둘 사이 변환은 `app`이 한다 | 타깃 링크 관계 |
| R4 | 구체 클래스(`Win32Window`, `D3D11Renderer`)를 아는 곳은 `main_win32.cpp` 하나뿐이다 | 코드 리뷰 |

> **왜 R3인가?** 캐릭터 로직(도메인)은 "어디에 있고 어떤 자세인가"만 알면 되고, "어떻게 그리는가"는 렌더러의 일입니다. 둘을 분리하면 플레이스홀더에서 VRM으로 렌더러를 바꿔도 캐릭터 로직과 그 테스트는 그대로 둘 수 있습니다.

### 4.3 모듈 책임

| 모듈 | 책임 | 하지 않는 일 |
|---|---|---|
| `core` | 로그, 설정 파싱, 시간, 수학 타입, 입력 이벤트 정의, 고정 시간 간격 계산 | OS 호출(파일 읽기 제외), 그래픽 |
| `model` | VRM·glTF / PMX / FBX 파일 → 공통 규약의 렌더링용 데이터(정점·인덱스·머티리얼·텍스처·본), 바인드 포즈 굽기, 휴머노이드 본 이름 통일 (ADR-0009) | GPU 업로드, PNG/JPEG 디코딩 |
| `anim` | 휴머노이드 본 회전(상태별 코드 동작)·FK·스킨 행렬·표정 가중치 (ADR-0010) | 캐릭터 상태 결정, GPU 업로드 |
| `character` | 캐릭터 상태(대기/드래그/공중), 위치·속도, 중력, 애니메이션용 포즈 값 계산 | 창 이동, 그리기 |
| `platform` (API) | 창 생성, 이벤트 수집, 창 위치·클릭 영역·메뉴, 작업 영역 조회를 **인터페이스로** 정의 | 구현 |
| `platform/win32` | 위 인터페이스를 Win32로 구현 | 게임 로직 |
| `renderer` (API) | 렌더러 인터페이스와 렌더링 입력 데이터(`RenderScene`) 정의 | 구현 |
| `renderer/d3d11` | D3D11 + DXGI + DComp로 투명 스왑체인 생성, 렌더 패스 실행, 디바이스 손실 복구 | 캐릭터 로직 |
| `app` | 메인 루프, 이벤트 분배, 도메인 ↔ 창 ↔ 렌더러 사이 데이터 변환, 메뉴 처리 | OS·그래픽 API 직접 호출 |

## 5. 컴포넌트 구조

핵심 타입 관계입니다. 전체 멤버는 [상세 설계](../03-detailed-design/README.md)에 있습니다.

```mermaid
classDiagram
    direction LR
    class Application {
        -AppConfig config_
        -unique_ptr~IWindow~ window_
        -unique_ptr~IRenderer~ renderer_
        -CharacterController character_
        -FixedTimestep timestep_
        +run() int
        +requestQuit()
    }
    class IWindow {
        <<interface>>
        +create(WindowDesc) bool
        +pollEvents(vector~Event~) void
        +setPosition(PointI) void
        +workArea() RectI
        +setHitRegion(RectI, HitShape) void
        +showContextMenu(items, PointI) int
        +nativeHandle() void*
    }
    class IRenderer {
        <<interface>>
        +initialize(void*, SizeI, RendererOptions) bool
        +render(RenderScene) FrameResult
        +resize(SizeI) void
        +shutdown() void
    }
    class CharacterController {
        +onPointerDown(Vec2)
        +onPointerMove(Vec2)
        +onPointerUp(Vec2)
        +onDoubleClick()
        +update(double dt)
        +pose() CharacterPose
    }
    class Win32Window
    class D3D11Renderer {
        -vector~unique_ptr~IRenderPass~~ passes_
        -handleDeviceLost() bool
    }
    class IRenderPass {
        <<interface>>
        +create(D3D11Context) bool
        +execute(D3D11Context, RenderScene)
        +release()
    }
    class PlaceholderPass
    class MeshPass {
        <<M2 이후 구현>>
    }

    Application o-- IWindow
    Application o-- IRenderer
    Application *-- CharacterController
    IWindow <|.. Win32Window
    IRenderer <|.. D3D11Renderer
    D3D11Renderer *-- IRenderPass
    IRenderPass <|.. PlaceholderPass
    IRenderPass <|.. MeshPass
```

## 6. 런타임 구조

### 6.1 스레딩 모델

M0는 **단일 스레드**입니다. 하나의 스레드가 메시지 처리 → 업데이트 → 렌더링 → Present를 반복합니다.

- 이유: Win32 창은 생성한 스레드에서만 메시지를 받고, D3D11 즉시 컨텍스트는 스레드 안전하지 않습니다. 작은 펫 하나에 멀티스레드는 복잡도 대비 이득이 없습니다.
- 확장 계획: M3에서 VRM/텍스처 로딩을 **작업 스레드**로 분리합니다. 로딩 결과는 큐로 메인 스레드에 넘기고, GPU 리소스 생성은 메인 스레드에서 합니다 (ID3D11Device는 스레드 안전하므로 작업 스레드에서 생성하는 것도 선택 가능).

### 6.2 시작 시퀀스

```mermaid
sequenceDiagram
    autonumber
    participant Main as main_win32
    participant Cfg as core::Config
    participant Win as Win32Window
    participant Rnd as D3D11Renderer
    participant App as Application

    Main->>Cfg: loadConfigFile("deskpet.ini")
    Cfg-->>Main: AppConfig (+경고 목록)
    Main->>Main: 로그 싱크·레벨 설정
    Main->>App: Application(config, window, renderer)
    Main->>App: run()
    App->>Win: create(WindowDesc)
    Win-->>App: true
    App->>Rnd: initialize(hwnd, size, options)
    Note over Rnd: D3D11 디바이스 → 컴포지션 스왑체인<br/>→ DComp 타깃/비주얼 → 렌더 패스 생성
    Rnd-->>App: true
    App->>Win: workArea()
    App->>App: resetCharacterPosition()<br/>(오른쪽 아래, 바닥 위)
    App->>Win: setHitRegion(...)
    App->>Win: show()
    App->>App: 메인 루프 진입
```

### 6.3 프레임 루프

한 프레임의 처리 순서입니다. 업데이트는 **고정 시간 간격(60Hz)**, 렌더링은 **모니터 주사율(VSync)**에 맞춥니다 ([ADR-0004](adr/0004-fixed-timestep-update.md)).

```mermaid
sequenceDiagram
    autonumber
    participant App as Application
    participant Win as IWindow
    participant Chr as CharacterController
    participant Rnd as IRenderer

    loop 매 프레임 (종료 요청 전까지)
        App->>Win: pollEvents(events)
        Win-->>App: [PointerDown, PointerMove, ...]
        App->>Chr: onPointerXxx(...) 로 이벤트 전달
        App->>App: steps = timestep.advance(frameDelta)
        loop steps 회 (보통 0~2회)
            App->>Chr: update(1/60초)
        end
        App->>Win: setPosition(발 위치 → 창 좌상단 변환)
        App->>App: scene = buildScene(character.pose())
        App->>Rnd: render(scene)
        Note over Rnd: Clear(투명) → 렌더 패스들 → Present(VSync)<br/>Present가 다음 VSync까지 대기 → CPU 휴식
        Rnd-->>App: FrameResult
    end
```

### 6.4 드래그 → 낙하 시나리오

`HTCAPTION`을 이용한 OS 기본 창 드래그는 드래그하는 동안 메인 루프를 멈추게 합니다(모달 루프). 그래서 드래그를 **직접 구현**합니다 ([ADR-0003](adr/0003-manual-window-drag.md)).

```mermaid
sequenceDiagram
    autonumber
    actor U as 사용자
    participant Win as Win32Window
    participant App as Application
    participant Chr as CharacterController

    U->>Win: 왼쪽 버튼 누름 (WM_LBUTTONDOWN)
    Win->>Win: SetCapture(hwnd)
    Win-->>App: PointerDown(화면 좌표)
    App->>Chr: onPointerDown(p)
    Note over Chr: 누른 위치와 잡은 오프셋 기록<br/>상태는 아직 Idle
    U->>Win: 마우스 이동 (WM_MOUSEMOVE)
    Win-->>App: PointerMove(p)
    App->>Chr: onPointerMove(p)
    Note over Chr: 4px 이상 움직임 → Dragged<br/>발 위치 = 커서 + 잡은 오프셋
    App->>Win: setPosition(...) — 창이 커서를 따라감
    U->>Win: 버튼 놓음 (WM_LBUTTONUP)
    Win->>Win: ReleaseCapture()
    Win-->>App: PointerUp(p)
    App->>Chr: onPointerUp(p)
    Note over Chr: 바닥보다 위 → Airborne
    loop 바닥에 닿을 때까지
        App->>Chr: update(dt) — 중력 가속
        App->>Win: setPosition(...)
    end
    Note over Chr: 바닥 도달 → Idle + 착지 반동
```

### 6.5 캐릭터 상태 머신

```mermaid
stateDiagram-v2
    [*] --> Idle
    Idle --> Dragged: 누른 채 임계 거리 이상 이동
    Dragged --> Airborne: 놓음 (발이 바닥보다 위)
    Dragged --> Idle: 놓음 (발이 바닥 위치 이하)
    Idle --> Airborne: 더블클릭 (점프) 또는 바닥이 아래로 이동
    Airborne --> Idle: 바닥에 닿음 (착지 반동 시작, 수평 속도는 미끄러짐)
    Airborne --> Dragged: 공중에서 붙잡음
    Idle --> Walk: 무작위 대기 시간 경과
    Walk --> Idle: 무작위 걷기 시간 경과
    Walk --> Dragged: 누른 채 임계 거리 이상 이동
    Walk --> Airborne: 점프 또는 바닥이 아래로 이동
```

놓는 순간 최근 드래그 속도로 던져지고(FR-16), 발 x는 가상 데스크톱 안에 머뭅니다(FR-17). 자세한 규칙: [character.md](../03-detailed-design/character.md) §4

### 6.6 GPU 디바이스 손실 복구

```mermaid
sequenceDiagram
    autonumber
    participant App as Application
    participant Rnd as D3D11Renderer
    participant DX as D3D11 / DXGI

    App->>Rnd: render(scene)
    Rnd->>DX: Present(1, 0)
    DX-->>Rnd: DXGI_ERROR_DEVICE_REMOVED
    Rnd->>DX: GetDeviceRemovedReason() — 원인 로그
    Rnd->>Rnd: releaseDeviceResources()<br/>(패스 → 렌더 타깃 → DComp → D2D → 스왑체인 → D3D 순)
    Rnd->>Rnd: createDeviceResources()
    alt 재생성 성공
        Rnd-->>App: FrameResult::DeviceRecovered
    else 재생성 실패
        Rnd-->>App: FrameResult::Fatal
        App->>App: 오류 로그 후 종료
    end
```

## 7. 렌더링 아키텍처

### 7.1 투명 창 합성 경로

레이어드 윈도우(`UpdateLayeredWindow`)는 매 프레임 GPU 결과를 CPU 메모리로 복사합니다. DeskPet은 DirectComposition으로 **GPU 안에서** 합성합니다 ([ADR-0001](adr/0001-directcomposition-for-transparency.md)).

```mermaid
flowchart LR
    subgraph DeskPet
        D3D["ID3D11Device<br/>(BGRA 지원)"]
        SC["컴포지션 스왑체인<br/>B8G8R8A8 · Premultiplied Alpha<br/>Flip Sequential · 버퍼 2개"]
        V["IDCompositionVisual"]
        T["IDCompositionTarget<br/>(HWND에 연결)"]
    end
    DWM["DWM"]
    Screen["화면"]
    D3D -- "렌더 타깃으로 그림" --> SC
    SC -- "SetContent" --> V
    V -- "SetRoot" --> T
    T -- "Commit" --> DWM
    DWM --> Screen
```

핵심 조건 세 가지:

1. 창은 `WS_EX_NOREDIRECTIONBITMAP` 스타일로 만들어 GDI용 비트맵을 만들지 않는다.
2. 스왑체인은 `CreateSwapChainForComposition`으로 만들고 `DXGI_ALPHA_MODE_PREMULTIPLIED`를 쓴다.
3. 모든 픽셀 출력은 **premultiplied alpha**여야 한다 (RGB에 이미 알파가 곱해진 값). 셰이더를 직접 작성할 때 반드시 지킨다.

### 7.2 렌더 패스 구조

렌더러는 등록된 패스를 순서대로 실행합니다. M0에는 `PlaceholderPass` 하나만 있습니다.

```mermaid
flowchart LR
    Begin["프레임 시작<br/>RTV Clear(0,0,0,0)"] --> Mesh["MeshPass<br/>(M2~: D3D11 3D)"]
    Mesh --> Overlay["PlaceholderPass / DebugOverlay<br/>(Direct2D)"]
    Overlay --> Present["Present(VSync)"]
    style Mesh stroke-dasharray: 5 5
```

- 3D 패스(D3D11)를 먼저, 2D 오버레이(Direct2D)를 나중에 그립니다. 두 API는 같은 D3D11 디바이스와 백버퍼를 공유합니다.
- 패스는 디바이스 종속 리소스를 가지므로 디바이스 손실 시 `release()` → `create()`로 다시 만들어집니다.

## 8. 좌표계

```mermaid
flowchart TB
    subgraph Screen["화면 좌표계 (물리 픽셀, 원점 = 주 모니터 좌상단, y는 아래로 증가)"]
        subgraph Window["창 (크기 W×H)"]
            Body(("캐릭터"))
            Feet["● 발 위치 = 창 하단 중앙"]
        end
        Ground["━━━━ 바닥 y = 작업 영역 하단 ━━━━"]
    end
```

| 값 | 좌표계 | 소유자 |
|---|---|---|
| 캐릭터 발 위치 (`CharacterController::position`) | 화면 좌표, float | character |
| 창 좌상단 | 화면 좌표, int | platform |
| 렌더링 좌표 (`RenderScene`) | 창 내부 좌표 (0,0 = 창 좌상단) | renderer |

변환 규칙: `창 좌상단 = (round(발.x − W/2), round(발.y − H))`. 이 변환은 `app`이 담당합니다.

- 프로세스는 **Per-Monitor DPI Aware V2**로 실행되어 모든 좌표가 물리 픽셀입니다. 고해상도 모니터에서 캐릭터 크기 보정은 M1 과제입니다.

## 9. 오류 처리와 로깅 정책

| 상황 | 처리 |
|---|---|
| 초기화 실패 (창, 디바이스 생성) | `bool` 반환 + `error` 로그 → `main`이 메시지 박스로 알리고 종료 코드 1 |
| HRESULT 실패 | `d3d11::check(hr, "설명")`으로 로그(호출 위치 포함) 후 `false` 반환 |
| 디바이스 손실 | 복구 시도 (§6.6) |
| 설정 값 오류 | 기본값 사용 + `warn` 로그 (프로그램 계속) |
| 프로그래밍 오류 (불변식 위반) | `assert` (Debug 빌드에서만) |
| 예외 | 표준 라이브러리 예외(`bad_alloc` 등)만 `main`에서 최종 포착. 우리 코드는 예외를 던지지 않음 |

로그는 `core::logging`을 통해 남기며, 출력 대상(싱크)은 `main`이 결정합니다. Windows에서는 `OutputDebugString`(Visual Studio 출력 창)과 `deskpet.log` 파일에 씁니다.

## 10. 빌드 구조

CMake 타깃 의존 그래프입니다. 점선 타깃은 Windows에서만 빌드됩니다.

```mermaid
flowchart BT
    core["deskpet_core<br/>(STATIC)"]
    character["deskpet_character<br/>(STATIC)"]
    papi["deskpet_platform_api<br/>(INTERFACE)"]
    rapi["deskpet_renderer_api<br/>(INTERFACE)"]
    app["deskpet_app<br/>(STATIC)"]
    win32["deskpet_platform_win32<br/>(STATIC)"]
    d3d11["deskpet_renderer_d3d11<br/>(STATIC)"]
    exe["DeskPet.exe<br/>(WIN32 실행 파일)"]
    tests["deskpet_tests<br/>(GoogleTest)"]

    character --> core
    papi --> core
    rapi --> core
    app --> character
    app --> papi
    app --> rapi
    win32 --> papi
    d3d11 --> rapi
    exe --> app
    exe --> win32
    exe --> d3d11
    tests --> app

    style win32 stroke-dasharray: 5 5
    style d3d11 stroke-dasharray: 5 5
    style exe stroke-dasharray: 5 5
```

## 11. 배포 구조

릴리스 산출물은 설치가 필요 없는 zip입니다 (NFR-USE-01).

```
DeskPet-0.1.0-win64.zip
└── DeskPet-0.1.0-win64/
    ├── bin/
    │   ├── DeskPet.exe        ← 버전 정보 리소스 포함, MSVC 런타임 정적 링크
    │   └── deskpet.ini        ← 기본 설정 (주석 포함)
    ├── README.md
    ├── CHANGELOG.md
    └── LICENSE
```

빌드부터 배포까지의 자동화는 [CI/CD 구현 문서](../05-devops/ci-cd-implementation.md)를 참고하세요.

## 12. 위험 요소와 기술 부채

| ID | 위험 / 부채 | 영향 | 대응 |
|---|---|---|---|
| RISK-01 | GPU 드라이버 리셋 시 DComp 창이 사라짐 | 펫이 조용히 사라짐 | 디바이스 손실 복구 구현 (FR-33) |
| RISK-02 | `SetWindowRgn`의 도형 영역은 캐릭터 실루엣과 정확히 일치하지 않음 | 모서리 클릭이 통과/차단 오차 | M5에서 알파 기반 히트 테스트로 교체 |
| RISK-03 | 단일 스레드에서 큰 VRM 로딩 시 프레임 멈춤 | 시작 시 잠깐 멈춤 | M3에서 작업 스레드 로딩 |
| RISK-04 | Direct3D 디버그 레이어는 "그래픽 도구" 선택적 기능이 설치되어야 동작 | Debug 빌드 디바이스 생성 실패 | 디버그 플래그 실패 시 플래그 없이 재시도 |
| ~~DEBT-01~~ | ✅ 해소 (M1): 창 크기 × DPI 배율, `WM_DPICHANGED` 처리 | 사용성 | — |
| ~~DEBT-02~~ | ✅ 해소 (M1): 장면이 같으면 Present 생략 + 이벤트 대기 | 전력 소모 | — |

## 13. 아키텍처 의사결정 기록

| ADR | 제목 |
|---|---|
| [0001](adr/0001-directcomposition-for-transparency.md) | 투명 창 합성에 DirectComposition 사용 |
| [0002](adr/0002-platform-renderer-abstraction.md) | 플랫폼·렌더러 인터페이스 분리와 의존성 주입 |
| [0003](adr/0003-manual-window-drag.md) | OS 기본 창 드래그 대신 직접 드래그 구현 |
| [0004](adr/0004-fixed-timestep-update.md) | 고정 시간 간격 업데이트 |
| [0005](adr/0005-vrm-over-live2d.md) | 캐릭터 포맷으로 VRM 채택 |
| [0006](adr/0006-cmake-presets-ninja.md) | CMake Presets + Ninja Multi-Config 빌드 |
| [0007](adr/0007-github-actions-ci.md) | GitHub Actions 기반 CI/CD, Linux에서 이식 가능 모듈 검증 |
