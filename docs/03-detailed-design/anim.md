# anim 모듈 상세 설계

| 항목 | 내용 |
|---|---|
| CMake 타깃 | `deskpet_anim` (STATIC) |
| 네임스페이스 | `deskpet::anim` |
| 의존 | `deskpet_model` (→ `deskpet_core`) |
| 관련 | [ADR-0010](../02-architecture/adr/0010-procedural-animation.md), [ADR-0009](../02-architecture/adr/0009-multiple-model-formats.md) |

## 1. 책임

- 뼈대 정방향 운동학(FK)으로 본별 스킨 행렬을 계산합니다.
- 캐릭터 동작 상태를 휴머노이드 본 회전과 표정 가중치로 바꿉니다 (코드로 계산, 동작별로 모션 파일 재생 가능).
- 모션 클립(model::MotionClip)을 대상 모델의 뼈대에 맞춰 재생합니다 (리타기팅, §4.5).
- 하지 않는 일: GPU 업로드(renderer), 캐릭터 상태 결정(character), 모션 파일 읽기(model).
- 플랫폼 독립. Linux CI에서 테스트됩니다.

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `src/anim/Skeleton.h/.cpp` | 본 계층 정리(부모 먼저 순서), FK, 스킨 행렬 |
| `src/anim/ProceduralAnimator.h/.cpp` | `Motion` → 본 회전·표정, 표시용 경계 상자 |
| `src/anim/SpringBone.h/.cpp` | 머리카락·옷 흔들림 (VRM SpringBone, §4.4) |
| `src/anim/MotionPlayer.h/.cpp` | 모션 클립 재생·리타기팅 (§4.5, ADR-0013) |
| `src/core/Math3D.h` | `Quat` (해밀턴 곱, `axisAngle`, `fromTo`, `toMat4`) |

## 3. 공개 인터페이스

```cpp
namespace deskpet::anim {
class Skeleton {
public:
    explicit Skeleton(const model::Model& model);
    std::size_t size() const;
    int find(model::HumanBone bone) const;          // 없으면 -1
    core::Vec3 bindPosition(int bone) const;
    // hidden[본] ≠ 0이면 그 본과 아래 본을 크기 0으로 접음 (클립의 숨김 본, ADR-0015).
    // offsets[본]: 기본 자세 모델 축의 이동, Pⱼ = P부모 + A부모(pⱼ − p부모 + offsetⱼ)
    void computeSkinMatrices(std::span<const core::Quat> rotations, std::vector<core::Mat4>& out,
                             core::Vec3 rootOffset = {}, std::span<const std::uint8_t> hidden = {},
                             std::span<const core::Vec3> offsets = {}) const;
};

enum class Motion : std::uint8_t { Idle, Walk, Dragged, Airborne, Interact };  // Interact: 더블클릭 상호작용 (클립이 있을 때만, 없으면 대기 자세)
struct AnimationInput { Motion motion; float time; float squash; bool blink; bool idleMotion = true;
                        core::Vec3 movement; /* 화면 이동 (m), 흔들림 관성 */ };
struct AnimationOutput { std::vector<core::Mat4> skin; std::array<float, Expression::Count> expressions; };

class ProceduralAnimator {
public:
    explicit ProceduralAnimator(const model::Model& model);  // model은 animator보다 오래 살아야 함
    void setClip(Motion motion, std::shared_ptr<const model::MotionClip> clip);  // nullptr = 코드 동작
    static constexpr float kTransitionSeconds = 0.2f;
    void evaluate(const AnimationInput& input, AnimationOutput& out) const;  // 입력만으로 정해지는 자세
    void animate(const AnimationInput& input, float dt, AnimationOutput& out);  // 전환 보간 포함 (매 프레임)
    model::Bounds displayBounds(float maxTurnRadians = 0) const;  // 모든 동작 자세를 담는 경계 상자
    std::array<model::Bounds, kMotionCount> motionBounds(float maxTurnRadians = 0) const;  // 동작별
    model::Bounds standingBounds() const;  // 화면 크기 기준: 클립 없이 코드로 만든 대기 자세
};
}
```

## 4. 동작 명세

