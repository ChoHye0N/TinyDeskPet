# anim 모듈 상세 설계

| 항목 | 내용 |
|---|---|
| CMake 타깃 | `deskpet_anim` (STATIC) |
| 네임스페이스 | `deskpet::anim` |
| 의존 | `deskpet_model` (→ `deskpet_core`) |
| 관련 | [ADR-0010](../02-architecture/adr/0010-procedural-animation.md), [ADR-0009](../02-architecture/adr/0009-multiple-model-formats.md) |

## 1. 책임

- 뼈대 정방향 운동학(FK)으로 본별 스킨 행렬을 계산합니다.
- 캐릭터 동작 상태를 휴머노이드 본 회전과 표정 가중치로 바꿉니다 (모션 파일 없이 코드로 계산).
- 하지 않는 일: GPU 업로드(renderer), 캐릭터 상태 결정(character), 모션 파일 재생(이후 과제).
- 플랫폼 독립. Linux CI에서 테스트됩니다.

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `src/anim/Skeleton.h/.cpp` | 본 계층 정리(부모 먼저 순서), FK, 스킨 행렬 |
| `src/anim/ProceduralAnimator.h/.cpp` | `Motion` → 본 회전·표정, 표시용 경계 상자 |
| `src/anim/SpringBone.h/.cpp` | 머리카락·옷 흔들림 (VRM SpringBone, §4.4) |
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
    void computeSkinMatrices(std::span<const core::Quat> rotations, std::vector<core::Mat4>& out) const;
};

enum class Motion : std::uint8_t { Idle, Walk, Dragged, Airborne };
struct AnimationInput { Motion motion; float time; float squash; bool blink; bool idleMotion = true; };
struct AnimationOutput { std::vector<core::Mat4> skin; std::array<float, Expression::Count> expressions; };

