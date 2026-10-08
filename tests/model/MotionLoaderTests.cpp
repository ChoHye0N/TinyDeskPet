#include "model/GltfBuilder.h"
#include "model/Motion.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using deskpet::core::Quat;
using deskpet::core::Vec3;
using deskpet::model::HumanBone;
using deskpet::model::loadMotionFromMemory;
using deskpet::model::MotionClip;
using deskpet::model::MotionFormat;
using deskpet::model::MotionLoadResult;
using deskpet::test::GltfBuilder;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

// 같은 회전인지 (q와 −q는 같은 회전)
void expectSameRotation(const Quat& actual, const Quat& expected, float tolerance = 1e-3f) {
    const float d = std::abs(actual.x * expected.x + actual.y * expected.y + actual.z * expected.z +
                             actual.w * expected.w);
    EXPECT_NEAR(d, 1.0f, tolerance) << "actual (" << actual.x << ", " << actual.y << ", "
                                    << actual.z << ", " << actual.w << ")";
}

MotionLoadResult load(std::span<const std::uint8_t> bytes, std::string_view extension = {}) {
    return loadMotionFromMemory(bytes, extension);
}

MotionLoadResult loadText(std::string_view text, std::string_view extension = {}) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(text.data());
    return load(std::span(bytes, text.size()), extension);
}

// ---------------------------------------------------------------------------
// VMD 바이너리 작성 (MMD 좌표: 왼손, 정면 −Z, 본 이름은 Shift-JIS)
// ---------------------------------------------------------------------------

// Shift-JIS 본 이름
constexpr std::string_view kUpperBody = "\x8F\xE3\x94\xBC\x90\x67";          // 上半身
constexpr std::string_view kHead = "\x93\xAA";                               // 頭
constexpr std::string_view kLeftArm = "\x8D\xB6\x98\x72";                    // 左腕
constexpr std::string_view kLeftLeg = "\x8D\xB6\x91\xAB";                    // 左足
constexpr std::string_view kLeftLegIk = "\x8D\xB6\x91\xAB\x82\x68\x82\x6A";  // 左足ＩＫ

struct VmdKey {
    std::string_view name;
    std::uint32_t frame = 0;
    Vec3 position;
    Quat rotation;
};

class VmdBuilder {
public:
    explicit VmdBuilder(std::string_view magic = "Vocaloid Motion Data 0002") {
        text(magic, 30);
        text("model", 20);
    }

    std::vector<std::uint8_t> build(const std::vector<VmdKey>& keys) {
        u32(static_cast<std::uint32_t>(keys.size()));
        for (const VmdKey& key : keys) {
            text(key.name, 15);
            u32(key.frame);
            f32(key.position.x);
            f32(key.position.y);
            f32(key.position.z);
            f32(key.rotation.x);
            f32(key.rotation.y);
            f32(key.rotation.z);
            f32(key.rotation.w);
            // 보간 64바이트: 직선 베지어 (x1 = y1 = 20, x2 = y2 = 107)를 X·Y·Z·회전 모두에
            for (int i = 0; i < 64; ++i) {
                const int row = (i % 16) / 4;
                bytes_.push_back(static_cast<std::uint8_t>(row < 2 ? 20 : 107));
            }
        }
        u32(0);  // 표정 키 수
        return bytes_;
    }

private:
    void text(std::string_view s, std::size_t size) {
        for (std::size_t i = 0; i < size; ++i) {
            bytes_.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : std::uint8_t{0});
        }
    }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void f32(float f) {
        std::uint32_t v = 0;
        std::memcpy(&v, &f, sizeof v);
        u32(v);
    }

    std::vector<std::uint8_t> bytes_;
};

// MMD 좌표의 회전 (축은 MMD 좌표)
Quat mmd(Vec3 axis, float degrees) {
    return Quat::axisAngle(axis, radians(degrees));
}

}  // namespace

TEST(MotionLoader, DetectsFormatByMagic) {
    using deskpet::model::detectMotionFormat;
    const std::vector<std::uint8_t> vmd = VmdBuilder().build({});
    EXPECT_EQ(detectMotionFormat(vmd), MotionFormat::Vmd);
    const std::string_view glb = "glTF\x02\x00\x00\x00";
    EXPECT_EQ(detectMotionFormat(
                  std::span(reinterpret_cast<const std::uint8_t*>(glb.data()), glb.size())),
              MotionFormat::Vrma);
    const std::string_view fbx = "Kaydara FBX Binary  ";
    EXPECT_EQ(detectMotionFormat(
                  std::span(reinterpret_cast<const std::uint8_t*>(fbx.data()), fbx.size())),
              MotionFormat::Fbx);
}

