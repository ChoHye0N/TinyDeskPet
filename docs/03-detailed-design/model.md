# model 모듈 상세 설계

| 항목 | 내용 |
|---|---|
| 문서 ID | DP-DDS-MODEL |
| 버전 | 0.2.0 |
| 관련 | [ADR-0005](../02-architecture/adr/0005-vrm-over-live2d.md), [ADR-0008](../02-architecture/adr/0008-vrm-before-interaction.md), [ADR-0009](../02-architecture/adr/0009-multiple-model-formats.md), FR-22 ~ FR-28 |

## 1. 책임

- VRM/glTF/GLB, PMX, FBX 파일을 읽어 **렌더링에 바로 쓸 수 있는 공통 데이터**(`Model`)로 바꿉니다.
- 형식마다 다른 좌표계·단위·UV 원점·본 이름을 하나의 규약으로 통일합니다.
- 애니메이션용 데이터(정점 스킨 가중치, 표정 모프)를 형식과 무관한 모양으로 제공합니다 (ADR-0010).
- 하지 않는 일: GPU 업로드, PNG/JPEG 디코딩(렌더러가 WIC로), 자세 계산(anim 모듈), 물리.
- 플랫폼 독립입니다. Windows 헤더를 쓰지 않으며 Linux CI에서 테스트됩니다.

## 2. 파일 구성

| 파일 | 역할 |
|---|---|
| `src/model/Model.h` | `Model`, `Vertex`, `Material`, `Texture`, `Bone`, `Primitive`, `Bounds`, `ModelFormat`, `computeBounds` |
| `src/model/ModelLoader.h/.cpp` | 진입점: `detectModelFormat`, `loadModelFromMemory`, `loadModelFile` |
| `src/model/Importers.h` | 형식별 로더 선언 (모듈 내부 전용) |
| `src/model/GltfImporter.cpp` | glTF 2.0 / GLB / VRM 0.x·1.0 (cgltf) |
| `src/model/PmxImporter.cpp` | PMX 2.0·2.1 (직접 구현) |
| `src/model/FbxImporter.cpp` | FBX 바이너리·ASCII (ufbx) |
| `src/model/TexturePadding.h/.cpp` | 텍스처 패딩: UV 섬 바깥 여백을 섬 색으로 채워 이음매 선 방지 (§4.8) |
| `src/model/TextureSource.h/.cpp` | 텍스처 바이트 정리, 외부 텍스처 파일 읽기, TGA 디코딩 |
| `src/model/Humanoid.h/.cpp` | `HumanBone`과 형식별 본 이름 매핑표 |
| `src/model/ThirdPartyImpl.cpp` | cgltf·stb_image 구현부 (우리 경고 옵션 미적용 타깃 `deskpet_model_thirdparty`, ufbx.c 포함) |
| `src/core/Math3D.h` | `Vec3`, `Vec4`, `Mat4` ([core.md §3.8](core.md)) |

의존성 (FetchContent, 버전 고정): cgltf v1.14, ufbx v0.23.1, stb (커밋 `2c980bb`, TGA 전용 빌드).

## 3. 공개 인터페이스

