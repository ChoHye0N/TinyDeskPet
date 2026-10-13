#include "anim/MotionPlayer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace deskpet::anim {

MotionPlayer::MotionPlayer(const Skeleton& skeleton, std::shared_ptr<const model::MotionClip> clip,
                           std::span<const model::NamedMorph> morphs)
    : clip_(std::move(clip)),
      tracks_(skeleton.size(), nullptr),
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
            tracks_[j] = &clip_->tracks[h];
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
    bindByName(skeleton, morphs);
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

    // 엉덩이 이동: 원본과 대상의 엉덩이 높이 비율 (키가 다른 모델도 같은 비율로 앉고 뜀)
    const int hips = skeleton.find(model::HumanBone::Hips);
    const float targetHeight = hips >= 0 ? skeleton.bindPosition(hips).y : 0.0f;
    if (clip_->hipsHeight > 0.0f && targetHeight > 0.0f) {
        offsetScale_ = targetHeight / clip_->hipsHeight;
        if (!clip_->hipsTranslation.empty()) {
            hipsScale_ = offsetScale_;
        }
    }
}

// 이름으로 맞추는 것들 (ADR-0014·ADR-0015)
void MotionPlayer::bindByName(const Skeleton& skeleton, std::span<const model::NamedMorph> morphs) {
    // 휴머노이드가 아닌 본(머리카락·꼬리 등): 이름이 같은 트랙. 휴머노이드 트랙이 우선이고,
    // 모델에 없는 이름의 트랙은 버림 (그 본들은 부모를 따라가거나 SpringBone이 움직임)
    std::unordered_map<std::string_view, int> byName;
    for (std::size_t j = 0; j < skeleton.size(); ++j) {
        byName.emplace(skeleton.name(static_cast<int>(j)), static_cast<int>(j));
    }
    named_.assign(skeleton.size(), 0);
    offsetTracks_.assign(skeleton.size(), nullptr);
    for (const model::NamedTrack& track : clip_->boneTracks) {
        const auto found = byName.find(track.name);
        if (found == byName.end() || track.rotations.empty()) {
            continue;
        }
        const auto j = static_cast<std::size_t>(found->second);
        if (tracks_[j] == nullptr) {
            tracks_[j] = &track.rotations;
            named_[j] = 1;
            offsetTracks_[j] = track.offsets.empty() ? nullptr : &track.offsets;
        }
    }
    hidden_.assign(skeleton.size(), 0);
    for (const std::string& name : clip_->hiddenBones) {
        if (const auto found = byName.find(name); found != byName.end()) {
            hidden_[static_cast<std::size_t>(found->second)] = 1;
        }
    }
    // 모프 가중치: 노드 이름과 타깃 번호가 같은 대상 모델의 모프. 트랙이 없는 모프는 기본값
    defaultWeights_.clear();
    for (const model::NamedMorph& morph : morphs) {
        defaultWeights_.push_back(morph.defaultWeight);
    }
    for (const model::WeightTrack& track : clip_->weightTracks) {
        const auto found = std::ranges::find_if(morphs, [&](const model::NamedMorph& m) {
            return m.name == track.name && m.target == track.target;
        });
        if (found != morphs.end() && !track.weights.empty()) {
            weightTracks_.emplace_back(static_cast<std::size_t>(found - morphs.begin()),
                                       &track.weights);
        }
    }
}

void MotionPlayer::sampleOffsets(float time, std::vector<core::Vec3>& offsets) const {
    offsets.assign(offsetTracks_.size(), core::Vec3{});
    const FramePosition at = locate(time);
    for (std::size_t j = 0; j < offsetTracks_.size(); ++j) {
        if (offsetTracks_[j] == nullptr) {
            continue;
        }
        const std::vector<core::Vec3>& track = *offsetTracks_[j];
        const std::size_t last = track.size() - 1;
        const core::Vec3 a = track[std::min(at.i0, last)];
        const core::Vec3 b = track[std::min(at.i1, last)];
        offsets[j] = (a + (b - a) * at.weight) * offsetScale_;
    }
}

bool MotionPlayer::sampleWeights(float time, std::vector<float>& weights) const {
    weights = defaultWeights_;
    const FramePosition at = locate(time);
    for (const auto& [morph, track] : weightTracks_) {
        const std::size_t last = track->size() - 1;
        const float a = (*track)[std::min(at.i0, last)];
        const float b = (*track)[std::min(at.i1, last)];
        weights[morph] = a + (b - a) * at.weight;
    }
    return !weightTracks_.empty();
}

MotionPlayer::FramePosition MotionPlayer::locate(float time) const {
    const model::MotionClip& clip = *clip_;
    FramePosition at;
    if (clip.frameCount <= 1) {
        return at;
    }
    const float length = clip.duration();
    float t = std::max(time, 0.0f);
    if (t > length) {
        // 처음 한 번 끝까지 재생한 뒤로는 [loopStart, 끝]을 반복. 반복 구간이 없으면 마지막 자세
        const float start = std::clamp(clip.loopStart, 0.0f, length);
        t = length - start > 1e-6f ? start + std::fmod(t - start, length - start) : length;
    }
    const auto period = static_cast<float>(clip.frameCount - 1);
    const float frame = std::min(t * model::MotionClip::kFramesPerSecond, period);
    at.i0 = std::min(static_cast<std::size_t>(frame), clip.frameCount - 2);
    at.i1 = at.i0 + 1;
    at.weight = frame - static_cast<float>(at.i0);
    at.phase = frame / period;
    return at;
}

core::Vec3 MotionPlayer::rootOffset(float time) const {
    if (hipsScale_ <= 0.0f) {
        return {};
    }
    const std::vector<core::Vec3>& hips = clip_->hipsTranslation;
    const FramePosition at = locate(time);
    const std::size_t last = hips.size() - 1;
    const core::Vec3 a = hips[std::min(at.i0, last)];
    const core::Vec3 b = hips[std::min(at.i1, last)];
    const core::Vec3 now = a + (b - a) * at.weight;
    // 수평: 시작 위치와 시작 → 끝 이동을 빼 반복해도 제자리 (흔들림만 남음). 수직: 그대로
    const core::Vec3 path = hips.front() + (hips.back() - hips.front()) * at.phase;
    return core::Vec3{now.x - path.x, now.y, now.z - path.z} * hipsScale_;
}

void MotionPlayer::sample(float time, std::vector<core::Quat>& rotations) const {
    const FramePosition at = locate(time);
    const std::size_t i0 = at.i0;
    const std::size_t i1 = at.i1;
    const float w = at.weight;

    rotations.resize(parents_.size());
    for (const int bone : order_) {
        const auto j = static_cast<std::size_t>(bone);
        const int parent = parents_[j];
        const core::Quat up =
            parent >= 0 ? accumulated_[static_cast<std::size_t>(parent)] : core::Quat{};
        core::Quat a = up;
        if (tracks_[j] != nullptr) {
            const std::vector<core::Quat>& track = *tracks_[j];
            // 휴머노이드·이름 트랙 모두 프레임 수가 같음 (로더가 같은 샘플링으로 만듦)
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