TEST(MotionLoader, UnknownBytes_ReturnError) {
    const MotionLoadResult result = loadText("hello");
    EXPECT_FALSE(result.clip.has_value());
    EXPECT_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// VMD
// ---------------------------------------------------------------------------

TEST(VmdMotion, RotationKeys_AreInterpolatedAt30FpsAndConverted) {
    // 프레임 0 → 10에 左腕을 MMD Z축으로 90°. 직선 보간이면 5프레임에 45°.
    // MMD(왼손) → 공통 규약: z 반전이라 회전축 (x, y, z) → (−x, −y, z)
    const auto vmd = VmdBuilder().build({
        {kLeftArm, 0, {}, Quat{}},
        {kLeftArm, 10, {}, mmd({0.0f, 0.0f, 1.0f}, 90.0f)},
        {kHead, 10, {}, mmd({1.0f, 0.0f, 0.0f}, 30.0f)},
    });
    const MotionLoadResult result = load(vmd);
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const MotionClip& clip = *result.clip;
    EXPECT_EQ(clip.format, MotionFormat::Vmd);
    EXPECT_EQ(clip.frameCount, 11U);
    EXPECT_NEAR(clip.duration(), 10.0f / 30.0f, 1e-6f);

    const auto& arm = clip.track(HumanBone::LeftUpperArm);
    ASSERT_EQ(arm.size(), 11U);
    expectSameRotation(arm[5], Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(45.0f)));
    expectSameRotation(arm[10], Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(90.0f)));
    // 키가 하나뿐인 본은 처음부터 그 값
    expectSameRotation(clip.track(HumanBone::Head)[0],
                       Quat::axisAngle({-1.0f, 0.0f, 0.0f}, radians(30.0f)));
}

TEST(VmdMotion, ChildBones_FollowParentRotation) {
    // 上半身만 돌리면 그 아래(목·머리·팔)의 월드 델타도 같은 회전
    const auto vmd = VmdBuilder().build({{kUpperBody, 0, {}, mmd({0.0f, 1.0f, 0.0f}, 30.0f)}});
    const MotionLoadResult result = load(vmd);
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const Quat expected = Quat::axisAngle({0.0f, -1.0f, 0.0f}, radians(30.0f));
    expectSameRotation(result.clip->track(HumanBone::Spine)[0], expected);
    expectSameRotation(result.clip->track(HumanBone::Head)[0], expected);
    expectSameRotation(result.clip->track(HumanBone::LeftUpperArm)[0], expected);
    expectSameRotation(result.clip->track(HumanBone::Hips)[0], Quat{});  // 下半身은 그대로
}

TEST(VmdMotion, RestDirections_AssumeMmdAPose) {
    const MotionLoadResult result = load(VmdBuilder().build({{kHead, 0, {}, Quat{}}}));
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const Vec3 arm = result.clip->restDirections[static_cast<std::size_t>(HumanBone::LeftUpperArm)];
    // 캐릭터 왼쪽(+X)으로, 수평에서 아래로 약 37°
    EXPECT_GT(arm.x, 0.0f);
    EXPECT_NEAR(std::atan2(-arm.y, arm.x) * 180.0f / kPi, 37.0f, 1.0f);
    const Vec3 leg = result.clip->restDirections[static_cast<std::size_t>(HumanBone::LeftUpperLeg)];
    EXPECT_NEAR(leg.y, -1.0f, 1e-3f);
}

TEST(VmdMotion, LegIk_BendsKneeForward) {
    // 左足ＩＫ를 2(MMD 단위)만큼 올리면 발목이 올라가며 무릎이 앞(공통 규약 +Z)으로 굽음
    const auto vmd = VmdBuilder().build({{kLeftLegIk, 0, {0.0f, 2.0f, 0.0f}, Quat{}}});
    const MotionLoadResult result = load(vmd);
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const MotionClip& clip = *result.clip;
    const auto dir = [&](HumanBone bone) {
        return clip.track(bone)[0].rotate(clip.restDirections[static_cast<std::size_t>(bone)]);
    };
    EXPECT_GT(dir(HumanBone::LeftUpperLeg).z, 0.1f);   // 허벅지는 앞으로
    EXPECT_LT(dir(HumanBone::LeftLowerLeg).z, -0.1f);  // 정강이는 뒤로 내려옴
    // 오른발은 IK 키가 없으므로 그대로
    expectSameRotation(clip.track(HumanBone::RightUpperLeg)[0], Quat{});
}