```cpp
namespace deskpet::model {
// 공통 규약: 미터, 오른손, +Y 위, 정면 +Z, CCW 앞면, UV 원점 왼쪽 위
struct Vertex { core::Vec3 position; core::Vec3 normal; float u, v;
                std::array<std::uint16_t, 4> joints; std::array<float, 4> weights; };  // 56바이트
// joints = Model::bones 인덱스, 가중치 큰 순서·합 1 (모두 0이면 움직이지 않음)
void setSkinWeights(Vertex&, std::span<const std::pair<int, float>> influences);  // 합치기·상위 4개·정규화
enum class Expression : std::uint8_t { Blink, Happy, Surprised, Count };
struct Morph { std::vector<std::uint32_t> vertices; std::vector<core::Vec3> deltas; };  // 희소 정점 오프셋
enum class AlphaMode : std::uint8_t { Opaque, Mask, Blend };
struct SpringCollider { int bone; core::Vec3 offset; float radius; };
struct SpringGroup { float stiffness, gravityPower; core::Vec3 gravityDir; float dragForce, hitRadius;
                     std::vector<int> roots, colliders; };
struct Material { std::string name; core::Vec4 baseColor; int baseColorTexture;  // -1 = 없음
                  AlphaMode alphaMode; float alphaCutoff; bool doubleSided;
                  float outlineWidth /* m, 0 = 없음 */; core::Vec4 outlineColor; };
struct Texture { std::string name, mimeType;
                 std::vector<std::uint8_t> encoded;           // WIC가 읽는 형식 원본 (PNG/JPEG/BMP…)
                 int width, height; std::vector<std::uint8_t> rgba;  // 미리 푼 픽셀 (TGA)
                 bool empty() const; };                        // 둘 다 비면 읽기 실패 → 흰색
struct Bone { std::string name; int parent; core::Vec3 position; HumanBone human; };
struct Primitive { std::uint32_t firstIndex, indexCount; int material; };
enum class ModelFormat : std::uint8_t { Unknown, Gltf, Pmx, Fbx };
enum class VrmVersion : std::uint8_t { None, V0, V1 };

struct Model {
    ModelFormat format; VrmVersion version;
    std::vector<Vertex> vertices;          // 바인드 포즈 모델 공간
    std::vector<std::uint32_t> indices;    // 전체 정점 배열 기준
    std::vector<Primitive> primitives;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Bone> bones;
    std::array<Morph, Expression::Count> expressions;  // expression(Expression::Blink) 등
    Bounds bounds;
    int findBone(HumanBone) const;         // 없으면 -1
};

struct ImportContext { std::filesystem::path baseDirectory; };  // 외부 텍스처 기준 폴더
struct LoadResult { std::optional<Model> model; std::string error; };

ModelFormat detectModelFormat(std::span<const std::uint8_t> bytes, std::string_view extension = {});
LoadResult loadModelFromMemory(std::span<const std::uint8_t> bytes, std::string_view extension = {},
                               const ImportContext& context = {});
LoadResult loadModelFile(const std::filesystem::path& path);  // 기준 폴더 = 파일이 있는 폴더

// Humanoid.h
enum class HumanBone : std::uint8_t { None, Hips, Spine, Chest, UpperChest, Neck, Head, ... };  // 손가락 제외 24개
HumanBone humanBoneFromVrmName(std::string_view);   // "leftUpperArm"
HumanBone humanBoneFromMmdName(std::string_view);   // "左腕" (UTF-8)
HumanBone humanBoneFromFbxName(std::string_view);   // "mixamorig:LeftArm" (':' 앞 무시)
}
```

## 4. 동작 명세

### 4.1 형식 판별 (`detectModelFormat`)

매직 바이트가 확장자보다 우선합니다 (확장자가 잘못 붙은 파일 대비).

| 순서 | 조건 | 형식 |
|---|---|---|
| 1 | `glTF` | Gltf (GLB, .vrm) |
| 2 | `PMX ` | Pmx |
| 3 | `Kaydara FBX Binary` 또는 `; FBX` | Fbx |
| 4 | 확장자 `.fbx` (대소문자 무관) | Fbx (주석 없는 ASCII FBX) |
| 5 | 첫 비공백 문자가 `{` | Gltf (JSON) |
| — | 그 외 (PMD, OBJ, ZIP 등) | Unknown → "지원하지 않는 모델 형식" 오류 |

`loadModelFile`은 파일을 직접 읽어 메모리로 넘깁니다 (라이브러리의 `char*` 경로가 한글·일본어 경로에서 깨지는 문제 회피).

### 4.2 형식별 좌표 변환

| 형식 | 단위 | 좌표계·정면 | 변환 | 감기 순서 | UV |
|---|---|---|---|---|---|
| glTF / VRM 1.0 | m | 오른손, 정면 +Z | 없음 | CCW | 왼쪽 위 원점 |
| VRM 0.x | m | 오른손, 정면 −Z | Y축 180° 회전 = `(x, z)` 부호 반전 | 그대로 (회전은 감기 순서 유지) | 〃 |
| PMX | 1단위 ≈ 8cm | **왼손**, 정면 −Z | `(x, y, −z) × 0.08`, 법선도 z 반전 | z 반전(거울)로 뒤집히므로 **삼각형 두 정점 교환** | 〃 |
| FBX | 파일마다 (보통 cm) | 파일마다 | ufbx `target_axes = 오른손 Y 위`, `target_unit_meters = 1`, `MODIFY_GEOMETRY` | ufbx가 처리 | **왼쪽 아래 원점** → `v' = 1 − v` |

PMX의 z 반전 한 번이 "왼손 → 오른손"과 "정면 −Z → +Z"를 동시에 해결합니다.

