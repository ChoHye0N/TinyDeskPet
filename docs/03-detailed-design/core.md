# core 모듈 상세 설계

| 항목 | 내용 |
|---|---|
| CMake 타깃 | `deskpet_core` (STATIC) |
| 네임스페이스 | `deskpet::core` |
| 의존 | C++ 표준 라이브러리만 |
| 관련 요구사항 | FR-30, FR-31, FR-32, NFR-PORT-01 |

## 1. 책임

모든 모듈이 공통으로 쓰는 **플랫폼 독립 기반 기능**을 제공합니다.

- 하는 일: 수학 타입, 입력 이벤트 정의, 시간 측정, 고정 시간 간격 계산, 로그, 설정 파싱, 버전 정보
- 하지 않는 일: OS API 호출(표준 라이브러리 파일 입출력 제외), 그래픽

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `src/core/Math.h` | `Vec2`, `PointI`, `SizeI`, `RectI` |
| `src/core/Math3D.h` | `Vec3`, `Vec4`, `Mat4`, 뷰·투영 행렬 (§3.8) |
| `src/core/Events.h` | 플랫폼이 앱에 전달하는 입력 이벤트 (`std::variant`) |
| `src/core/Clock.h/.cpp` | 단조 증가 시간 소스 (`TimeSource`) |
| `src/core/FixedTimestep.h/.cpp` | 고정 시간 간격 누적기 ([ADR-0004](../02-architecture/adr/0004-fixed-timestep-update.md)) |
| `src/core/Log.h/.cpp` | 레벨별 로그, 교체 가능한 출력 싱크 |
| `src/core/LogFile.h/.cpp` | 로그 파일: 이전 실행 보존(.1), 크기 한도 |
| `src/core/Config.h/.cpp` | `deskpet.ini` 파싱, 기본값, 검증 |
| `cmake/Version.h.in` → `build/.../generated/deskpet/Version.h` | 빌드 시 생성되는 버전 매크로 |

## 3. 공개 인터페이스

### 3.1 Math.h

```cpp
namespace deskpet::core {
struct Vec2   { float x = 0.0f; float y = 0.0f; /* +, -, * 연산자, length() */ };
struct PointI { int x = 0; int y = 0; };
struct SizeI  { int width = 0; int height = 0; };
struct RectI  { int left = 0; int top = 0; int right = 0; int bottom = 0;
                int width() const; int height() const; };
}
```

- `float` 좌표는 도메인(캐릭터 위치), `int` 좌표는 OS(창 위치, 픽셀) 용도입니다.
- `RectI`는 Win32 `RECT`와 같이 `right`, `bottom`을 **포함하지 않는** 반열린 구간입니다.

### 3.2 Events.h

```cpp
namespace deskpet::core {
enum class MouseButton : std::uint8_t { Left, Right, Middle };

struct PointerDownEvent    { Vec2 screen; MouseButton button; };
struct PointerUpEvent      { Vec2 screen; MouseButton button; };
struct PointerMoveEvent    { Vec2 screen; };
struct DoubleClickEvent    { Vec2 screen; MouseButton button; };
struct WorkAreaChangedEvent {};
struct QuitRequestedEvent  {};

using Event = std::variant<PointerDownEvent, PointerUpEvent, PointerMoveEvent,
                           DoubleClickEvent, WorkAreaChangedEvent, QuitRequestedEvent>;
}
```

- 모든 포인터 좌표는 **화면 좌표(물리 픽셀)** 입니다. 창이 움직여도 값이 흔들리지 않게 하기 위함입니다 ([ADR-0003](../02-architecture/adr/0003-manual-window-drag.md)).
- 이벤트를 추가할 때는 `std::variant`에 타입을 추가하고, `Application::handleEvent`의 `std::visit`에 처리를 추가합니다. 처리를 빠뜨리면 컴파일 오류가 나도록 `overloaded` 패턴을 사용합니다.

### 3.3 Clock.h

```cpp
namespace deskpet::core {
using TimeSource = std::function<double()>;   // 초 단위, 단조 증가
[[nodiscard]] TimeSource makeSteadyTimeSource(); // std::chrono::steady_clock 기반
}
```