TEST(VmdMotion, WithoutIkKeys_LegsUseForwardKinematics) {
    const auto vmd = VmdBuilder().build({{kLeftLeg, 0, {}, mmd({1.0f, 0.0f, 0.0f}, 20.0f)}});
    const MotionLoadResult result = load(vmd);
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    expectSameRotation(result.clip->track(HumanBone::LeftUpperLeg)[0],
                       Quat::axisAngle({-1.0f, 0.0f, 0.0f}, radians(20.0f)));
}

TEST(VmdMotion, BadHeaderOrTruncatedData_ReturnError) {
    auto bad = VmdBuilder("Not a motion").build({});
    EXPECT_FALSE(load(bad).clip.has_value());

    auto truncated = VmdBuilder().build({{kHead, 0, {}, Quat{}}});
    truncated.resize(truncated.size() - 40);
    const MotionLoadResult result = load(truncated);
    EXPECT_FALSE(result.clip.has_value());
    EXPECT_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// VRMA (glTF + VRMC_vrm_animation)
// ---------------------------------------------------------------------------

namespace {

// hips(0) ─ spine(1)
//         └ leftUpperArm(2) ─ leftLowerArm(3)
// leftUpperArm을 0 → 1초에 Z축 0 → 90° (선형). hipsRestDegrees: hips의 기본 자세 Y 회전
std::string vrmaJson(GltfBuilder& b, float hipsRestDegrees = 0.0f) {
    const int times = b.addFloats({0.0f, 1.0f}, "SCALAR");
    const float s = std::sin(radians(45.0f));
    const int rotations = b.addFloats({0, 0, 0, 1, 0, 0, s, s}, "VEC4");
    const float h = radians(hipsRestDegrees) * 0.5f;
    return R"("nodes":[{"name":"hips","translation":[0,1,0],"rotation":[0,)" +
           std::to_string(std::sin(h)) + ",0," + std::to_string(std::cos(h)) +
           R"(],"children":[1,2]},)"
           R"({"name":"spine","translation":[0,0.2,0]},)"
           R"({"name":"leftUpperArm","translation":[0.2,0.3,0],"children":[3]},)"
           R"({"name":"leftLowerArm","translation":[0.3,0,0]}],)"
           R"("scenes":[{"nodes":[0]}],"scene":0,)"
           R"("animations":[{"channels":[{"sampler":0,"target":{"node":2,"path":"rotation"}}],)"
           R"("samplers":[{"input":)" +
           std::to_string(times) + R"(,"output":)" + std::to_string(rotations) +
           R"(,"interpolation":"LINEAR"}]}],)"
           R"("extensionsUsed":["VRMC_vrm_animation"],"extensions":{"VRMC_vrm_animation":{)"
           R"("specVersion":"1.0","humanoid":{"humanBones":{"hips":{"node":0},"spine":{"node":1},)"
           R"("leftUpperArm":{"node":2},"leftLowerArm":{"node":3}}}}})";
}

}  // namespace

TEST(VrmaMotion, RotationChannel_IsResampledAsWorldDelta) {
    GltfBuilder b;
    const std::string json = b.build(vrmaJson(b));
    const MotionLoadResult result = loadText(json, ".vrma");
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const MotionClip& clip = *result.clip;
    EXPECT_EQ(clip.format, MotionFormat::Vrma);
    ASSERT_EQ(clip.frameCount, 31U);  // 1초 × 30fps + 1

    const Quat half = Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(45.0f));
    expectSameRotation(clip.track(HumanBone::LeftUpperArm)[15], half);
    expectSameRotation(clip.track(HumanBone::LeftLowerArm)[15], half);  // 자식도 같이 돎
    expectSameRotation(clip.track(HumanBone::Spine)[15], Quat{});
    expectSameRotation(clip.track(HumanBone::Hips)[30], Quat{});

    // 기본 자세 방향: 위팔 → 아래팔 = +X
    const Vec3 arm = clip.restDirections[static_cast<std::size_t>(HumanBone::LeftUpperArm)];
    EXPECT_NEAR(arm.x, 1.0f, 1e-5f);
}

TEST(VrmaMotion, ParentRestRotation_IsExpressedInModelAxes) {
    // hips가 기본 자세에서 Y축 90° 돌아 있으면, 위팔의 로컬 Z축 회전은 모델 공간에서 X축 회전
    GltfBuilder b;
    const std::string json = b.build(vrmaJson(b, 90.0f));
    const MotionLoadResult result = loadText(json, ".vrma");
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    expectSameRotation(result.clip->track(HumanBone::LeftUpperArm)[30],
                       Quat::axisAngle({1.0f, 0.0f, 0.0f}, radians(90.0f)));
}