### 4.1 회전 규약과 FK

| 기호 | 의미 |
|---|---|
| `pⱼ` | 바인드 포즈 관절 위치 (모델 공간, `Bone::position`) |
| `Δⱼ` | 본 j의 델타 회전 — **모델 공간 축 기준**, 부모 자세가 적용된 뒤에 더해짐 |
| `Aⱼ` | 누적 회전 = `Δⱼ × A부모` (해밀턴 곱: 오른쪽이 먼저) |
| `Pⱼ` | 자세 적용 후 관절 위치 = `P부모 + A부모(pⱼ − p부모)`, 루트는 `pⱼ` |
| `Sⱼ` | 스킨 행렬 = `T(−pⱼ) · R(Aⱼ) · T(Pⱼ)` (행 벡터 규약) |

- 계산 순서는 루트까지의 깊이로 정렬해 부모가 항상 먼저 오게 합니다 (본 배열 순서와 무관). 잘못된 부모 번호(범위 밖, 자기 자신)는 루트로 취급합니다.
- 원본 본의 로컬 축 방향을 쓰지 않으므로 VRM·PMX·FBX에 같은 회전이 같은 결과를 냅니다.

### 4.2 자세 (`ProceduralAnimator`)

| 동작 | 팔 (벌림 / 앞뒤 흔들기 / 팔꿈치) | 다리 (허벅지 / 무릎) | 몸통·머리 | 표정 |
|---|---|---|---|---|
| Idle | 10° / ±1.5° (숨쉬기 2.4초) / 10° | — | 가슴 ±1.2° 앞뒤, 머리 ±2° 기울임 (5초) | 깜빡임은 캐릭터 타이밍 |
| Walk (0.6초 주기) | 10° / ∓18° (같은 쪽 다리와 반대) / 20° | ±25° 교차 / 앞으로 넘어올 때 최대 30° | 몸통 ±4° 비틀기 | 기쁨 0.6 |
| Dragged (0.9초 주기) | 35° / ±10° 교차 | ±12° 교차 / 15° | 머리 ±6° | 놀람 1 |
| Airborne | 45° / 0 | 15° 앞 / 35° | — | 놀람 1 |
| 착지 웅크림 (§4.2b) | 바깥으로 +12°×c | 무릎 +50°×c, 허벅지 앞으로 그 절반 | 가슴 +15°×c 숙임, 머리 −7.5°×c | — |

- **팔 내리기**: 목표 방향 = `(side·sin벌림, −cos벌림, 0)`을 X축으로 흔들기만큼 회전. 위팔 회전 = `fromTo(바인드 방향, 목표)`. `side`는 바인드 방향의 x 부호(캐릭터 왼쪽 = +X).
- 회전 방향: 허벅지 `X축 −각도` = 발이 앞(+Z), 무릎 `X축 +각도` = 발이 뒤, 팔꿈치 `X축 −각도` = 손이 앞.
- 가슴 본이 없으면 척추 본을 씁니다. 필요한 본이 없으면 그 부위만 움직이지 않습니다.
- `idleMotion = false`면 Idle 자세가 시간과 무관해져, 장면이 같아지므로 렌더러가 Present를 생략합니다.
- 깜빡임 가중치 = `1 − 놀람` (완전히 놀라면 깜빡이지 않음).

### 4.2a 상태 전환 보간 (`animate`)

- 동작(`Motion`)이 바뀌면 **직전 프레임에 그린 자세**를 저장하고, `kTransitionSeconds`(0.2초) 동안 본마다 `slerp(이전, 새 자세, w)`로 섞습니다. `w = smoothstep(경과/0.2)` — 시작·끝에서 속도 0.
- 표정(기쁨·놀람)도 같은 `w`로 선형 보간합니다.
- 전환 도중 다시 바뀌어도 섞인 자세에서 출발하므로 튀지 않습니다. 이전 자세는 멈춘 상태로 쓰는데(걷던 다리가 그 자리에서 사라짐), 0.2초라 눈에 띄지 않습니다.
- **착지 웅크림은 보간 뒤에 더합니다.** Airborne→Idle 전환과 같은 프레임에 시작하므로, 함께 섞으면 약해집니다. 보간 출발점으로 저장하는 자세에는 웅크림을 빼서 두 번 더해지지 않게 합니다.
- 첫 프레임은 넘어올 자세가 없어 그대로 그립니다. 전환이 끝나면 보간을 건너뛰므로 대기 자세가 정지하면 다시 Present 생략이 됩니다.
- `dt`는 앱이 이번 프레임에 진행한 고정 스텝 시간(`steps × 1/60`)을 넘깁니다. `evaluate`(상태 없음)는 `displayBounds`와 테스트에서 씁니다.