### 4.3 glTF / VRM (`GltfImporter`)

1. `cgltf_parse` → `cgltf_load_buffers` → `cgltf_validate` (범위 밖 접근자 등 손상된 파일 차단)
2. 버전: `extensionsUsed`에 `VRMC_vrm` → V1, `VRM` → V0
3. 이미지: 버퍼 뷰 안의 이미지(GLB) → `makeTexture`. `.gltf`의 외부 이미지 파일 URI → `loadTextureFile`
4. 머티리얼: `pbrMetallicRoughness` 기본색·텍스처, 알파 모드, 양면
5. 본: **모든 노드**를 본으로 (인덱스 = 노드 인덱스, VRM `humanBones`가 노드 번호로 가리키므로). 위치 = 월드 행렬의 이동
6. 휴머노이드: 확장 JSON(`cgltf_extension.data`)에서 `humanBones`만 찾는 최소 스캐너 — 1.0은 `{"hips": {"node": n}}` 객체, 0.x는 `[{"bone": "hips", "node": n, "min": {...}}]` 배열(중첩 객체를 괄호 짝 맞추기로 건너뜀)
7. 메시: 메시가 달린 모든 노드의 삼각형 프리미티브를 이어 붙임. 바인드 포즈는 아래처럼 굽기

| 노드 | 정점 변환 |
|---|---|
| 스킨 없음 | 노드 월드 행렬 |
| 스킨 있음 | Σ wᵢ · (역바인드ᵢ × 관절 월드ᵢ). 노드 자신의 변환은 **무시** (glTF 사양) |
| 가중치 합 0 | 노드 월드 행렬로 대체 |

8. 스킨 가중치: `JOINTS_0`의 관절 번호 → `skin.joints[번호]`의 노드 번호 = 본 번호. 스킨이 없는 메시(액세서리 등)는 **자기 노드에 가중치 1로 고정**해 그 노드와 함께 움직임
9. 모프 타깃: `POSITION` 델타를 정점과 같은 변환(바인드 포즈 행렬의 3×3, VRM 0.x 회전)으로 모델 공간에 옮겨 노드별로 희소 저장
10. 표정: VRM 1.0 `expressions.preset.{blink, happy, surprised}.morphTargetBinds[{node, index, weight 0~1}]`, VRM 0.x `blendShapeMaster.blendShapeGroups[{presetName: blink/joy, binds[{mesh, index, weight 0~100}]}]` (0.x에는 놀람 프리셋이 없어 이름 `Surprised`로 찾음, `mesh` 번호는 그 메시를 단 모든 노드로 확장)

GLB의 BIN 청크는 복사되지 않고 입력 바이트를 가리키므로, 변환이 끝날 때까지 입력을 유지합니다.

### 4.4 PMX (`PmxImporter`)

사양: PmxEditor 동봉 `PMX仕様.txt`. 읽는 구간: 헤더 → 모델 정보 → 정점 → 면 → 텍스처 → 머티리얼 → 본 → 모프 (표시 틀·강체·조인트는 읽지 않음).

