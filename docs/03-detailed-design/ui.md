# ui 모듈 상세 설계

| 항목 | 내용 |
|---|---|
| 상위 문서 | [SAD](../02-architecture/SAD.md), [ADR-0016](../02-architecture/adr/0016-imgui-settings-window.md) |
| 플랫폼 | 독립 (Linux에서도 빌드·테스트) |
| CMake 타깃 | `deskpet_ui`, `deskpet_imgui` (ImGui 본체), `deskpet_imgui_dx11` (D3D11 백엔드, Windows) |

## 1. 책임

- 하는 일: Dear ImGui 컨텍스트 소유, 설정 창 한 프레임 만들기(`ImDrawData`), 마우스 입력 받기, 테마·아이콘
- 하지 않는 일: 앱 상태 바꾸기·설정 파일 쓰기(`app`이 `SettingsAction`을 처리), GPU 그리기(렌더러의 `ImGuiPass`), 키보드 입력(오버레이 창은 포커스를 받지 않음)

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `src/ui/SettingsUi.h`, `.cpp` | 설정 창: 상태(`SettingsState`) → 화면, 사용자가 한 일(`SettingsAction`) |
| `src/ui/Theme.h`, `.cpp` | 모서리·여백·색 (`applyTheme`), 직접 그리는 위젯의 색 (`palette`) — 모듈 내부 |
| `src/ui/Icons.h`, `.cpp` | 선 아이콘 (발바닥, 닫기, 사람, 재생, 모니터, 정보, 폴더, 펼침) — 모듈 내부 |
| `src/ui/CMakeLists.txt` | ImGui v1.92.9b FetchContent, `deskpet_imgui`·`deskpet_imgui_dx11`·`deskpet_ui` |

## 3. 공개 인터페이스

```cpp
namespace deskpet::ui {
struct ClipChoice { std::string label; std::string value; };  // value "" = 기본 동작
struct MotionSetting {
    std::string name; std::string current; std::vector<ClipChoice> choices;
    float duration, loopStart;  // 지금 클립 길이(0이면 클립 없음 → 반복 시작 슬라이더 없음), 반복 시작 (s)
};
struct MovementSetting { int gravity, walkSpeed, dragThreshold; };  // 고급 (px 단위)
struct SettingsState {  // 앱이 매 프레임 채움
    std::string version;
    bool modelLoading; std::string modelName, modelAuthor, modelLicense, modelFile;
    std::array<MotionSetting, 5> motions;  // 대기, 걷기, 끌려 다닐 때, 공중, 상호작용
    int scalePercent, minScalePercent, maxScalePercent, scaleStepPercent;
    bool walk, walkOnWindows, lookAt, idleMotion, alwaysOnTop;
    bool vsync; int msaa; core::OutlineMode outline;  // 그래픽
    MovementSetting movement;                        // 고급
};
struct SettingsAction {
    enum class Kind : std::uint8_t {
        OpenModel, SetClip, SetScale, SetLookAt, SetWalkOnWindows, SetIdleMotion,
        SetLoopStart, SetWalk, SetAlwaysOnTop, SetVsync, SetMsaa, SetOutline, SetTexturePadding, SetOutlineScale,
        SetGravity, SetWalkSpeed, SetDragThreshold, ResetMovement };
    Kind kind; std::size_t motion; std::string text; int number; bool flag; float value;
};
enum class SettingsTab : std::uint8_t { Model, Motion, Display, Graphics, Advanced, About };

class SettingsUi {
public:
    void setFont(const std::filesystem::path& file);  // TrueType, 없으면 ImGui 내장 글꼴
    void setDarkTheme(bool dark);
    void setScale(float scale);                       // DPI 배율
    void open(core::RectI avoid);                     // 캐릭터 영역 옆에 (오버레이 px)
    void close() noexcept;
    bool isOpen() const noexcept;
    void setMousePosition(core::Vec2 position);       // 오버레이 px, 매 프레임
    void setMouseButton(bool leftDown);
    void addMouseWheel(float notches);
    bool wantsMouse() const noexcept;                 // 직전 프레임 기준
    ImDrawData* frame(float dt, core::SizeI display, const SettingsState& state,
                      std::vector<SettingsAction>& actions);  // 닫혀 있으면 nullptr
    core::RectI windowRect() const noexcept;          // 테스트·디버그용
};
}
```