TEST(VrmaMotion, PlainGltfWithoutExtension_ReturnsError) {
    GltfBuilder b;
    const MotionLoadResult result = loadText(b.build(R"("nodes":[{"name":"a"}])"), ".glb");
    EXPECT_FALSE(result.clip.has_value());
    EXPECT_FALSE(result.error.empty());
}

// ---------------------------------------------------------------------------
// FBX (ufbx로 애니메이션 평가)
// ---------------------------------------------------------------------------

namespace {

// Mixamo 이름의 본 2개(Hips ─ LeftArm). LeftArm의 Lcl Rotation Z: 0 → 90° (1초, 선형)
// FBX 시간 단위: 1초 = 46186158000
constexpr std::string_view kAnimatedFbx = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",1
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",2
		P: "FrontAxisSign", "int", "Integer", "",1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Model: 300, "Model::mixamorig:Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,100,0
		}
	}
	Model: 400, "Model::mixamorig:LeftArm", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",20,50,0
		}
	}
	Model: 410, "Model::mixamorig:LeftForeArm", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",30,0,0
		}
	}
	AnimationStack: 600, "AnimStack::Take 001", "" {
		Properties70:  {
			P: "LocalStop", "KTime", "Time", "",46186158000
			P: "ReferenceStop", "KTime", "Time", "",46186158000
		}
	}
	AnimationLayer: 700, "AnimLayer::BaseLayer", "" {
	}
	AnimationCurveNode: 800, "AnimCurveNode::R", "" {
		Properties70:  {
			P: "d|X", "Number", "", "A",0
			P: "d|Y", "Number", "", "A",0
			P: "d|Z", "Number", "", "A",0
		}
	}
	AnimationCurve: 900, "AnimCurve::", "" {
		Default: 0
		KeyVer: 4009
		KeyTime: *2 {
			a: 0,46186158000
		}
		KeyValueFloat: *2 {
			a: 0,90
		}
		KeyAttrFlags: *1 {
			a: 24836
		}
		KeyAttrDataFloat: *4 {
			a: 0,0,218434821,0
		}
		KeyAttrRefCount: *1 {
			a: 2
		}
	}
}
Connections:  {
	C: "OO",300,0
	C: "OO",400,300
	C: "OO",410,400
	C: "OO",700,600
	C: "OO",800,700
	C: "OP",800,400, "Lcl Rotation"
	C: "OP",900,800, "d|Z"
}
)";

}  // namespace

TEST(FbxMotion, AnimatedRotation_IsResampledAsWorldDelta) {
    const MotionLoadResult result = loadText(kAnimatedFbx, ".fbx");
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    const MotionClip& clip = *result.clip;
    EXPECT_EQ(clip.format, MotionFormat::Fbx);
    ASSERT_EQ(clip.frameCount, 31U);
    const Quat half = Quat::axisAngle({0.0f, 0.0f, 1.0f}, radians(45.0f));
    expectSameRotation(clip.track(HumanBone::LeftUpperArm)[15], half);
    expectSameRotation(clip.track(HumanBone::LeftLowerArm)[15], half);
    expectSameRotation(clip.track(HumanBone::Hips)[15], Quat{});
    const Vec3 arm = clip.restDirections[static_cast<std::size_t>(HumanBone::LeftUpperArm)];
    EXPECT_NEAR(arm.x, 1.0f, 1e-4f);
}

TEST(FbxMotion, FileWithoutAnimation_ReturnsError) {
    constexpr std::string_view kStatic = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
Objects:  {
	Model: 300, "Model::mixamorig:Hips", "LimbNode" {
		Version: 232
	}
}
Connections:  {
	C: "OO",300,0
}
)";
    const MotionLoadResult result = loadText(kStatic, ".fbx");
    EXPECT_FALSE(result.clip.has_value());
    EXPECT_FALSE(result.error.empty());
}

TEST(VmdMotion, DuplicateFrameKeys_LastInFileWins) {
    const auto vmd = VmdBuilder().build({
        {kHead, 0, {}, mmd({1.0f, 0.0f, 0.0f}, 10.0f)},
        {kHead, 0, {}, mmd({1.0f, 0.0f, 0.0f}, 40.0f)},
    });
    const MotionLoadResult result = load(vmd);
    ASSERT_TRUE(result.clip.has_value()) << result.error;
    expectSameRotation(result.clip->track(HumanBone::Head)[0],
                       Quat::axisAngle({-1.0f, 0.0f, 0.0f}, radians(40.0f)));
}
