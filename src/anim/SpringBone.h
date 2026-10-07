#pragma once

// 머리카락·옷 흔들림 (VRM SpringBone). 명세: docs/03-detailed-design/anim.md §4.4
//
// 본마다 "끝(tail)" 한 점을 Verlet 적분으로 움직이고, 본이 그 점을 향하도록 회전합니다.
//   다음 끝 = 현재 끝 + (현재 끝 − 이전 끝)·(1 − drag)       관성 (흔들림 유지·감쇠)
//          + 원래 방향 · stiffness · dt                     복원
//          + 중력 방향 · gravityPower · dt                   중력
//   → 본 길이로 되돌리고, 충돌체(구) 밖으로 밀어냄
// 계산 방식은 UniVRM VRMSpringBone과 같습니다. 모델 공간에서 계산하고, 캐릭터가 화면에서
// 움직인 만큼(movement) 이전 위치들을 반대로 옮겨 관성이 생기게 합니다.

#include "anim/Skeleton.h"
#include "model/Model.h"

#include <vector>

namespace deskpet::anim {

class SpringBoneSimulator {
public:
    // 흔들리는 본이 원래 방향에서 벗어날 수 있는 최대 각도. 빠르게 떨어지다 멈추면 관성으로
    // 머리카락이 몸보다 높이 튀어 오르는 등 과장되고, 창 밖으로 크게 나가는 것을 막음
    static constexpr float kMaxSwingDegrees = 40.0f;
    // 화면 이동에서 오는 관성의 상한 (한 단계 = 1/60초에 m). 던지기·낙하 같은 빠른 이동도
    // 이 속도(초속 1.8m)로 움직인 것처럼 반응
    static constexpr float kMaxInertiaMetersPerStep = 0.03f;

    explicit SpringBoneSimulator(const model::Model& model);

    [[nodiscard]] bool empty() const noexcept { return joints_.empty(); }

    // rotations: 자세 델타 (입력). 흔들리는 본의 델타를 덧붙여 돌려줌.
    // movement: 이번 프레임에 캐릭터가 화면에서 움직인 거리 (모델 공간 m)
    // rootOffset: 몸 전체를 옮긴 거리 (Skeleton::computePose와 같음). 바뀌면 그만큼 관성이 생김
    void update(const Skeleton& skeleton, std::vector<core::Quat>& rotations, core::Vec3 movement,
                float dt, core::Vec3 rootOffset = {});

    // 다음 update에서 현재 자세 그대로 다시 시작 (순간 이동, 모델 교체 등)
    void reset() noexcept { initialized_ = false; }

private:
    struct Joint {
        int bone = -1;
        int parent = -1;
        core::Vec3 axis;  // 바인드 포즈에서 본 → 끝 (모델 공간, 길이 포함)
        float length = 0.0f;
        int group = 0;
        core::Vec3 currentTail;
        core::Vec3 prevTail;
    };

    void addChain(int root, int group, const std::vector<std::vector<int>>& children,
                  std::vector<bool>& used);
    void step(const Skeleton& skeleton, std::vector<core::Quat>& rotations, float dt,
              core::Vec3 rootOffset);

    const model::Model& model_;
    std::vector<Joint> joints_;  // 부모가 먼저 오는 순서
    bool initialized_ = false;

    // 중간 결과 (할당 재사용)
    std::vector<core::Quat> accumulated_;
    std::vector<core::Quat> bodyAccumulated_;
    std::vector<core::Vec3> posed_;
    std::vector<core::Vec3> colliderCenters_;
};

}  // namespace deskpet::anim