## 4. 동작 명세

### 4.1 컨텍스트와 입력

- 생성자에서 `ImGui::CreateContext`, `io.IniFilename = nullptr`(창 위치를 imgui.ini에 저장하지 않음), `io.BackendFlags |= RendererHasTextures`(렌더러 백엔드가 초기화할 때 같은 값을 다시 씀. 백엔드가 없는 테스트에서는 텍스처 요청이 처리되지 않고 남을 뿐). 소멸자에서 `DestroyContext`. 앱은 이 객체를 렌더러보다 먼저 선언해 나중에 파괴 (백엔드가 컨텍스트를 씀)
- 입력은 닫혀 있으면 버림. `open`에서 남은 입력 큐를 비우고 버튼을 뗀 상태로 시작
- `wantsMouse` = 프레임 끝의 `io.WantCaptureMouse` (커서가 창·콤보 목록 위, 또는 창 위젯을 누르는 중). 닫히면 false: 닫는 프레임은 커서가 닫기 버튼 위라 참인데, 닫힌 뒤에는 프레임을 돌리지 않아 그 값이 남으면 앱이 클릭 통과를 계속 꺼 화면 전체가 클릭을 받음

### 4.2 창 배치와 모양

- 크기 540×360 (96 DPI 기준 × 배율), 고정. 제목 줄 44, 왼쪽 탭 열 120, 이름 열 108
- 처음 열 때: `avoid` 왼쪽 24px 밖, 자리가 없으면 오른쪽. 세로는 `avoid` 가운데. 화면 가장자리 8px 안으로
- 창 플래그: 제목 표시줄·크기 조절·저장·스크롤 없음, `NoMove` — 제목 줄을 직접 그리고, 제목 줄(닫기 버튼 제외)을 끌 때 `SetWindowPos`로 옮김
- 제목 줄: 발바닥 아이콘(강조색), "TinyDeskPet 설정", 닫기(마우스를 올리면 둥근 바탕). 아래·탭 열 오른쪽에 1px 테두리 선
- 탭: 높이 34, 간격 4, 아이콘 + 글자. 선택 = `accentSoft` 바탕 + `accentText`, 올림 = `frameHover`
- 위젯: 콤보는 기본 화살표 버튼 대신 펼침 선 아이콘, 슬라이더는 얇은 트랙(4px) + 원 손잡이(지름 16, step 단위), 토글은 36×20 알약 + 흰 손잡이

### 4.3 테마 (`Theme.cpp`)

| 항목 | 값 |
|---|---|
| 모서리 | 창 12, 입력·팝업·스크롤 8 |
| 테두리 | 창·팝업·입력 1px, 그림자 없음 |
| 여백 | 창 16×14, 입력 10×6, 항목 간격 10 |
| 글자 | 15px (이름 18px), 시스템 맑은 고딕 |
| 밝은 색 | 바탕 #FFFFFF, 글자 #2C2C2A, 보조 #6B6A65, 테두리 #E3E1DA, 올림 #ECEAE4, 강조 #D4537E, 선택 바탕 #FBEAF0 / 글자 #72243E |
| 어두운 색 | 바탕 #232321, 글자 #EDECE8, 보조 #A8A69E, 테두리 #3A3936, 올림 #363532, 강조 #E07A9E, 선택 바탕 #4B1528 / 글자 #F4C0D1 |

### 4.4 탭 내용