### 4.2b 착지와 발 고정

- **웅크림 곡선**: 캐릭터의 `squash`(착지 순간 0.18에서 0.18초 동안 직선 감소)는 사람 동작에는 짧고 갑작스러워서, **커지는 순간을 시작 신호로만** 씁니다. `animate`가 착지 후 시간을 재어 `c = 0.07초 동안 smoothstep으로 0→1, 이후 0.38초 동안 1→0` (세기 = squash/0.18). `evaluate`(상태 없음)는 `c = squash/0.18`.
- **관절로 웅크림** (`applyCrouch`): 무릎을 굽히고 허벅지를 그 절반만큼 앞으로 → 정강이가 반대로 같은 각도만큼 기울어 발이 엉덩이 바로 아래(허벅지·정강이 길이가 비슷할 때). 예전처럼 몸 전체를 납작하게 늘이면(슬라임 방식) 사람 모델은 비율이 깨지고, 엉덩이를 고정한 채 무릎만 굽히면 발이 뒤로 들립니다.
- **발 고정** (`rootOffset(motion)`): Idle·Walk에서는 FK로 발 위치를 구해 **낮은 쪽 발이 바인드 포즈 발 높이**에 오도록 루트를 내립니다(`Skeleton::computePose`의 `rootOffset`, 올리지는 않음). 그래서 웅크리면 몸이 내려가고, 걸을 때는 다리 흔들림에 맞춰 몸이 오르내립니다. 같은 `rootOffset`을 SpringBone에도 넘겨 몸이 내려가면 머리카락이 관성으로 반응합니다.

### 4.3 표시용 경계 상자 (`displayBounds`)

`displayBounds(maxTurnRadians)` — 화면에 실제로 그려질 수 있는 모든 모습을 담는 경계 상자입니다. T포즈 경계를 쓰면 팔 폭 때문에 캐릭터가 작게 보이고, 일부 자세만 쓰면 빠진 자세에서 창 밖으로 잘립니다.