class ProceduralAnimator {
public:
    explicit ProceduralAnimator(const model::Model& model);  // model은 animator보다 오래 살아야 함
    static constexpr float kTransitionSeconds = 0.2f;
    void evaluate(const AnimationInput& input, AnimationOutput& out) const;  // 입력만으로 정해지는 자세
    void animate(const AnimationInput& input, float dt, AnimationOutput& out);  // 전환 보간 포함 (매 프레임)
    model::Bounds displayBounds() const;  // 대기·매달림·공중 자세를 모두 담는 경계 상자
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
| 착지 반동 | — | 허벅지 +60°×squash / 무릎 +150°×squash | — | — |

- **팔 내리기**: 목표 방향 = `(side·sin벌림, −cos벌림, 0)`을 X축으로 흔들기만큼 회전. 위팔 회전 = `fromTo(바인드 방향, 목표)`. `side`는 바인드 방향의 x 부호(캐릭터 왼쪽 = +X).
- 회전 방향: 허벅지 `X축 −각도` = 발이 앞(+Z), 무릎 `X축 +각도` = 발이 뒤, 팔꿈치 `X축 −각도` = 손이 앞.
- 가슴 본이 없으면 척추 본을 씁니다. 필요한 본이 없으면 그 부위만 움직이지 않습니다.
- `idleMotion = false`면 Idle 자세가 시간과 무관해져, 장면이 같아지므로 렌더러가 Present를 생략합니다.
- 깜빡임 가중치 = `1 − 놀람` (완전히 놀라면 깜빡이지 않음).

### 4.2a 상태 전환 보간 (`animate`)

- 동작(`Motion`)이 바뀌면 **직전 프레임에 그린 자세**를 저장하고, `kTransitionSeconds`(0.2초) 동안 본마다 `slerp(이전, 새 자세, w)`로 섞습니다. `w = smoothstep(경과/0.2)` — 시작·끝에서 속도 0.
- 표정(기쁨·놀람)도 같은 `w`로 선형 보간합니다.
- 전환 도중 다시 바뀌어도 섞인 자세에서 출발하므로 튀지 않습니다. 이전 자세는 멈춘 상태로 쓰는데(걷던 다리가 그 자리에서 사라짐), 0.2초라 눈에 띄지 않습니다.
- **착지 반동은 보간 뒤에 더합니다.** 반동은 Airborne→Idle 전환과 같은 프레임에 시작하므로, 함께 섞으면 약해집니다. 다리 회전은 모두 X축이라 뒤에 곱해도 각도를 더한 것과 같습니다. 저장하는 자세에는 반동을 빼서 두 번 더해지지 않게 합니다.
- 첫 프레임은 넘어올 자세가 없어 그대로 그립니다. 전환이 끝나면 보간을 건너뛰므로 대기 자세가 정지하면 다시 Present 생략이 됩니다.
- `dt`는 앱이 이번 프레임에 진행한 고정 스텝 시간(`steps × 1/60`)을 넘깁니다. `evaluate`(상태 없음)는 `displayBounds`와 테스트에서 씁니다.

### 4.3 표시용 경계 상자 (`displayBounds`)

`displayBounds(maxTurnRadians)` — 화면에 실제로 그려질 수 있는 모든 모습을 담는 경계 상자입니다. T포즈 경계를 쓰면 팔 폭 때문에 캐릭터가 작게 보이고, 일부 자세만 쓰면 빠진 자세에서 창 밖으로 잘립니다.

- **자세**: Idle·Dragged·Airborne + **Walk 한 주기를 8등분**(다리를 가장 멀리 뻗는 1/4·3/4, 무릎을 가장 굽히는 0·1/2 포함). 정점은 CPU에서 스키닝합니다.
- **몸 돌리기**: 앱은 걸을 때 몸을 Y축으로 최대 ±50° 돌려 그립니다. 그러면 정면에선 깊이(Z)였던 꼬리·치마·뒤로 뻗은 머리카락이 화면 가로(X)로 나옵니다. 각 정점에 대해 θ ∈ [−T, T]에서 `x' = x·cos θ + z·sin θ`의 범위를 **해석적으로** 구합니다: `= r·cos(θ − φ)`이므로 양 끝값과, φ가 구간 안이면 ±r. 양 끝 각도만 샘플링하면 비스듬히 뒤로 뻗은 점의 최대(θ = φ)를 놓칩니다. z'도 같은 방법.
- 스킨 가중치가 있는 정점이 하나도 없으면 원래 `Model::bounds`(돌릴 각도가 있으면 그 꼭짓점 8개를 돌려서)를 씁니다.
- 걷기 중 바뀌는 회전은 모든 동작에 적용합니다 (걷다가 들리면 돌아오는 도중에도 회전이 남아 있음).

### 4.4 흔들림 (`SpringBoneSimulator`)

UniVRM `VRMSpringBone`과 같은 방식입니다. 데이터는 모델의 `springGroups`·`springColliders`(VRM 0.x `secondaryAnimation`)를 그대로 써서, 모델마다 제작자가 정한 대로 흔들립니다.

- **관절**: 그룹의 루트 본부터 자손 전체(전위 순회 → 부모가 먼저). 본의 끝(tail)은 첫 자식 위치, 자식이 없으면 부모 → 본 방향으로 7cm.
- **한 단계** (`dt` = 1/60초, 프레임이 밀리면 최대 4단계로 나눔):
  1. 몸 자세(절차적 동작 + 착지 반동)로 FK → 부모 회전 `A부모`, 머리 위치 `head`
  2. 원래 방향 `rest = (Δⱼ·A부모)(축)`
  3. `next = cur + (cur − prev)(1 − drag) + rest·stiffness·dt + 중력방향·gravityPower·dt`
  4. 본 길이로 되돌림 → 그룹의 구 충돌체(`P본 + A본(offset)`, 반지름 + hitRadius) 안이면 표면으로 밀고 다시 길이 유지
  5. `Δⱼ ← fromTo(rest, next − head) · Δⱼ`, 이 본의 `A`·`P`를 갱신해 자식 계산에 사용
- **관성 (`AnimationInput::movement`)**: 캐릭터가 화면에서 움직인 거리(m). 모델 공간에서 계산하므로 이전 끝 위치들을 `−movement`만큼 옮겨 관성을 만듭니다. 앱이 창 이동 px × (m/px)를 걸을 때 돌린 몸의 반대로 돌려서 넘깁니다 → 앞으로 걸으면 머리카락이 뒤로 날림.
- **순간 이동**: 한 프레임에 0.5m 넘게 움직이면(위치 초기화, 다른 모니터) 관성 없이 현재 자세에서 다시 시작.
- **정지**: 끝이 한 단계에 0.01mm 미만으로 움직이면 그대로 둠. 미세한 오차로 매 프레임 자세가 바뀌면 Present 생략(DEBT-02)이 안 되기 때문.
- `animate`에서만 계산합니다 (`evaluate`·`displayBounds`는 상태가 없어 흔들림 없음). 자세 전환 보간 대상도 아닙니다 (스스로 연속적).

## 5. 테스트 항목

`tests/anim/` — `AnimTestModels.h`의 최소 휴머노이드(본 13개, 자식이 부모보다 앞에 오도록 섞음, T/A포즈 선택)로 검증합니다.

| 테스트 | 검증 |
|---|---|
| `Skeleton.IdentityRotations_GiveIdentitySkin` | 회전 없음 → 모든 스킨 행렬이 항등 |
| `Skeleton.ParentRotation_MovesChildrenAroundParentJoint` | 위팔 회전이 아래팔·손을 어깨 기준으로 옮김, 배열 순서와 무관 |
| `Skeleton.ChildDelta_IsAppliedOnTopOfParentPose` | 자식 델타가 부모 자세 위에서 모델 축 기준으로 적용 |
| `ProceduralAnimator.Idle_LowersTPoseArms` / `Idle_LowersAPoseArmsToSameDirection` | T포즈·A포즈 모두 같은 위치로 팔 내림 |
| `Walk_SwingsLegsInOppositeDirections` | 걷기 위상에서 두 발이 앞뒤 반대 |
| `Expressions_FollowMotion` | 깜빡임, 매달림 → 놀람(깜빡임 억제), 걷기 → 기쁨 |
| `IdleMotionDisabled_IsStaticOverTime` | 대기 동작 끄면 시간이 달라도 스킨 행렬이 같음 |
| `DisplayBounds_AreNarrowerThanTPoseButCoverRaisedArms` | 경계 상자가 T포즈보다 좁고 벌린 팔은 포함 |
| `DisplayBounds_IncludeWalkingStride` | 걷기에서 앞뒤로 뻗은 발 포함 |
| `DisplayBounds_CoverBodyTurnedWhileWalking` | 뒤로 뻗은 꼬리가 ±50° 돌렸을 때 옆으로 나오는 만큼 포함 |
| `DisplayBounds_CoverIntermediateTurnAngles` | 양 끝 각도가 아닌 중간 각도의 최대도 포함 |
| `ModelWithoutHumanoid_StaysInBindPose` | 휴머노이드 본이 없으면 항등 |
| `ProceduralAnimatorTransition.FirstFrame_MatchesTargetPose` | 첫 프레임은 보간 없음 |
| `MotionChange_BlendsFromPreviousPose` | 바뀐 직후엔 이전 자세 가까이, 0.2초 뒤 목표 자세 |
| `InterruptedTransition_DoesNotJump` | 전환 도중 다시 바뀌어도 튀지 않음 |
| `LandingSquash_IsNotDelayed` | 착지 반동이 전환으로 약해지지 않음 |
| `Expressions_FadeWithPose` | 놀람이 서서히 사라짐 |
| `AfterTransition_IdleIsStatic` | 전환 후 스킨 행렬 고정 (Present 생략) |

`tests/anim/SpringBoneTests.cpp` — 몸통 + 사슬 3개짜리 모델: 힘이 없으면 바인드 포즈 유지, 중력으로 처지되 본 길이 유지, 캐릭터가 움직이면 끝이 뒤처졌다가 돌아옴, 구 충돌체 안으로 들어가지 않음(구가 없을 때와 대조), 순간 이동은 초기화, 멈춘 뒤 회전이 정확히 같음.

## 6. 확장 지점

| TODO | 내용 |
|---|---|
| `TODO(M4)` | 모션 파일 재생 (VRMA, VMD, FBX) — 같은 휴머노이드 본으로 리타기팅 |
| `TODO(M4)` | 손가락 |
| ~~`TODO(M5)`~~ | ✅ SpringBone (VRM 0.x, §4.4) |
| `TODO(M5)` | VRM 1.0 SpringBone(`VRMC_springBone`, 캡슐 충돌체), PMX 강체 → 흔들림 근사 |
| `TODO(M6)` | 시선(LookAt) |