| 탭 | 내용 | 동작 |
|---|---|---|
| 모델 | 이름(없으면 "모델 없음", 읽는 중이면 "불러오는 중…"), 작가, 라이선스, 파일 | "모델 열기…" → `OpenModel` |
| 동작 | 대기·걷기·끌려 다닐 때·공중·상호작용 콤보 (상호작용의 기본 동작은 "점프 (기본 동작)", 반복 시작 슬라이더 없음) (선택지 = 앱이 준 `choices`). 클립이 있는 동작은 아래에 반복 시작 슬라이더 (0 ~ 클립 길이, 0.1초 단위). 넘치면 휠로 스크롤 | 다른 항목 선택 → `SetClip(motion, value)`, 슬라이더 → `SetLoopStart(motion, value)` |
| 화면 | 크기 슬라이더(최소~최대, 단계), 토글 5개(걷기, 다른 창 위를 걷기, 커서 바라보기, 대기 중 숨쉬기, 다른 창보다 항상 위에) | `SetScale`, `SetWalk`, `SetWalkOnWindows`, `SetLookAt`, `SetIdleMotion`, `SetAlwaysOnTop` |
| 그래픽 | 수직 동기화 토글, 안티에일리어싱(끔·2x·4x·8x)·외곽선(자동·모델 지정·모든 재질·외곽만·끔, 버튼 순서는 열거형 순서와 달라 표로 맞춤)·텍스처 여백 채우기(자동·모두 채움·끔)는 붙은 선택 버튼, 외곽선 두께 슬라이더(0~300%, 10% 단위, 외곽선을 끄면 비활성). 설명은 각 설정 바로 아래 한 줄씩(`hint`) | `SetVsync`, `SetMsaa(샘플 수)`, `SetOutline(core::OutlineMode)`, `SetOutlineScale(%)`, `SetTexturePadding(core::TexturePadding)` |
| 고급 | 중력(500~6000, 100), 걷기 속도(20~200, 5), 끌기 시작 거리(0~20, 1) 슬라이더, "기본값으로" | `SetGravity` 등 (`number`), `ResetMovement` |
| 정보 | 버전, 소개, 저장소, 설정 저장 위치 | — |

## 5. 테스트 항목

`tests/ui/SettingsUiTests.cpp` — 렌더러 없이 ImGui를 돌림 (내장 글꼴)

| 테스트 | 검증 |
|---|---|
| `Closed_DrawsNothingAndIgnoresMouse` | 닫혀 있으면 nullptr, 마우스를 가로채지 않음 |
| `Open_IsPlacedBesideTheCharacterInsideTheScreen` | 캐릭터 왼쪽, 화면 안 |
| `NearTheLeftEdge_IsPlacedRightOfTheCharacter` | 왼쪽에 자리가 없으면 오른쪽 |
| `MouseOverWindow_IsCapturedOnlyThere` | 창 위에서만 `wantsMouse` |
| `CloseButton_ClosesWindow` | 닫기 클릭 → 닫힘, 마우스를 놓아줌(`wantsMouse` false), 다음 프레임 nullptr |
| `ClickingTab_SelectsIt` | 세 번째 탭 클릭 → 화면 탭 |
| `DraggingTitle_MovesWindow` | 제목 줄 끌기 → 창이 같은 만큼 이동 |
| `ClickingGraphicsAndAdvancedTabs_SelectsThem` | 네 번째·다섯 번째 탭 클릭 → 그래픽·고급 |
| `EveryTab_DrawsWithClipLoopSliders` | 모든 탭이 그려지고(ImGui 단언 없음), 클릭 없이는 동작 없음 |
| `GraphicsTab_VsyncToggleEmitsAction` | 수직 동기화 스위치 클릭 → `SetVsync(false)` |

앱 쪽 처리(적용·저장·입력 가로채기)는 `ApplicationTests`(app.md §5)

## 6. 확장 지점

| 항목 | 내용 |
|---|---|
| `TODO` | 다른 모션 파일 고르기 (파일 대화상자) |
