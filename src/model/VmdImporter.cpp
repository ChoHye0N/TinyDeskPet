// VMD (MikuMikuDance 모션) 로더 (직접 구현, ADR-0013). 명세: docs/03-detailed-design/model.md §4.9
//
// VMD에는 뼈대가 없고 본 이름(Shift-JIS)별 키프레임만 있습니다. MMD 본은 기본 자세 회전이 없어
// (모든 본 축 = 모델 축) 로컬 회전을 표준 MMD 뼈대 계층으로 곱하면 월드 델타가 됩니다.
// 다리는 대부분 足ＩＫ(발목 위치)로 움직이므로, 표준 다리 길이로 2관절 IK를 풀어 회전으로 바꿉니다.

#include "model/Importers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <vector>

namespace deskpet::model::detail {
namespace {

using core::Quat;
using core::Vec3;

constexpr std::size_t kMagicSize = 30;
constexpr std::size_t kNameSize = 15;
constexpr std::size_t kKeySize = 111;  // 이름 15 + 프레임 4 + 위치 12 + 회전 16 + 보간 64
constexpr std::uint32_t kMaxFrame = 30U * 60U * 30U;  // 30분 (메모리 보호)

// MMD 표준 뼈대 (부모가 먼저 오는 순서). VMD 키가 없는 본은 회전 없음
enum MmdBone : std::uint8_t {
    Root,
    Center,
    Groove,
    Waist,
    UpperBody,
    UpperBody2,
    UpperBody3,
    Neck,
    Head,
    LowerBody,
    LShoulder,
    LArm,
    LArmTwist,
    LElbow,
    LHandTwist,
    LWrist,
    RShoulder,
    RArm,
    RArmTwist,
    RElbow,
    RHandTwist,
    RWrist,
    LLeg,
    LKnee,
    LAnkle,
    LToe,
    RLeg,
    RKnee,
    RAnkle,
    RToe,
    LLegIk,
    RLegIk,
    Count
};

struct BoneInfo {
    std::string_view sjis;  // Shift-JIS 이름
    int parent = -1;
    HumanBone human = HumanBone::None;
    Vec3 rest;  // 표준 모델의 기본 자세 위치 (MMD 좌표·단위). 다리 길이·팔 각도만 의미 있음
};

// 팔은 MMD 모델의 일반적인 A포즈: 수평에서 37° 아래 (cos 0.7986, sin 0.6018)
constexpr std::array<BoneInfo, Count> kBones = {{
    {"\x91\x53\x82\xC4\x82\xCC\x90\x65", -1, HumanBone::None, {0.0f, 0.0f, 0.0f}},  // 全ての親
    {"\x83\x5A\x83\x93\x83\x5E\x81\x5B", Root, HumanBone::None, {0.0f, 8.0f, 0.0f}},  // センター
    {"\x83\x4F\x83\x8B\x81\x5B\x83\x75", Center, HumanBone::None, {0.0f, 8.2f, 0.0f}},  // グルーブ
    {"\x8D\x98", Groove, HumanBone::None, {0.0f, 11.2f, 0.0f}},                         // 腰
    {"\x8F\xE3\x94\xBC\x90\x67", Waist, HumanBone::Spine, {0.0f, 11.6f, 0.0f}},         // 上半身
    {"\x8F\xE3\x94\xBC\x90\x67\x32", UpperBody, HumanBone::Chest, {0.0f, 12.8f, 0.0f}},  // 上半身2
    {"\x8F\xE3\x94\xBC\x90\x67\x33", UpperBody2, HumanBone::UpperChest, {0.0f, 14.0f, 0.0f}},
    {"\x8E\xF1", UpperBody3, HumanBone::Neck, {0.0f, 15.9f, 0.0f}},                   // 首
    {"\x93\xAA", Neck, HumanBone::Head, {0.0f, 16.6f, 0.0f}},                         // 頭
    {"\x89\xBA\x94\xBC\x90\x67", Waist, HumanBone::Hips, {0.0f, 11.6f, 0.0f}},        // 下半身
    {"\x8D\xB6\x8C\xA8", UpperBody3, HumanBone::LeftShoulder, {0.35f, 15.4f, 0.0f}},  // 左肩
    {"\x8D\xB6\x98\x72", LShoulder, HumanBone::LeftUpperArm, {1.3f, 15.2f, 0.0f}},    // 左腕
    {"\x8D\xB6\x98\x72\x9D\x80", LArm, HumanBone::None, {2.498f, 14.297f, 0.0f}},     // 左腕捩
    {"\x8D\xB6\x82\xD0\x82\xB6", LArmTwist, HumanBone::LeftLowerArm, {3.696f, 13.395f, 0.0f}},
    {"\x8D\xB6\x8E\xE8\x9D\x80", LElbow, HumanBone::None, {4.774f, 12.583f, 0.0f}},  // 左手捩
    {"\x8D\xB6\x8E\xE8\x8E\xF1", LHandTwist, HumanBone::LeftHand, {5.852f, 11.770f, 0.0f}},
    {"\x89\x45\x8C\xA8", UpperBody3, HumanBone::RightShoulder, {-0.35f, 15.4f, 0.0f}},  // 右肩
    {"\x89\x45\x98\x72", RShoulder, HumanBone::RightUpperArm, {-1.3f, 15.2f, 0.0f}},    // 右腕
    {"\x89\x45\x98\x72\x9D\x80", RArm, HumanBone::None, {-2.498f, 14.297f, 0.0f}},      // 右腕捩
    {"\x89\x45\x82\xD0\x82\xB6", RArmTwist, HumanBone::RightLowerArm, {-3.696f, 13.395f, 0.0f}},
    {"\x89\x45\x8E\xE8\x9D\x80", RElbow, HumanBone::None, {-4.774f, 12.583f, 0.0f}},  // 右手捩
    {"\x89\x45\x8E\xE8\x8E\xF1", RHandTwist, HumanBone::RightHand, {-5.852f, 11.770f, 0.0f}},
    {"\x8D\xB6\x91\xAB", LowerBody, HumanBone::LeftUpperLeg, {1.0f, 10.6f, 0.0f}},    // 左足
    {"\x8D\xB6\x82\xD0\x82\xB4", LLeg, HumanBone::LeftLowerLeg, {1.0f, 6.0f, 0.0f}},  // 左ひざ
    {"\x8D\xB6\x91\xAB\x8E\xF1", LKnee, HumanBone::LeftFoot, {1.0f, 1.4f, 0.0f}},     // 左足首
    {"\x8D\xB6\x82\xC2\x82\xDC\x90\xE6", LAnkle, HumanBone::LeftToes, {1.0f, 0.0f, -1.5f}},
    {"\x89\x45\x91\xAB", LowerBody, HumanBone::RightUpperLeg, {-1.0f, 10.6f, 0.0f}},    // 右足
    {"\x89\x45\x82\xD0\x82\xB4", RLeg, HumanBone::RightLowerLeg, {-1.0f, 6.0f, 0.0f}},  // 右ひざ
    {"\x89\x45\x91\xAB\x8E\xF1", RKnee, HumanBone::RightFoot, {-1.0f, 1.4f, 0.0f}},     // 右足首
    {"\x89\x45\x82\xC2\x82\xDC\x90\xE6", RAnkle, HumanBone::RightToes, {-1.0f, 0.0f, -1.5f}},
    {"\x8D\xB6\x91\xAB\x82\x68\x82\x6A", Root, HumanBone::None, {1.0f, 1.4f, 0.0f}},  // 左足ＩＫ
    {"\x89\x45\x91\xAB\x82\x68\x82\x6A", Root, HumanBone::None, {-1.0f, 1.4f, 0.0f}},  // 右足ＩＫ
}};

// 3차 베지어 보간 곡선 (0,0) → (x1,y1), (x2,y2) → (1,1). VMD는 0 ~ 127 정수로 저장
struct Curve {
    float x1 = 0.25f;
    float y1 = 0.25f;
    float x2 = 0.75f;
    float y2 = 0.75f;
};

// 진행률 x(0 ~ 1)에서의 보간 값. x(s)가 단조 증가하므로 이분법으로 s를 찾음
float evaluate(const Curve& c, float x) {
    if (c.x1 == c.y1 && c.x2 == c.y2) {
        return x;  // 직선
    }
    const auto bezier = [](float p1, float p2, float s) {
        const float r = 1.0f - s;
        return 3.0f * r * r * s * p1 + 3.0f * r * s * s * p2 + s * s * s;
    };
    float lo = 0.0f;
    float hi = 1.0f;
    float s = x;
    for (int i = 0; i < 20; ++i) {
        s = (lo + hi) * 0.5f;
        if (bezier(c.x1, c.x2, s) < x) {
            lo = s;
        } else {
            hi = s;
        }
    }
    return bezier(c.y1, c.y2, s);
}

struct Key {
    std::uint32_t frame = 0;
    Vec3 position;
    Quat rotation;
    std::array<Curve, 4> curves;  // X, Y, Z 이동, 회전. 이전 키 → 이 키 구간에 적용
};

struct Pose {
    Vec3 position;
    Quat rotation;
};

Pose sample(const std::vector<Key>& keys, float frame) {
    if (keys.empty()) {
        return {};
    }
    const auto next = std::ranges::upper_bound(
        keys, frame, {}, [](const Key& k) { return static_cast<float>(k.frame); });
    if (next == keys.begin()) {
        return {keys.front().position, keys.front().rotation};
    }
    if (next == keys.end()) {
        return {keys.back().position, keys.back().rotation};
    }
    const Key& a = *(next - 1);
    const Key& b = *next;
    const float x = (frame - static_cast<float>(a.frame)) / static_cast<float>(b.frame - a.frame);
    const auto mix = [&](float from, float to, int curve) {
        return from + (to - from) * evaluate(b.curves[static_cast<std::size_t>(curve)], x);
    };
    return {{mix(a.position.x, b.position.x, 0), mix(a.position.y, b.position.y, 1),
             mix(a.position.z, b.position.z, 2)},
            core::slerp(a.rotation, b.rotation, evaluate(b.curves[3], x))};
}

// MMD(왼손, 정면 −Z) → 공통 규약(오른손, 정면 +Z): z 반전. 회전은 반사 M에 대해 M·R·M이라
// 쿼터니언 축 (x, y, z) → det(M)·M·축 = (−x, −y, z)
Quat toModelSpace(Quat q) {
    return {-q.x, -q.y, q.z, q.w};
}

Vec3 toModelSpace(Vec3 p) {
    return {p.x, p.y, -p.z};
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] std::size_t remaining() const noexcept { return bytes_.size() - pos_; }