- 시간을 함수로 주입받으면 테스트에서 "가짜 시간"을 넣어 결정적으로 검증할 수 있습니다.

### 3.4 FixedTimestep.h

```cpp
namespace deskpet::core {
class FixedTimestep {
public:
    explicit FixedTimestep(double stepSeconds = 1.0 / 60.0, int maxStepsPerFrame = 5);
    [[nodiscard]] int advance(double frameSeconds); // 이번 프레임에 실행할 업데이트 횟수
    [[nodiscard]] double step() const noexcept;
    [[nodiscard]] double alpha() const noexcept;    // 남은 누적 시간 / step (0 이상 1 미만)
    void reset() noexcept;
};
}
```

동작 규칙

| 입력 | 결과 |
|---|---|
| `frameSeconds`가 음수, NaN, 무한대 | 0으로 취급 |
| 누적 시간 ≥ step | `floor(누적 / step)`회 반환, 그만큼 누적에서 차감 |
| 계산된 횟수 > `maxStepsPerFrame` | `maxStepsPerFrame` 반환, **남은 누적 시간은 버림** (죽음의 나선 방지) |

### 3.5 Log.h

```cpp
namespace deskpet::core::logging {
enum class Level : std::uint8_t { Trace, Debug, Info, Warn, Error, Off };
using Sink = std::function<void(Level, std::string_view line)>;

void setLevel(Level level);
[[nodiscard]] Level level();
[[nodiscard]] bool isEnabled(Level level);
void setSink(Sink sink);                       // nullptr이면 기본 싱크(stderr)
void write(Level level, std::string_view message);
[[nodiscard]] std::string_view toString(Level level);
[[nodiscard]] std::optional<Level> parseLevel(std::string_view text); // "info", "WARN" 등
[[nodiscard]] std::string formatLine(Level level, std::string_view message); // "[  1.234] [INFO ] ..."

template <class... Args> void trace(std::format_string<Args...> fmt, Args&&... args);
template <class... Args> void debug(...); /* info, warn, error 동일 */
}
```

- 레벨 비교를 먼저 하고 통과한 경우에만 `std::format`을 호출합니다. 꺼진 레벨의 로그는 문자열 생성 비용이 없습니다.
- `write`는 내부 뮤텍스로 보호되어 여러 스레드에서 호출해도 안전합니다 (M3 로딩 스레드 대비).
- 싱크는 `main`이 정합니다. Windows에서는 `OutputDebugStringW` + `deskpet.log` 파일입니다.

`LogFile` (로그 파일 회전·한도)

```cpp
class LogFile {
public:
    static constexpr std::string_view kTruncatedNotice;  // 한도 도달 시 마지막 줄
    [[nodiscard]] bool open(const std::filesystem::path& path, std::uintmax_t maxBytes);
    void write(std::string_view line);   // 한 줄 + flush. 한도를 넘기면 안내 한 줄 후 무시
};
```

- `open`: 기존 파일을 `<이름>.1`로 옮김(기존 `.1`은 덮어씀) → 새로 엶. 직전 실행의 로그 하나를 보존합니다.
- 한도는 실행 중 크기 기준(4MB, `main_win32.cpp`). 대기 중에는 로그가 거의 없어 정상 사용에서는 닿지 않습니다.

### 3.6 Config.h

