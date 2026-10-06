#include "anim/Skeleton.h"

#include <algorithm>

namespace deskpet::anim {

Skeleton::Skeleton(const model::Model& model) {
    const std::size_t count = model.bones.size();
    positions_.reserve(count);
    parents_.reserve(count);
    human_.fill(-1);
    for (std::size_t i = 0; i < count; ++i) {
        const model::Bone& bone = model.bones[i];
        positions_.push_back(bone.position);
        const bool validParent = bone.parent >= 0 &&
                                 static_cast<std::size_t>(bone.parent) < count &&
                                 static_cast<std::size_t>(bone.parent) != i;
        parents_.push_back(validParent ? bone.parent : -1);
        const auto human = static_cast<std::size_t>(bone.human);
        if (bone.human != model::HumanBone::None && human < human_.size() && human_[human] < 0) {
            human_[human] = static_cast<int>(i);
        }
    }

    // 깊이(루트까지 거리) 순으로 정렬하면 부모가 항상 먼저 옴. 순환이 있으면 깊이를 count로 제한
    std::vector<std::size_t> depth(count, 0);
    for (std::size_t i = 0; i < count; ++i) {
        for (int p = parents_[i]; p >= 0 && depth[i] < count;
             p = parents_[static_cast<std::size_t>(p)]) {
            ++depth[i];
        }
    }
    order_.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        order_[i] = static_cast<int>(i);
    }
    std::ranges::stable_sort(order_, [&](int a, int b) {
        return depth[static_cast<std::size_t>(a)] < depth[static_cast<std::size_t>(b)];
    });
}

int Skeleton::find(model::HumanBone bone) const noexcept {
    const auto index = static_cast<std::size_t>(bone);
    return index < human_.size() && bone != model::HumanBone::None ? human_[index] : -1;
}

core::Vec3 Skeleton::bindPosition(int bone) const noexcept {
    return bone >= 0 && static_cast<std::size_t>(bone) < positions_.size()
               ? positions_[static_cast<std::size_t>(bone)]
               : core::Vec3{};
}

int Skeleton::parent(int bone) const noexcept {
    return bone >= 0 && static_cast<std::size_t>(bone) < parents_.size()
               ? parents_[static_cast<std::size_t>(bone)]
               : -1;
}

void Skeleton::computePose(std::span<const core::Quat> rotations,
                           std::vector<core::Quat>& accumulated,
                           std::vector<core::Vec3>& posed) const {
    const std::size_t count = positions_.size();
    accumulated.assign(count, core::Quat{});
    posed.assign(count, core::Vec3{});
    for (const int index : order_) {
        const auto j = static_cast<std::size_t>(index);
        const core::Quat delta = j < rotations.size() ? rotations[j] : core::Quat{};
        const int parent = parents_[j];
        if (parent < 0) {
            accumulated[j] = delta;
            posed[j] = positions_[j];  // 루트는 제자리에서 회전
        } else {
            const auto p = static_cast<std::size_t>(parent);
            // 부모 자세가 먼저, 그 위에 이 본의 델타 (해밀턴 곱은 오른쪽이 먼저 적용)
            accumulated[j] = delta * accumulated[p];
            posed[j] = posed[p] + accumulated[p].rotate(positions_[j] - positions_[p]);
        }
    }
}

void Skeleton::computeSkinMatrices(std::span<const core::Quat> rotations,
                                   std::vector<core::Mat4>& out) const {
    computePose(rotations, accumulated_, posed_);
    out.resize(positions_.size());
    for (std::size_t j = 0; j < positions_.size(); ++j) {
        // v' = A(v - p) + P  →  T(-p) · R(A) · T(P)
        out[j] = core::Mat4::translation(positions_[j] * -1.0f) * accumulated_[j].toMat4() *
                 core::Mat4::translation(posed_[j]);
    }
}

}  // namespace deskpet::anim