| 항목 | 처리 |
|---|---|
| 리더 | 범위를 검사하는 `Reader`. 끝을 넘으면 `failed` 상태가 되고 값은 0 — 구간마다 확인해 "PMX 파일이 손상되었습니다 (구간)" 오류. 예외를 쓰지 않음 |
| 개수 필드 | 음수이거나 `개수 × 최소 크기 > 남은 바이트`면 손상으로 판단 (손상 파일의 거대 할당 방지) |
| 헤더 | 버전 2.0 ≤ v < 3.0, 전역 설정 8개 이상(초과분은 건너뜀). 인덱스 크기는 1·2·4만 허용 |
| 텍스트 | int32 바이트 길이 + 본문. UTF-16LE(서로게이트 쌍 포함)는 UTF-8로 변환 |
| 인덱스 | 정점 인덱스는 부호 없음(1·2바이트) / int32. 그 외(본·텍스처·머티리얼)는 부호 있음, −1 = 없음 |
| 정점 가중치 | BDEF1 / BDEF2 / BDEF4 → 그대로, SDEF(구면 보간) → BDEF2로, QDEF(2.1) → BDEF4로 근사. 본을 읽은 뒤 없는 본을 가리키는 영향은 제거 |
| 면 | 인덱스 수가 3의 배수, 모든 인덱스 < 정점 수 |
| 텍스처 | 모델 파일 기준 상대 경로(`\` 구분자)의 외부 파일 |
| 머티리얼 | 확산색 → 기본색, 플래그 bit0 → 양면. 머티리얼별 면 수가 프리미티브 구간. 알파 < 1 → Blend, 텍스처 있음 → Mask, 그 외 Opaque |
| 본 | 플래그(0x0001 꼬리 본, 0x0100/0x0200 부여, 0x0400 고정 축, 0x0800 로컬 축, 0x2000 외부 부모, 0x0020 IK)에 따른 가변 필드를 사양서 순서대로 건너뜀. 휴머노이드는 일본어 표준 본 이름으로 매핑 |
| 모프 | 정점 모프 중 `まばたき` → Blink, `笑い` → Happy, `びっくり` → Surprised (오프셋도 × 0.08, z 반전). 그룹·본·UV·재질·플립·임펄스 모프는 크기만큼 건너뜀 |

MMD는 모든 재질을 알파 블렌딩으로 그리지만, 깊이 정렬 문제를 피하려고 텍스처 재질은 알파 테스트(Mask)로 근사합니다.

### 4.5 FBX (`FbxImporter`)

| 항목 | 처리 |
|---|---|
| 읽기 | `ufbx_load_memory` (`generate_missing_normals`, `ignore_animation`). 실패 시 `ufbx_format_error` 메시지 |
| 메시 | 루트가 아닌 노드의 메시마다, 머티리얼 파트별로 `ufbx_triangulate_face`(오목 다각형 포함) → 삼각형 꼭짓점마다 정점 생성 (중복 제거 없음). 위치는 `geometry_to_world`, 법선은 `ufbx_matrix_for_normals` |
| 머티리얼 | ufbx가 정리한 PBR 값: `base_color × base_factor`, `opacity` < 1 → Blend, 텍스처 → Mask. `features.double_sided` |
| 텍스처 | 내장(`content`)이면 그대로, 아니면 `relative_filename` → 실패하면 파일 이름만으로 모델 폴더에서 찾음 (다른 PC에서 만든 절대 경로 대비) |
| 본 | `bone` 속성이 있는 노드 + 스킨 클러스터가 가리키는 노드. 부모는 가장 가까운 본 조상. 위치는 클러스터의 `bind_to_world`(바인드 포즈)를 우선, 없으면 `node_to_world`. Mixamo 이름으로 휴머노이드 매핑. 메시보다 먼저 읽음 |
| 스킨 가중치 | 첫 번째 스킨 디포머의 정점별 (클러스터, 가중치) → 본 번호. 면 꼭짓점 번호는 `vertex_indices`로 정점 번호로 바꿔 조회. 스킨 없는 메시는 움직이지 않음 |
| 표정 | 아직 읽지 않음 (FBX 블렌드 셰이프 이름이 제각각) |

### 4.6 텍스처 (`TextureSource`)

| 입력 | 결과 |
|---|---|
| PNG/JPEG/BMP/GIF/DDS/TIFF (매직 바이트) | `encoded` + `mimeType` — 렌더러가 WIC로 디코딩 |
| 그 외 | TGA로 보고 stb_image로 디코딩 → `rgba` (RGBA8). 실패하면 원본을 `encoded`로 (렌더러가 마지막으로 시도) |
| 외부 파일 | `baseDirectory / 상대 경로`(`\`→`/`, UTF-8 → `u8string` 경로). 기준 폴더가 없거나 파일이 없으면 빈 텍스처 (경고 로그) |

### 4.8 텍스처 패딩 (`padUvIslands`)

텍스처는 그림 조각(UV 섬)과 그 사이 여백(보통 검정)으로 되어 있습니다. 선형 필터·밉맵·축소는 주변 텍셀을 섞으므로 섬 가장자리에 여백 색이 섞여 들어오고, 조각끼리 맞닿는 이음매(좌우 대칭 몸통의 중앙, 입 테두리 등)에 얇은 선이 생깁니다. 게임 엔진이 텍스처를 구울 때 하는 처리처럼, 여백을 섬 색으로 미리 채웁니다.

1. `collectUvTriangles(model, 텍스처)`: 그 텍스처를 쓰는 머티리얼의 삼각형 UV를 모음
2. 덮인 텍셀 표시: UV 삼각형을 텍셀 격자에 래스터화 (텍셀 중심이 삼각형 안이면 덮임). UV는 반복(REPEAT)으로 해석해 `[1, 2)` 등도 같은 텍셀로
3. 번짐(dilation): 덮인 이웃(8방향, 가장자리는 감싸서)이 있는 빈 텍셀을 이웃 평균색으로 채우는 것을 한 겹씩 `maxDistance`번. 한 겹을 다 계산한 뒤 표시해 거리 = 겹 수. 다음 겹 후보는 직전에 칠한 텍셀의 이웃만 봄 (전체 재탐색 없음)
4. 섬 안 텍셀은 바꾸지 않고, 삼각형이 없으면 아무것도 하지 않음

렌더러는 디코딩·축소 직후, **밉맵을 만들기 전에** 16텍셀(512 기준 밉 4단계까지)을 채웁니다. 측정: 펭귄 모델(텍스처 7장, 삼각형 5.3만 개) 로드 + 업로드 0.2초.

### 4.7 오류

| 상황 | 결과 |
|---|---|
| 지원하지 않는 형식 | `model` 비어 있음 + "지원하지 않는 모델 형식입니다" |
| 파싱 실패, 손상, 검증 실패 | 〃 + 형식별 이유 |
| 그릴 메시 없음 / 인덱스 범위 초과 / 정점 2³²개 초과 | 〃 |
| 텍스처 파일 없음·디코딩 실패 | 모델은 성공, 해당 텍스처만 흰색 |

### 흔들림 정보 (SpringBone)

VRM 0.x `extensions.VRM.secondaryAnimation`만 읽습니다 (VRM 1.0 `VRMC_springBone`, PMX 강체는 TODO).

| 데이터 | 출처 | 변환 |
|---|---|---|
| `SpringCollider{bone, offset, radius}` | `colliderGroups[{node, colliders[{offset, radius}]}]` (그룹별 구를 펼쳐 저장) | VRM 0.x 노드는 회전 없는 T포즈라 offset을 모델 공간으로 쓰고 x·z 부호만 반전 |
| `SpringGroup{stiffness, gravityPower, gravityDir, dragForce, hitRadius, roots, colliders}` | `boneGroups[{stiffiness(원문 철자), gravityPower, gravityDir, dragForce, hitRadius, bones, colliderGroups}]` | gravityDir도 x·z 반전. colliderGroups 번호 → 구 인덱스 목록 |

`secondaryAnimation` 안에는 `colliderGroups`가 두 단계(최상위, `boneGroups[]` 안)에 있어서, 처음 나온 키를 찾는 스캐너로는 엉뚱한 배열을 읽습니다. 바로 아래 단계만 찾는 `childSpan`을 씁니다.

### MToon 정보 (ADR-0012)

`Material`의 MToon 값은 VRM 1.0 정의 하나로 통일하고, 색은 모두 선형 공간입니다. 렌더러는 `mtoon = false`면 기본 2단 툰으로 그립니다.

| 형식 | 출처 | 변환 |
|---|---|---|
| VRM 1.0 | `materials[].extensions.VRMC_materials_mtoon`: `shadeColorFactor`, `shadeMultiplyTexture`, `shadingShiftFactor`, `shadingToonyFactor`, `matcapFactor`, `matcapTexture`, `parametricRim*`, `rimLightingMixFactor`. 발광은 glTF `emissiveFactor`·`emissiveTexture` | 그대로. 텍스처 참조 `{index}`는 glTF 텍스처 → 이미지 번호로 |
| VRM 0.x | `extensions.VRM.materialProperties[]`(재질 이름으로 매칭, `shader`가 `VRM/MToon`일 때만): `_ShadeColor`, `_ShadeTexture`, `_ShadeShift`, `_ShadeToony`, `_RimColor`, `_RimFresnelPower`, `_RimLift`, `_RimLightingMix`, `_SphereAdd`(MatCap), `_EmissionColor`, `_EmissionMap` | 색(그림자·림·외곽선)은 sRGB → 선형. 발광은 HDR이라 그대로. 그림자 경계: min = shift, max = lerp(1, shift, toony) → 1.0의 shift = −(min + max)/2, toony = 1 − (max − min)/2. glTF `emissiveFactor`(대체 셰이더용)는 무시 |
| glTF(VRM 아님) | `emissiveFactor`·`emissiveTexture`, `KHR_materials_unlit` → `unlit` | MToon 아님 |
| PMX, FBX | 없음 | MToon 아님 |

### 외곽선 정보 (반전 헐용)

| 형식 | 출처 | 변환 |
|---|---|---|
| VRM 0.x | `extensions.VRM.materialProperties[]` (재질 이름으로 매칭, MToon만): `_OutlineWidthMode` ≠ 0, `_OutlineWidth`(cm), `_OutlineColor` | cm → m. 화면 모드(2)도 cm로 취급. 색은 sRGB → 선형 |
| VRM 1.0 | `materials[].extensions.VRMC_materials_mtoon`: `outlineWidthMode`, `outlineWidthFactor`, `outlineColorFactor` | `worldCoordinates`: m 그대로. `screenCoordinates`(화면 높이 비율): 모델 키를 곱해 m로 근사 (이 앱은 창을 모델 키에 맞춤) |
| PMX | 재질 플래그 0x10(에지 그리기), 에지 색, 에지 크기 | 크기 1 → 4mm (MMD 에지는 화면 기준이라 단위가 없어 근사) |
| glTF(VRM 아님), FBX | 없음 | 0 (`[renderer] outline = all`이면 렌더러가 4mm 적용) |

## 5. 테스트 항목

모델 파일은 저장소에 없으므로 테스트 데이터를 코드로 만듭니다: `GltfBuilder`(data URI glTF), `PmxBuilder`(PMX 바이너리), 테스트 안의 ASCII FBX 문자열.

| 테스트 파일 | 검증 |
|---|---|
| `ModelFormatTests` | 매직 바이트 판별, 매직이 확장자보다 우선, ASCII FBX(확장자·주석), 미지원 형식 오류 메시지 |
| `ModelLoaderTests` | glTF: 기하·경계, 노드 변환, 프리미티브 이어 붙이기, VRM 0/1 방향, 머티리얼, 스키닝 바인드 포즈, VRM 1.0/0.x 휴머노이드 매핑, 스킨 가중치(관절 → 본 번호), 스킨 없는 메시는 자기 노드에 고정, VRM 1.0/0.x 표정, MToon(0.x 변환·sRGB → 선형, 1.0 값·텍스처 번호, MToon이 아닌 재질의 발광·unlit) |
| `PmxImporterTests` | 미터·오른손·정면 변환, 감기 순서 교환, 머티리얼(색·양면·텍스처·Mask), 본 4개(가변 필드 건너뛰기)와 휴머노이드, UTF-8·인덱스 크기 1/4·추가 UV, 외부 TGA 텍스처, 잘린 파일, 범위 밖 인덱스, BDEF/SDEF 가중치, 정점 모프 → 표정(본 모프 건너뛰기) |
| `FbxImporterTests` | cm → m, UV 원점 뒤집기, 확산색 머티리얼, Mixamo 본 매핑·부모·위치, 손상 파일, 스킨 클러스터 → 정점 가중치 |
| `ModelFormatTests` (SkinWeights) | 같은 본 합치기, 상위 4개, 음수 본 제거, 정규화, 모두 0이면 움직이지 않음 |
| `TexturePaddingTests` | 섬 옆 여백이 섬 색으로 채워짐(지정 거리까지만), 섬 텍셀은 그대로, 삼각형 없으면 변화 없음, 반복 UV, 텍스처별 삼각형 수집 |
| `TextureSourceTests` | PNG는 인코딩 유지, TGA는 RGBA 디코딩, 알 수 없는 바이트, `\` 상대 경로 파일 읽기 |
| `HumanoidTests` | VRM·MMD·Mixamo 이름 매핑 |

수동 확인: VRM 0.x 모델, ufbx 테스트 데이터 `maya_kenney_character_7700_binary.fbx`(본 58개 중 휴머노이드 18개).

## 6. 확장 지점

| TODO | 내용 |
|---|---|
| ~~`TODO(M3)`~~ | ✅ 휴머노이드 본 매핑 (VRM, PMX, FBX — ADR-0009) |
| `TODO(M6)` | VRM 메타(이름, 작가, 라이선스) 파싱과 표시 |
| ~~`TODO(M4)`~~ | ✅ 정점 스킨 가중치(VRM/glTF, PMX, FBX), 표정 모프(VRM, PMX) — ADR-0010 |
| `TODO(M4)` | FBX 블렌드 셰이프 표정, PMX SDEF 정확한 구면 보간 |
| `TODO(M4)` | 모션 파일: VRMA, VMD(MMD), FBX 애니메이션 |
| `TODO(M5)` | ~~MToon 파라미터~~ ✅ (ADR-0012), VRM 1.0 SpringBone, MMD 툰·스피어 텍스처 |