```cpp
namespace deskpet::core {
struct WindowConfig    { int width = 200; int height = 200; int marginRight = 40; bool alwaysOnTop = true; };
struct CharacterConfig { float gravity = 2400.0f; float jumpSpeed = 900.0f;
                         float dragThreshold = 4.0f; float maxFallSpeed = 4000.0f; };
struct RendererConfig  { bool vsync = true; bool debugLayer = /* Debug 빌드면 true */; };
struct ModelConfig     { std::string path; };   // 비면 슬라임. 상대 경로는 실행 파일 폴더 기준
struct AnimationConfig { bool idleMotion = true; };  // 대기 숨쉬기 동작. 끄면 대기 중 Present 생략
struct StateConfig     { std::optional<int> lastX, lastY;      // 프로그램이 기록하는 마지막 발 위치
                         std::optional<PointI> lastPosition() const; };  // 둘 다 있을 때만
struct LogConfig       { logging::Level level = logging::Level::Info; bool toFile = true; };
struct AppConfig       { WindowConfig window; CharacterConfig character;
                         RendererConfig renderer; ModelConfig model; LogConfig log;
                         StateConfig state; };

struct ConfigLoadResult { AppConfig config; std::vector<std::string> warnings; };

[[nodiscard]] ConfigLoadResult parseConfig(std::string_view text);
[[nodiscard]] ConfigLoadResult loadConfigFile(const std::filesystem::path& path);

// 쓰기: 주석·순서·줄바꿈(LF/CRLF) 유지. 키가 없으면 섹션 끝에, 섹션이 없으면 파일 끝에 추가
[[nodiscard]] std::string setIniValue(std::string_view text, std::string_view section,
                                      std::string_view key, std::string_view value);
struct IniValue { std::string section, key, value; };
[[nodiscard]] bool saveConfigValues(const std::filesystem::path& path, const std::vector<IniValue>& values);
}
```

파일 형식 (INI)

```ini
# 주석은 # 또는 ; 로 시작
[window]
width = 200            ; 32 ~ 2048
height = 200           ; 32 ~ 2048
margin_right = 40      ; 0 ~ 10000
always_on_top = true   ; true/false/1/0/yes/no/on/off

[character]
gravity = 2400         ; px/s², 1 ~ 100000
jump_speed = 900       ; px/s, 0 ~ 100000
drag_threshold = 4     ; px, 0 ~ 100
max_fall_speed = 4000  ; px/s, 1 ~ 100000

[renderer]
vsync = true
debug_layer = false
msaa = 4            ; 1(끔), 2, 4, 8. 그 외 값은 경고 후 기본값
outline = model     ; 외곽선: model(모델 지정 재질만) / all(모든 불투명 재질) / off

[model]
path = models/zmd_EM.vrm     ; 문자열 그대로 (검증은 로딩 시점에)

[state]                      ; 프로그램이 종료할 때 기록
last_x = 1720                ; -100000 ~ 100000
last_y = 1032

[log]
level = info           ; trace/debug/info/warn/error/off
to_file = true
```

파싱 규칙

| 상황 | 처리 |
|---|---|
| 파일 없음 | 기본값 + 경고 1개 (`"설정 파일 없음: ... 기본값 사용"`) |
| 빈 줄, `#`/`;`로 시작하는 줄 | 무시 |
| 값 뒤의 `;`/`#` 주석 | 제거 후 해석 |
| 앞뒤 공백 | 제거. 섹션·키 이름은 대소문자 구분 안 함 |
| `=`가 없는 줄, 닫히지 않은 `[` | 경고 후 무시 (`"N행: 형식 오류"`) |
| 알 수 없는 섹션/키 | 경고 후 무시 |
| 숫자 변환 실패, 범위 밖 값 | 경고 후 **해당 키만** 기본값 유지 |
| 같은 키 중복 | 마지막 값 사용 |

- 숫자 변환은 로케일 영향을 받지 않는 `std::from_chars`를 사용합니다.
- 파서는 문자열만 받으므로(`parseConfig`) 파일 없이 테스트할 수 있습니다.

### 3.7 Version.h (생성 파일)

```cpp
#define DESKPET_VERSION_MAJOR 0
#define DESKPET_VERSION_MINOR 1
#define DESKPET_VERSION_PATCH 0
#define DESKPET_VERSION_STRING "0.1.0"
#define DESKPET_GIT_HASH "a1b2c3d"     // 저장소가 아니면 "unknown", 수정 사항이 있으면 "-dirty"
```