- **자세**: Idle·Dragged·Airborne + **Walk 한 주기를 8등분**(다리를 가장 멀리 뻗는 1/4·3/4, 무릎을 가장 굽히는 0·1/2 포함). 정점은 CPU에서 스키닝합니다.
- **몸 돌리기**: 앱은 걸을 때 몸을 Y축으로 최대 ±50° 돌려 그립니다. 그러면 정면에선 깊이(Z)였던 꼬리·치마·뒤로 뻗은 머리카락이 화면 가로(X)로 나옵니다. 각 정점에 대해 θ ∈ [−T, T]에서 `x' = x·cos θ + z·sin θ`의 범위를 **해석적으로** 구합니다: `= r·cos(θ − φ)`이므로 양 끝값과, φ가 구간 안이면 ±r. 양 끝 각도만 샘플링하면 비스듬히 뒤로 뻗은 점의 최대(θ = φ)를 놓칩니다. z'도 같은 방법.
- 스킨 가중치가 있는 정점이 하나도 없으면 원래 `Model::bounds`(돌릴 각도가 있으면 그 꼭짓점 8개를 돌려서)를 씁니다.
- 몸 돌리기(`maxTurnRadians`)는 **걷기 상자에만** 넣습니다. 앱은 걸을 때만 몸을 돌리고, 걷다 멈춘 뒤 돌아오는 0.2초는 앱이 벽을 천천히 옮겨 처리합니다. 다른 동작까지 넓히면 화면 끝에 서 있을 때 그만큼 떠 보입니다.
- `motionBounds`: 같은 샘플을 동작별로 나눠 담은 상자 (`displayBounds` = 이것들의 합). 앱이 지금 동작의 상자를 3D 그리기 영역에 씀.
- `screenXRange(viewProjection, maxTurn)`: 같은 샘플의 **실제 정점**을 카메라로 투영한 동작별 화면 x 범위(NDC). 걷기는 몸 돌림을 7개 각도로 나눠 잼(놓치는 최대는 반지름의 1.4% 안). 앱이 벽 계산에 씀 — 상자 꼭짓점을 투영하면 앞쪽 모서리(가장 넓은 x와 가장 앞의 z가 만나는 빈 구석)가 원근으로 넓게 잡혀 화면 끝에서 떠 보임
- `poseExtents(viewProjection, maxTurn)`: `motionBounds` + `screenXRange`를 **한 번의 스키닝**으로 (자세마다 정점을 한 번만 CPU 스키닝, 걷기 회전 각도의 cos·sin은 미리, 투영은 클립 좌표의 x·w만). 앱은 서 있는 자세로 카메라를 먼저 맞춘 뒤 이것 하나만 부름 (카메라의 가로 투영은 서 있는 자세와 화면비로만 정해짐). 측정(Michiru, 정점 4만, 클립 3개): Debug 2.57 → 1.29초, Release 155 → 83ms
- `standingBounds`: **클립을 붙이지 않은** 새 애니메이터로 계산한 대기 자세(숨쉬기 없음, 팔 내림)의 상자. 클립과 무관해서, 앱이 화면 크기 기준으로 쓰면 클립을 바꿔도 캐릭터 크기가 그대로입니다. 스킨이 없는 모델은 `Model::bounds`. **아래(min.y)는 발바닥**: 발·발가락 본(또는 그 자손)에 가장 많이 붙은 정점 중 가장 낮은 높이. 발보다 낮게 늘어진 꼬리·머리카락·망토 끝을 바닥으로 삼으면 서 있을 때 그만큼 떠 보이기 때문(예: 꼬리 끝이 발보다 9.5cm 아래인 모델). 발 본이 없으면 가장 낮은 정점.

### 4.4 흔들림 (`SpringBoneSimulator`)

UniVRM `VRMSpringBone`과 같은 방식입니다. 데이터는 모델의 `springGroups`·`springColliders`(VRM 0.x `secondaryAnimation`, VRM 1.0 `VRMC_springBone`)를 그대로 써서, 모델마다 제작자가 정한 대로 흔들립니다.

- **관절 (VRM 0.x, `roots`)**: 그룹의 루트 본부터 자손 전체(전위 순회 → 부모가 먼저). 본의 끝(tail)은 첫 자식 위치, 자식이 없으면 부모 → 본 방향으로 7cm.
- **관절 (VRM 1.0, `joints`)**: 적힌 순서(루트 → 끝)의 본만. 본의 끝은 다음 관절 위치이고, 마지막 관절은 앞 본의 끝 위치만 줌(스스로 흔들리지 않고, 그 아래 본도 따라만 감). 관절마다 다른 값은 로더가 그룹을 나눠 둠 ([model.md](model.md) 흔들림 정보). 여러 그룹에 같은 본이 있으면 처음 그룹만.
- **한 단계** (`dt` = 1/60초, 프레임이 밀리면 최대 4단계로 나눔):
  1. 몸 자세(절차적 동작 + 착지 반동)로 FK → 부모 회전 `A부모`, 머리 위치 `head`
  2. 원래 방향 `rest = (Δⱼ·A부모)(축)`
  3. `next = cur + (cur − prev)(1 − drag) + rest·stiffness·dt + 중력방향·gravityPower·dt`
  4. 본 길이로 되돌림 → 그룹의 충돌체(반지름 + hitRadius) 안이면 표면으로 밀고 다시 길이 유지. 구는 중심 `P본 + A본(offset)`, 캡슐은 `P본 + A본(offset)` ~ `P본 + A본(tail)` 선분에서 `next`에 가장 가까운 점을 중심으로 봄
  5. `Δⱼ ← fromTo(rest, next − head) · Δⱼ`, 이 본의 `A`·`P`를 갱신해 자식 계산에 사용