    std::span<const std::uint8_t> take(std::size_t n) {
        const auto out = bytes_.subspan(pos_, n);
        pos_ += n;
        return out;
    }
    std::uint32_t u32() {
        const auto b = take(4);
        return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8U) |
               (static_cast<std::uint32_t>(b[2]) << 16U) |
               (static_cast<std::uint32_t>(b[3]) << 24U);
    }
    float f32() {
        const std::uint32_t bits = u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

int findBone(std::span<const std::uint8_t> name) {
    const auto end = std::ranges::find(name, std::uint8_t{0});
    const std::string_view text(reinterpret_cast<const char*>(name.data()),
                                static_cast<std::size_t>(end - name.begin()));
    for (std::size_t i = 0; i < kBones.size(); ++i) {
        if (kBones[i].sjis == text) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Key readKey(Reader& reader, std::uint32_t frame) {
    Key key;
    key.frame = frame;
    key.position = {reader.f32(), reader.f32(), reader.f32()};
    key.rotation = Quat{reader.f32(), reader.f32(), reader.f32(), reader.f32()}.normalized();
    const auto interpolation = reader.take(64);
    for (std::size_t c = 0; c < 4; ++c) {
        // 채널 c의 (x1, y1, x2, y2) = 바이트 [c], [c+4], [c+8], [c+12]
        key.curves[c] = {static_cast<float>(interpolation[c]) / 127.0f,
                         static_cast<float>(interpolation[c + 4]) / 127.0f,
                         static_cast<float>(interpolation[c + 8]) / 127.0f,
                         static_cast<float>(interpolation[c + 12]) / 127.0f};
    }
    return key;
}

// 한 프레임의 표준 뼈대 자세 (MMD 좌표)
struct Frame {
    std::array<Quat, Count> world{};
    std::array<Vec3, Count> position{};
};

// 2관절 IK: 허벅지·정강이 길이를 유지하며 발목을 IK 본 위치로. 무릎은 下半身 기준 앞(−Z)으로
// 굽힘. 회전은 부모(下半身) 자세 위에서 방향만 맞춰 다리의 비틀림이 골반을 따라가게 함
void solveLeg(Frame& f, MmdBone leg, MmdBone knee, MmdBone ankle, MmdBone ik) {
    const Vec3 hip = f.position[leg];
    const Vec3 thighRest = kBones[knee].rest - kBones[leg].rest;
    const Vec3 shinRest = kBones[ankle].rest - kBones[knee].rest;
    const float a = thighRest.length();
    const float b = shinRest.length();

    Vec3 toTarget = f.position[ik] - hip;
    float d = toTarget.length();
    const Quat pelvis = f.world[LowerBody];
    const Vec3 u = d > 1e-6f ? toTarget * (1.0f / d) : pelvis.rotate({0.0f, -1.0f, 0.0f});
    d = std::clamp(d, std::abs(a - b) + 1e-4f, (a + b) * 0.9999f);

    const Vec3 forward = pelvis.rotate({0.0f, 0.0f, -1.0f});
    Vec3 pole = forward - u * core::dot(forward, u);
    if (core::dot(pole, pole) < 1e-8f) {
        pole = pelvis.rotate({0.0f, 1.0f, 0.0f});  // 다리가 정확히 앞을 향함: 위쪽으로
        pole = pole - u * core::dot(pole, u);
    }
    pole = core::normalize(pole);

    const float cosHip = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
    const float sinHip = std::sqrt(1.0f - cosHip * cosHip);
    const Vec3 kneePosition = hip + (u * cosHip + pole * sinHip) * a;
    const Vec3 anklePosition = hip + u * d;

    const Quat parent = f.world[static_cast<std::size_t>(kBones[leg].parent)];
    f.world[leg] = Quat::fromTo(core::normalize(parent.rotate(thighRest)),
                                core::normalize(kneePosition - hip)) *
                   parent;
    f.world[knee] = Quat::fromTo(core::normalize(f.world[leg].rotate(shinRest)),
                                 core::normalize(anklePosition - kneePosition)) *
                    f.world[leg];
    f.world[ankle] = f.world[ik];  // 발 방향은 IK 본의 회전을 따름 (つま先ＩＫ 근사)
}

class VmdConverter {
public:
    std::optional<MotionClip> run(Reader& reader, std::string& error) {
        if (!readKeys(reader, error)) {
            return std::nullopt;
        }
        MotionClip clip;
        clip.format = MotionFormat::Vmd;
        clip.frameCount = static_cast<std::size_t>(lastFrame_) + 1;
        for (const BoneInfo& bone : kBones) {
            if (bone.human != HumanBone::None) {
                clip.track(bone.human).reserve(clip.frameCount);
            }
        }
        const bool leftIk = !keys_[LLegIk].empty();
        const bool rightIk = !keys_[RLegIk].empty();
        for (std::size_t i = 0; i < clip.frameCount; ++i) {
            const Frame f = pose(static_cast<float>(i), leftIk, rightIk);
            for (std::size_t b = 0; b < kBones.size(); ++b) {
                if (kBones[b].human != HumanBone::None) {
                    clip.track(kBones[b].human).push_back(toModelSpace(f.world[b]));
                }
            }
        }
        std::array<std::optional<Vec3>, kHumanBoneCount> rest{};
        for (const BoneInfo& bone : kBones) {
            if (bone.human != HumanBone::None) {
                rest[static_cast<std::size_t>(bone.human)] = toModelSpace(bone.rest);
            }
        }
        motion::setRestDirections(clip, rest);
        return clip;
    }

private:
    bool readKeys(Reader& reader, std::string& error) {
        if (reader.remaining() < 4) {
            error = "VMD 파일이 잘렸습니다 (본 키 수 없음)";
            return false;
        }
        const std::uint32_t count = reader.u32();
        if (static_cast<std::uint64_t>(count) * kKeySize > reader.remaining()) {
            error = std::format("VMD 파일이 잘렸습니다 (본 키 {}개)", count);
            return false;
        }
        bool any = false;
        for (std::uint32_t i = 0; i < count; ++i) {
            const int bone = findBone(reader.take(kNameSize));
            const std::uint32_t frame = reader.u32();
            if (bone < 0 || frame > kMaxFrame) {
                reader.take(kKeySize - kNameSize - 4);  // 모르는 본(손가락, 눈 등)은 건너뜀
                continue;
            }
            keys_[static_cast<std::size_t>(bone)].push_back(readKey(reader, frame));
            lastFrame_ = std::max(lastFrame_, frame);
            any = true;
        }
        if (!any) {
            error = "VMD에 휴머노이드로 쓸 수 있는 본 키가 없습니다";
            return false;
        }
        for (auto& keys : keys_) {
            // 같은 프레임 키가 여러 개면 파일에서 마지막 것 (stable_sort라 파일 순서 유지).
            // 뒤에서부터 unique → 남는 키가 배열 뒤쪽에 모이므로 앞쪽 나머지를 지움
            std::ranges::stable_sort(keys, {}, &Key::frame);
            const auto removed = std::ranges::unique(keys.rbegin(), keys.rend(), {}, &Key::frame);
            keys.erase(keys.begin(), removed.begin().base());
        }
        return true;
    }

    [[nodiscard]] Frame pose(float frame, bool leftIk, bool rightIk) const {
        Frame f;
        for (std::size_t b = 0; b < kBones.size(); ++b) {
            const Pose p = sample(keys_[b], frame);
            const int parent = kBones[b].parent;
            if (parent < 0) {
                f.world[b] = p.rotation;
                f.position[b] = kBones[b].rest + p.position;
                continue;
            }
            const auto up = static_cast<std::size_t>(parent);
            // MMD 본: 부모 자세 위에 로컬 회전 (W = W부모 · q), 이동은 부모 축 기준
            f.world[b] = f.world[up] * p.rotation;
            f.position[b] =
                f.position[up] + f.world[up].rotate(kBones[b].rest - kBones[up].rest + p.position);
        }
        if (leftIk) {
            solveLeg(f, LLeg, LKnee, LAnkle, LLegIk);
            f.world[LToe] = f.world[LAnkle] * sample(keys_[LToe], frame).rotation;
        }
        if (rightIk) {
            solveLeg(f, RLeg, RKnee, RAnkle, RLegIk);
            f.world[RToe] = f.world[RAnkle] * sample(keys_[RToe], frame).rotation;
        }
        return f;
    }

    std::array<std::vector<Key>, Count> keys_;
    std::uint32_t lastFrame_ = 0;
};

}  // namespace

MotionLoadResult importVmd(std::span<const std::uint8_t> bytes) {
    // 버전 2 ("Vocaloid Motion Data 0002", 모델 이름 20바이트)와 옛 버전 ("... file", 10바이트)
    const std::string_view magic(reinterpret_cast<const char*>(bytes.data()),
                                 std::min(bytes.size(), kMagicSize));
    std::size_t nameSize = 0;
    if (magic.starts_with("Vocaloid Motion Data 0002")) {
        nameSize = 20;
    } else if (magic.starts_with("Vocaloid Motion Data file")) {
        nameSize = 10;
    } else {
        return {std::nullopt, "VMD 헤더가 아닙니다"};
    }
    if (bytes.size() < kMagicSize + nameSize) {
        return {std::nullopt, "VMD 파일이 잘렸습니다 (헤더)"};
    }
    Reader reader(bytes);
    reader.take(kMagicSize + nameSize);
    MotionLoadResult result;
    result.clip = VmdConverter().run(reader, result.error);
    return result;
}

}  // namespace deskpet::model::detail