- 버전의 원천은 저장소 루트의 `VERSION` 파일 하나입니다. CMake가 읽어 `project(VERSION ...)`, 이 헤더, 실행 파일 리소스, 패키지 이름에 모두 반영합니다.
- Git 해시는 **CMake 구성(configure) 시점**에 기록됩니다. 커밋 후 재구성하지 않으면 이전 해시가 남는다는 점에 유의합니다 (CI는 항상 새로 구성하므로 정확함).

### 3.8 Math3D.h

```cpp
namespace deskpet::core {
struct Vec3 { float x, y, z; /* +, -, *, length() */ };
struct Vec4 { float x, y, z, w; };
struct Mat4 { std::array<std::array<float, 4>, 4> m;   // m[행][열]
              static Mat4 identity(), translation(Vec3), rotationY(float), fromColumnMajor(const float*);
              Mat4 operator*(const Mat4&) const; /* 스칼라 곱, 덧셈 */ };
Vec4 transform(Vec3 p, const Mat4&);          // (p, 1) × M
Vec3 transformPoint(Vec3, const Mat4&);       // 아핀 전용
Vec3 transformDirection(Vec3, const Mat4&);   // 이동 무시
Mat4 lookAtRH(Vec3 eye, Vec3 target, Vec3 up);
Mat4 perspectiveFovRH(float fovY, float aspect, float nearZ, float farZ);  // 깊이 0~1
}
```

`Quat` (애니메이션, ADR-0010): `axisAngle`, `fromTo`(정반대 방향은 수직 축으로 180°), `normalized`, 해밀턴 곱 `a * b`(**b 먼저**), `rotate(v)`, `toMat4()`(행 벡터 규약: `p * M == q.rotate(p)`), 자유 함수 `slerp(a, b, t)`(내적이 음수면 b 부호를 뒤집어 짧은 경로, 거의 같으면 선형 보간 후 정규화). 쿼터니언 곱과 행렬 곱은 적용 순서가 반대이니 주의.

| 규약 | 내용 |
|---|---|
| 벡터 | **행 벡터** `p' = p × M` (DirectXMath와 같음). `A * B` = A 다음 B |
| 저장 | 행 우선. 이동은 `m[3][0..2]`. HLSL에서는 `row_major`로 선언해 그대로 업로드 |
| 좌표계 | 오른손, +Y 위 (glTF와 같음). 투영 후 깊이 0(near) ~ 1(far) |
| glTF 행렬 | 열 우선 `float[16]`을 그대로 복사하면 이 규약의 행렬 (`fromColumnMajor`) |

## 4. 테스트 항목

| 테스트 파일 | 검증 내용 |
|---|---|
| `tests/core/FixedTimestepTests.cpp` | 정확히 1스텝, 누적 후 스텝, 최대 스텝 제한과 누적 버림, 음수·NaN 입력, `alpha()` 범위 |
| `tests/core/ConfigTests.cpp` | 기본값, 정상 파싱, 주석·공백, 대소문자, 알 수 없는 키 경고, 범위 밖 값, 형식 오류 행 번호, 불리언 표기, 파일 없음, 모델 경로, 마지막 위치, `setIniValue`(값 교체·주석 유지, 섹션 끝 추가, 새 섹션, CRLF 유지, 다시 파싱) |
| `tests/core/LogFileTests.cpp` | 이전 로그 `.1` 보존, 크기 한도와 안내 문구, 열기 실패 시 안전 |
| `tests/core/LogTests.cpp` | 레벨 필터링, 싱크 교체, 레벨 문자열 변환, 포맷 결과 |
| `tests/core/MathTests.cpp` | 벡터 연산, 사각형 크기 |
| `tests/core/Math3DTests.cpp` | 이동·회전 합성 순서, glTF 행렬 변환, lookAt, 투영 깊이 범위 |

## 5. 확장 지점

| TODO | 내용 |
|---|---|
| ~~`TODO(M1)`~~ | ✅ 설정 저장(`setIniValue`, `saveConfigValues`) — 마지막 위치 |
| ~~`TODO(M1)`~~ | ✅ 로그 파일 크기 제한과 회전 (`LogFile`) |
| ~~`TODO(M3)`~~ | ✅ M1a: `[model] path` 설정 키 |
