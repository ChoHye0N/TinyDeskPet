#pragma once

// 애니메이션 테스트용 최소 휴머노이드 (정점 없이 본만, 단위 m, 정면 +Z)

#include "model/Model.h"

#include <cmath>
#include <utility>

namespace deskpet::test {

// armDropDegrees: 0이면 T포즈(팔 수평), 35 정도면 MMD식 A포즈
inline model::Model makeSkeletonModel(float armDropDegrees = 0.0f) {
    using model::HumanBone;
    model::Model m;
    const float drop = armDropDegrees * 3.14159265f / 180.0f;
    const auto arm = [&](float side, float length) {
        return core::Vec3{side * (0.2f + length * std::cos(drop)), 1.4f - length * std::sin(drop),
                          0.0f};
    };
    const auto add = [&](const char* name, int parent, core::Vec3 position, HumanBone human) {
        model::Bone b;
        b.name = name;
        b.parent = parent;
        b.position = position;
        b.human = human;
        m.bones.push_back(std::move(b));
    };
    // 일부러 자식이 부모보다 앞에 오도록 섞음 (계층 순서 정렬 검증)
    add("lowerArm_L", 2, arm(1.0f, 0.3f), HumanBone::LeftLowerArm);       // 0
    add("hips", -1, {0.0f, 1.0f, 0.0f}, HumanBone::Hips);                 // 1
    add("upperArm_L", 1, {0.2f, 1.4f, 0.0f}, HumanBone::LeftUpperArm);    // 2
    add("hand_L", 0, arm(1.0f, 0.55f), HumanBone::LeftHand);              // 3
    add("upperArm_R", 1, {-0.2f, 1.4f, 0.0f}, HumanBone::RightUpperArm);  // 4
    add("lowerArm_R", 4, arm(-1.0f, 0.3f), HumanBone::RightLowerArm);     // 5
    add("upperLeg_L", 1, {0.1f, 0.9f, 0.0f}, HumanBone::LeftUpperLeg);    // 6
    add("lowerLeg_L", 6, {0.1f, 0.5f, 0.0f}, HumanBone::LeftLowerLeg);    // 7
    add("foot_L", 7, {0.1f, 0.08f, 0.0f}, HumanBone::LeftFoot);           // 8
    add("upperLeg_R", 1, {-0.1f, 0.9f, 0.0f}, HumanBone::RightUpperLeg);  // 9
    add("lowerLeg_R", 9, {-0.1f, 0.5f, 0.0f}, HumanBone::RightLowerLeg);  // 10
    add("foot_R", 10, {-0.1f, 0.08f, 0.0f}, HumanBone::RightFoot);        // 11
    add("head", 1, {0.0f, 1.55f, 0.0f}, HumanBone::Head);                 // 12

    // 손끝 정점 하나 (손 본에 고정) — 자세 경계 상자 검증용
    model::Vertex tip;
    tip.position = arm(1.0f, 0.7f);
    tip.joints[0] = 3;
    tip.weights[0] = 1.0f;
    m.vertices.push_back(tip);
    model::Vertex foot;
    foot.position = {0.1f, 0.0f, 0.0f};
    foot.joints[0] = 8;
    foot.weights[0] = 1.0f;
    m.vertices.push_back(foot);
    m.bounds = model::computeBounds(m.vertices);
    return m;
}

}  // namespace deskpet::test