- **관성 (`AnimationInput::movement`)**: 캐릭터가 화면에서 움직인 거리(m). 모델 공간에서 계산하므로 이전 끝 위치들을 `−movement`만큼 옮겨 관성을 만듭니다. 앱이 창 이동 px × (m/px)를 걸을 때 돌린 몸의 반대로 돌려서 넘깁니다 → 앞으로 걸으면 머리카락이 뒤로 날림.
- **흔들림 범위 제한**: 길이 유지 뒤, 끝 방향이 **몸 자세 그대로의 원래 방향**(흔들림을 더하기 전 FK)에서 `kMaxSwingDegrees`(40°)보다 벌어지면 그 각도로 되돌림 (충돌 처리 전). 부모의 휜 방향을 기준으로 하면 마디마다 각도가 누적되어 긴 머리카락이 날개처럼 펼쳐짐 (화면 캡처로 확인). 관성도 한 단계에 `kMaxInertiaMetersPerStep`(3cm, 초속 1.8m)까지만 반영 → 빠르게 떨어지다 착지해도 머리카락이 몸보다 높이 튀지 않음
- **순간 이동**: 한 프레임에 0.5m 넘게 움직이면(위치 초기화, 다른 모니터) 관성 없이 현재 자세에서 다시 시작.
- **시간이 흐르지 않은 프레임 (`dt = 0`)**: 화면 프레임이 고정 간격(1/60초) 사이에 끼면 그 프레임은 물리 0단계라 `dt = 0`으로 불림 (시작 직후·메뉴가 닫힌 직후처럼 누적 시간이 초기화되면 자주). 끝 위치는 그대로 두고 그 방향으로 본만 돌림(`hold`). 건너뛰면 그 프레임만 바인드 포즈로 그려져 한 프레임씩 번쩍였음
- **정지**: 끝이 한 단계에 0.01mm 미만으로 움직이면 그대로 둠. 미세한 오차로 매 프레임 자세가 바뀌면 Present 생략(DEBT-02)이 안 되기 때문.
- `animate`에서만 계산합니다 (`evaluate`·`displayBounds`는 상태가 없어 흔들림 없음). 자세 전환 보간 대상도 아닙니다 (스스로 연속적).

### 4.5 모션 클립 재생 (`MotionPlayer`, ADR-0013·ADR-0014)

클립은 휴머노이드 본(손가락 포함)마다 30fps "원본 기본 자세 → 지금" 모델 공간 회전 Δ와 원본 기본 자세의 팔다리·손가락 방향, 휴머노이드가 아닌 본의 이름 트랙, 엉덩이 이동을 가집니다 ([model.md §4.9](model.md)).

