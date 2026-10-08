#include "anim/MotionPlayer.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace deskpet::anim {

MotionPlayer::MotionPlayer(const Skeleton& skeleton, std::shared_ptr<const model::MotionClip> clip)
    : clip_(std::move(clip)),
      tracks_(skeleton.size(), -1),
      corrections_(skeleton.size()),
      accumulated_(skeleton.size()) {
    const std::span<const int> order = skeleton.order();
    order_.assign(order.begin(), order.end());
    parents_.resize(skeleton.size());
    for (std::size_t j = 0; j < skeleton.size(); ++j) {
        parents_[j] = skeleton.parent(static_cast<int>(j));
    }

    std::vector<core::Quat> own(skeleton.size());
    std::vector<bool> hasOwn(skeleton.size(), false);
    for (std::size_t h = 1; h < model::kHumanBoneCount; ++h) {
        const auto human = static_cast<model::HumanBone>(h);
        const int bone = skeleton.find(human);
        if (bone < 0) {
            continue;
        }
        const auto j = static_cast<std::size_t>(bone);
        if (!clip_->tracks[h].empty()) {
            tracks_[j] = static_cast<int>(h);
        }
        // 대상의 기본 자세 방향 → 원본의 기본 자세 방향
        const core::Vec3 source = clip_->restDirections[h];
        const int child = skeleton.find(model::limbChild(human));
        if (child < 0 || core::dot(source, source) < 1e-6f) {
            continue;
        }
        const core::Vec3 target = skeleton.bindPosition(child) - skeleton.bindPosition(bone);
        if (core::dot(target, target) > 1e-12f) {
            own[j] = core::Quat::fromTo(core::normalize(target), core::normalize(source));
            hasOwn[j] = true;
        }
    }
    // 방향을 모르는 본(몸통, 손, 중간 본)은 부모의 보정을 물려받음: 손은 아래팔과 같이 돎
    for (const int bone : order_) {
        const auto j = static_cast<std::size_t>(bone);
        const int parent = parents_[j];
        if (hasOwn[j]) {
            corrections_[j] = own[j];
        } else if (parent >= 0) {
            corrections_[j] = corrections_[static_cast<std::size_t>(parent)];
        }
    }
}

void MotionPlayer::sample(float time, std::vector<core::Quat>& rotations) const {
    const model::MotionClip& clip = *clip_;
    std::size_t i0 = 0;
    std::size_t i1 = 0;
    float w = 0.0f;
    if (clip.frameCount > 1) {
        const auto period = static_cast<float>(clip.frameCount - 1);
        float frame = std::fmod(time * model::MotionClip::kFramesPerSecond, period);
        if (frame < 0.0f) {
            frame += period;
        }
        i0 = std::min(static_cast<std::size_t>(frame), clip.frameCount - 2);
        i1 = i0 + 1;
        w = frame - static_cast<float>(i0);
    }

    rotations.resize(parents_.size());
    for (const int bone : order_) {
        const auto j = static_cast<std::size_t>(bone);
        const int parent = parents_[j];
        const core::Quat up =
            parent >= 0 ? accumulated_[static_cast<std::size_t>(parent)] : core::Quat{};
        core::Quat a = up;
        if (tracks_[j] >= 0) {
            const auto& track = clip.tracks[static_cast<std::size_t>(tracks_[j])];
            const std::size_t last = track.size() - 1;
            // 대상 기본 자세를 원본 기본 자세로 돌린(C) 뒤 원본의 움직임(Δ)
            a = core::slerp(track[std::min(i0, last)], track[std::min(i1, last)], w) *
                corrections_[j];
        }
        accumulated_[j] = a;
        rotations[j] = a * up.conjugate();  // Aⱼ = Δⱼ · A부모 → Δⱼ = Aⱼ · A부모⁻¹
    }
}

}  // namespace deskpet::anim
