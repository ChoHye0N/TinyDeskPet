#include "anim/SpringBone.h"

#include <algorithm>
#include <cmath>

namespace deskpet::anim {
namespace {

constexpr float kLeafTailLength =
    0.07f;  // 자식이 없는 본의 끝: 부모 → 본 방향으로 7cm (UniVRM과 같음)
constexpr float kStepSeconds = 1.0f / 60.0f;
constexpr int kMaxSteps = 4;
// 한 프레임에 이보다 멀리 움직이면 끌기·던지기가 아니라 순간 이동(위치 초기화, 크기 변경 등)
constexpr float kTeleportMeters = 0.5f;
// 끝이 한 단계에 이보다 덜 움직이면 멈춘 것으로 보고 그대로 둠. 미세한 오차로 매 프레임
// 자세가 바뀌면 화면이 같지 않아 Present 생략(DEBT-02)이 안 되기 때문
constexpr float kSettleMeters = 1e-5f;

float length(core::Vec3 v) {
    return std::sqrt(core::dot(v, v));
}

// 본별 자식 목록 (본 배열 순서)
std::vector<std::vector<int>> childrenOf(const model::Model& model) {
    const std::size_t count = model.bones.size();
    std::vector<std::vector<int>> children(count);
    for (std::size_t i = 0; i < count; ++i) {
        const int parent = model.bones[i].parent;
        if (parent >= 0 && static_cast<std::size_t>(parent) < count) {
            children[static_cast<std::size_t>(parent)].push_back(static_cast<int>(i));
        }
    }
    return children;
}

// 본의 끝: 첫 자식 위치. 자식이 없으면 부모 → 본 방향으로 7cm (UniVRM과 같음)
core::Vec3 tailOf(const model::Model& model, int bone, const std::vector<int>& kids) {
    const model::Bone& b = model.bones[static_cast<std::size_t>(bone)];
    if (!kids.empty()) {
        return model.bones[static_cast<std::size_t>(kids.front())].position;
    }
    if (b.parent >= 0) {
        const core::Vec3 dir =
            b.position - model.bones[static_cast<std::size_t>(b.parent)].position;
        const float len = length(dir);
        if (len > 1e-6f) {
            return b.position + dir * (kLeafTailLength / len);
        }
    }
    return b.position + core::Vec3{0.0f, -kLeafTailLength, 0.0f};
}

}  // namespace

SpringBoneSimulator::SpringBoneSimulator(const model::Model& model) : model_(model) {
    const std::vector<std::vector<int>> children = childrenOf(model);
    std::vector<bool> used(model.bones.size(), false);  // 여러 그룹에 같은 본이 있으면 처음 그룹만
    int group = 0;
    for (const model::SpringGroup& spring : model.springGroups) {
        for (const int root : spring.roots) {
            addChain(root, group, children, used);
        }
        ++group;
    }
}

// root와 자손 전체를 전위 순회로 추가: 부모가 자식보다 먼저 joints_에 들어감
void SpringBoneSimulator::addChain(int root, int group,
                                   const std::vector<std::vector<int>>& children,
                                   std::vector<bool>& used) {
    std::vector<int> stack{root};
    while (!stack.empty()) {
        const int bone = stack.back();
        stack.pop_back();
        if (bone < 0 || static_cast<std::size_t>(bone) >= used.size() ||
            used[static_cast<std::size_t>(bone)]) {
            continue;
        }
        used[static_cast<std::size_t>(bone)] = true;
        const auto& kids = children[static_cast<std::size_t>(bone)];
        stack.insert(stack.end(), kids.rbegin(), kids.rend());

        const model::Bone& b = model_.bones[static_cast<std::size_t>(bone)];
        Joint joint;
        joint.bone = bone;
        joint.parent = b.parent;
        joint.axis = tailOf(model_, bone, kids) - b.position;
        joint.length = length(joint.axis);
        joint.group = group;
        if (joint.length > 1e-6f) {
            joints_.push_back(joint);
        }
    }
}

void SpringBoneSimulator::update(const Skeleton& skeleton, std::vector<core::Quat>& rotations,
                                 core::Vec3 movement, float dt) {
    if (joints_.empty() || dt <= 0.0f) {
        return;
    }
    rotations.resize(skeleton.size());
    if (length(movement) > kTeleportMeters) {
        reset();
        movement = {};
    }

    // 고정 간격으로 나눠 계산 (프레임이 밀려 dt가 커져도 같은 결과에 가깝게)
    const int steps = std::clamp(static_cast<int>(std::lround(dt / kStepSeconds)), 1, kMaxSteps);
    const float stepDt = dt / static_cast<float>(steps);
    const core::Vec3 stepMove = movement * (1.0f / static_cast<float>(steps));
    const std::vector<core::Quat> base = rotations;
    for (int i = 0; i < steps; ++i) {
        rotations = base;
        if (initialized_) {
            // 캐릭터가 +m 움직였다 = 모델 공간에서는 이전 위치들이 −m 움직인 것 → 관성
            for (Joint& joint : joints_) {
                joint.currentTail = joint.currentTail - stepMove;
                joint.prevTail = joint.prevTail - stepMove;
            }
        }
        step(skeleton, rotations, stepDt);
    }
}

void SpringBoneSimulator::step(const Skeleton& skeleton, std::vector<core::Quat>& rotations,
                               float dt) {
    skeleton.computePose(rotations, accumulated_, posed_);

    colliderCenters_.clear();
    for (const model::SpringCollider& c : model_.springColliders) {
        const auto bone = static_cast<std::size_t>(std::max(c.bone, 0));
        colliderCenters_.push_back(
            bone < posed_.size() ? posed_[bone] + accumulated_[bone].rotate(c.offset) : c.offset);
    }

    for (Joint& joint : joints_) {
        const auto j = static_cast<std::size_t>(joint.bone);
        const model::SpringGroup& group =
            model_.springGroups[static_cast<std::size_t>(joint.group)];

        // 부모는 먼저 계산되어 accumulated_/posed_가 이번 결과로 갱신되어 있음
        core::Quat parentRotation{};
        core::Vec3 head = posed_[j];
        if (joint.parent >= 0) {
            const auto p = static_cast<std::size_t>(joint.parent);
            parentRotation = accumulated_[p];
            head = posed_[p] + parentRotation.rotate(skeleton.bindPosition(joint.bone) -
                                                     skeleton.bindPosition(joint.parent));
        }
        const core::Quat rest = rotations[j] * parentRotation;
        const core::Vec3 restDir = rest.rotate(joint.axis) * (1.0f / joint.length);

        if (!initialized_) {
            joint.currentTail = joint.prevTail = head + restDir * joint.length;
        }

        core::Vec3 gravity = group.gravityDir;
        const float gravityLength = length(gravity);
        gravity =
            gravityLength > 1e-6f ? gravity * (group.gravityPower / gravityLength) : core::Vec3{};

        core::Vec3 next = joint.currentTail +
                          (joint.currentTail - joint.prevTail) * (1.0f - group.dragForce) +
                          restDir * (group.stiffness * dt) + gravity * dt;
        const auto constrain = [&](core::Vec3 p) {  // 본 길이 유지
            const core::Vec3 d = p - head;
            const float len = length(d);
            return len > 1e-6f ? head + d * (joint.length / len) : head + restDir * joint.length;
        };
        next = constrain(next);

        for (const int index : group.colliders) {
            if (index < 0 || static_cast<std::size_t>(index) >= colliderCenters_.size()) {
                continue;
            }
            const core::Vec3 center = colliderCenters_[static_cast<std::size_t>(index)];
            const float radius =
                model_.springColliders[static_cast<std::size_t>(index)].radius + group.hitRadius;
            const core::Vec3 d = next - center;
            const float dist = length(d);
            if (dist < radius && dist > 1e-6f) {
                next = constrain(center + d * (radius / dist));  // 구 표면으로 밀어낸 뒤 길이 유지
            }
        }

        if (length(next - joint.currentTail) < kSettleMeters) {
            next = joint.currentTail;
        }
        joint.prevTail = joint.currentTail;
        joint.currentTail = next;

        // 원래 방향 → 끝을 향하는 방향으로 돌리는 델타를 이 본의 자세 위에 덧붙임
        const core::Vec3 dir = (next - head) * (1.0f / joint.length);
        rotations[j] = core::Quat::fromTo(restDir, dir) * rotations[j];
        accumulated_[j] = rotations[j] * parentRotation;
        posed_[j] = head;
    }
    initialized_ = true;
}

}  // namespace deskpet::anim