- **생성 시**: 대상 본마다 트랙과 보정 C를 구함. 휴머노이드 본은 휴머노이드 트랙, 그 밖의 본은 **이름이 같은 트랙**(`Skeleton::name`, 휴머노이드 트랙이 우선, 모델에 없는 이름은 버림). C = fromTo(대상 기본 자세 방향, 원본 기본 자세 방향) (팔다리·손가락 → `limbChild` 방향). 방향을 모르는 본(몸통, 손, 중간 본, 이름 트랙 본)은 부모의 C를 물려받음 → 손은 아래팔과 함께 돌아 손목이 꺾이지 않음. 엉덩이 비율 = 대상 엉덩이 높이 / 원본 `hipsHeight` (어느 한쪽이 0 이하면 이동 안 씀)
- **재생 시각**: 처음 한 번은 끝까지, 그 뒤로는 `loopStart` ~ 끝을 반복 (`loopStart`가 길이 이상이면 마지막 자세 유지). 앞뒤 프레임을 slerp
- **`sample(time)`**: 부모 먼저 순서로 Aⱼ = Δ·C (트랙 있음) 또는 A부모 (없음), 델타 = Aⱼ·A부모⁻¹
- **`rootOffset(time)`**: 엉덩이 이동 × 비율. 수직은 그대로, 수평은 첫 프레임 위치와 시작 → 끝 이동(진행률만큼)을 빼 흔들림만 남김 → 반복해도 제자리 (캐릭터 이동은 앱이 함)
- **`ProceduralAnimator` 연결**: `computeRotations`에서 그 동작에 클립이 있으면 코드 자세 대신 `sample`·`rootOffset`. 시각은 `AnimationInput::motionTime`(동작이 시작된 뒤 흐른 시간, `animate`가 동작이나 `AnimationInput::motionSerial`이 바뀔 때마다 0부터 채움 — 상호작용 중 다시 더블클릭하면 처음부터. 없으면 `time`) → 클립이 동작 시작에 맞춰 처음부터 나옴. 대기 동작을 끄면(`idleMotion = false`) 첫 프레임에 멈춤 (Present 생략 유지). 전환 보간·착지·발 붙이기·흔들림은 그대로이고, 전환 0.2초 동안 엉덩이 이동도 회전처럼 섞음
- **몸 위치 (`rootOffset(motion)`)**: 클립 엉덩이 이동 + (Idle·Walk·Interact만) 발 붙이기. 발 붙이기는 엉덩이 이동을 넣은 자세로 낮은 발 높이를 재므로, 클립이 이미 몸을 낮췄으면 더 내리지 않음 (내리기만 함)
- **경계 상자**: 클립이 있는 동작은 길이 전체에서 고르게 최대 24자세 (§4.3). 엉덩이 이동도 들어감
- **숨김 본 (ADR-0015)**: `hidden()` = 클립 `hiddenBones`와 이름이 같은 본. `writeOutput`이 그 동작의 클립이 있으면 그 마스크, 없으면 불러온 클립들 마스크의 합으로 `computeSkinMatrices`에 넘김 → 정점이 관절 한 점(v' = P)에 모여 보이지 않음. 전환 중에는 새 동작 기준. 경계 상자도 같은 경로라 숨긴 소품은 빠짐
- **이름 본 이동 (ADR-0015)**: `sampleOffsets(time)` = 이름 트랙의 `offsets` × (대상 엉덩이 높이 / 원본 `hipsHeight`, 모르면 1). `computeRotations`가 받아 `writeOutput`·발 붙이기의 `computePose`에 넘김 (전환 중에는 새 동작 값 그대로)
- **모프 가중치 (ADR-0015)**: 생성 시 `Model::morphs`를 받아 클립 `weightTracks`를 (모프 번호, 트랙)으로 연결 (이름·타깃 번호가 같은 것, 없으면 버림). `sampleWeights(time)`(트랙이 없는 모프는 `NamedMorph::defaultWeight`, 클립이 하나도 없으면 `writeOutput`이 기본 가중치로 채움) → `AnimationOutput::morphWeights` → 앱이 `MeshCharacter::morphWeights`로 넘기고 렌더러가 표정 모프와 함께 더함. 전환 중 섞지 않음
- **클립이 없는 동작의 이름 본 (ADR-0015)**: `named()` = 이름 트랙이 움직이는 본. 코드로 만든 자세 끝에 `drapeNamedBones`가 첫 클립(대기 우선)을 0초로 샘플링해 그 본들의 델타(부모 기준)를 덮어씀 → 머플러·꼬리가 기본 자세로 뻗지 않고 늘어짐. 이동 오프셋·모프 가중치도 그 첫 프레임 값 (눈꺼풀, 입 모양)

### 4.6 커서 바라보기 (LookAt, FR-28)

`AnimationInput::lookTarget`(모델 공간, 없으면 정면)을 향해 목·머리·눈을 돌립니다.

- **각도**: 머리 기본 위치 → 목표 방향의 좌우 `atan2(x, z)`, 위아래 `atan2(y, 수평 거리)`. 좌우 ±55°, 위 25°, 아래 20°로 자름 (바로 옆·뒤 목표도 목이 꺾이지 않음)
- **나누기**: 목 40% + 머리 60% (목이 없으면 머리 100%). 각 본에 모델 축 델타 `Y(yaw) · X(−pitch)`를 기존 자세 위에 덧붙임 (숙인 뒤 돌림). 눈 본이 있으면 같은 방향으로 30% 더 (최대 12°)
- **적용 순서**: 전환 보간·착지 웅크림 뒤, 발 붙이기·흔들림 앞. 클립 재생 중에도 적용
- **`animate`**: 각도가 목표로 지수 감쇠(초당 10, 약 0.3초에 95%)로 다가가고, 0.0001 rad 안이면 목표에 맞춤 → 커서가 멈추면 자세가 정확히 같아져 Present 생략 유지. `evaluate`는 목표 각도 그대로

## 5. 테스트 항목

`tests/anim/` — `AnimTestModels.h`의 최소 휴머노이드(본 13개, 자식이 부모보다 앞에 오도록 섞음, T/A포즈 선택)로 검증합니다.

| 테스트 | 검증 |
|---|---|
| `Skeleton.IdentityRotations_GiveIdentitySkin` | 회전 없음 → 모든 스킨 행렬이 항등 |
| `Skeleton.ParentRotation_MovesChildrenAroundParentJoint` | 위팔 회전이 아래팔·손을 어깨 기준으로 옮김, 배열 순서와 무관 |
| `Skeleton.ChildDelta_IsAppliedOnTopOfParentPose` | 자식 델타가 부모 자세 위에서 모델 축 기준으로 적용 |
| `Skeleton.HiddenBone_CollapsesItselfAndChildrenToTheirJoints` | 숨긴 본과 아래 본의 정점이 관절 한 점으로, 다른 본은 그대로 |
| `Skeleton.BoneOffset_MovesBoneAndChildrenInParentPoseAxes` | 이동 오프셋이 본과 자식을 옮기고, 부모 자세를 따라 돎 |
| `ProceduralAnimator.Idle_LowersTPoseArms` / `Idle_LowersAPoseArmsToSameDirection` | T포즈·A포즈 모두 같은 위치로 팔 내림 |
| `Walk_SwingsLegsInOppositeDirections` | 걷기 위상에서 두 발이 앞뒤 반대 |
| `Expressions_FollowMotion` | 깜빡임, 매달림 → 놀람(깜빡임 억제), 걷기 → 기쁨 |
| `IdleMotionDisabled_IsStaticOverTime` | 대기 동작 끄면 시간이 달라도 스킨 행렬이 같음 |
| `DisplayBounds_AreNarrowerThanTPoseButCoverRaisedArms` | 경계 상자가 T포즈보다 좁고 벌린 팔은 포함 |
| `MotionBounds_TurnOnlyWidensWalk`, `ScreenXRange_UsesActualVerticesThroughCamera` | 몸 돌림은 걷기 상자에만, 화면 x 범위는 실제 정점 투영(단위 행렬이면 상자 x 범위와 같음, 걷기는 2% 안) |
| `DisplayBounds_IncludeWalkingStride` | 걷기에서 앞뒤로 뻗은 발 포함 |
| `DisplayBounds_CoverBodyTurnedWhileWalking` | 뒤로 뻗은 꼬리가 ±50° 돌렸을 때 옆으로 나오는 만큼 포함 |
| `DisplayBounds_CoverIntermediateTurnAngles` | 양 끝 각도가 아닌 중간 각도의 최대도 포함 |
| `ModelWithoutHumanoid_StaysInBindPose` | 휴머노이드 본이 없으면 항등 |
| `ProceduralAnimatorTransition.FirstFrame_MatchesTargetPose` | 첫 프레임은 보간 없음 |
| `MotionChange_BlendsFromPreviousPose` | 바뀐 직후엔 이전 자세 가까이, 0.2초 뒤 목표 자세 |
| `InterruptedTransition_DoesNotJump` | 전환 도중 다시 바뀌어도 튀지 않음 |
| `ProceduralAnimatorLanding.Crouch_LowersHipsKeepingFeetOnGround` | 착지 후 엉덩이가 내려가고 발은 바닥 높이 |
| `NoWholeBodyStretch_FeetStayUnderHips` | 발이 엉덩이 아래 (뒤로 들리지 않음) |
| `RecoversToStandingPose` | 0.8초 뒤 대기 자세와 정확히 같음 |
| `WalkKeepsLowerFootOnGround` | 걷는 동안 낮은 쪽 발이 바닥 |
| `Expressions_FadeWithPose` | 놀람이 서서히 사라짐 |
| `AfterTransition_IdleIsStatic` | 전환 후 스킨 행렬 고정 (Present 생략) |
| `MotionPlayer.*` | 빈 클립은 기본 자세, 월드 델타 → 로컬 델타(트랙 없는 본은 부모를 따라감), A포즈 원본 → T포즈 대상 보정(손도 같이), 같은 기본 자세면 보정 없음, 이름 트랙은 같은 이름 본에만(없는 이름은 버림, 휴머노이드 우선), 프레임 보간·반복, 반복 시작 시각(처음 한 번 끝까지 → 그 구간만), 길이 이상이면 마지막 자세, 엉덩이 이동(높이 비율, 수평 루트 이동 제거), 이동 없는 클립은 0, 숨김 본은 이름으로(없는 이름은 버림), 이름 본 이동은 엉덩이 높이 비율로, 가중치 트랙은 모프 이름·타깃 번호로(없는 이름은 버림) |
| `ProceduralAnimatorClip.*` | 지정한 동작만 클립 재생, 상호작용 클립은 `motionSerial`이 바뀌면 처음부터, 경계 상자에 클립 자세 포함(동작별 상자에는 그 동작에만), 서 있는 자세 상자는 클립과 무관, 클립 제거 시 코드 동작 복귀, 엉덩이 이동으로 몸이 들림, 대기 클립이 낮춘 몸을 발 붙이기가 두 번 내리지 않음, 동작이 시작될 때 클립이 처음부터, 클립의 숨김 본이 접힘(클립 없는 동작도, 숨기지 않는 클립에선 보임), 클립 없는 동작의 이름 본은 대기 클립 첫 프레임 모양, 모프 가중치·이동 오프셋이 클립에서(클립 없는 동작은 대기 첫 프레임), 클립이 없거나 트랙이 없는 모프는 기본 가중치 |
| `ProceduralAnimatorLookAt.*` | 목표 없음·정면이면 그대로, 옆·위 목표 쪽으로 머리가 정확히 향함, 좌우·위 각도 제한, `animate`는 서서히 돌고 멈추면 같은 자세 |

`tests/anim/SpringBoneTests.cpp` — 몸통 + 사슬 3개짜리 모델: 힘이 없으면 바인드 포즈 유지, 중력으로 처지되 본 길이 유지, 캐릭터가 움직이면 끝이 뒤처졌다가 돌아옴, 구 충돌체 안으로 들어가지 않음(구가 없을 때와 대조), 순간 이동은 초기화, 멈춘 뒤 회전이 정확히 같음, 격하게 움직여도 각 마디·사슬 전체가 범위 안, 관성 상한, `dt = 0` 프레임은 직전 흔들림 유지(다음 프레임도 0 프레임이 없던 것과 같음), VRM 1.0 관절 사슬은 적힌 본만 흔들리고 마지막 관절은 스스로 돌지 않음, 캡슐 충돌체 안으로 들어가지 않음(양 끝 구로만 처리하면 막지 못하는 배치).

## 6. 확장 지점

| TODO | 내용 |
|---|---|
| ~~`TODO(M4)`~~ | ✅ 모션 파일 재생 (VRMA, VMD, FBX, §4.5). 남은 것: 걷기 클립 보폭과 이동 속도 맞추기, VRMA 표정 |
| ~~`TODO(M4)`~~ | ✅ 손가락, 비휴머노이드 본 이름 트랙, 엉덩이 이동, 반복 시작 시각 (ADR-0014). 남은 것: 이름 트랙과 SpringBone이 같은 본에 겹칠 때 우선순위 선택 |
| ~~`TODO(M5)`~~ | ✅ SpringBone (VRM 0.x, §4.4) |
| ~~`TODO(M5)`~~ | ✅ VRM 1.0 SpringBone(`VRMC_springBone`, 관절별 값, 캡슐 충돌체, §4.4). 남은 것: `center` 노드 |
| `TODO(M5)` | PMX 강체 → 흔들림 근사 |
| ~~`TODO(M6)`~~ | ✅ 시선(LookAt, §4.6). 남은 것: VRM 표정 방식 시선(BlendShape lookAt) |
