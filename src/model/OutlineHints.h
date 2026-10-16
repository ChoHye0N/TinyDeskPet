#pragma once

// "모든 재질" 외곽선에서 뺄 primitive를 고름 (ADR-0017, model.md §4.x).
// 반전 헐 외곽선은 메시마다 부풀린 껍데기를 그리므로, 얼굴 위에 붙인 얇은 판(눈·입·눈썹)이나 평평한
// 장식(헤일로)에 그리면 껍데기가 얼굴을 덮거나 장식 전체를 칠함. 외곽선 정보가 없는 모델(게임에서
// 내보낸 glTF 등)에서 "모든 재질"을 쓸 때만 의미가 있고, "모델 지정"은 모델이 정한 대로 둠

#include "model/Model.h"

#include <string_view>

namespace deskpet::model {

// 모든 primitive의 outlineSkip을 정함. 판단 순서: 눈 본 → 재질 이름 → 납작함
//   EyeBone: 정점의 절반 넘게 가장 큰 가중치가 눈 본(LeftEye·RightEye)이나 그 자손
//   Name: 재질 이름에 eye·iris·pupil·highlight·brow·lash·mouth·teeth·tongue·halo, 目·眉·口·歯·舌·
//         瞳·睫 등이 있음 (대소문자 무관)
//   Flat: 기본 자세에서 가장 얇은 축이 가장 긴 축의 5%보다 얇음
void markOutlineSkips(Model& model);

[[nodiscard]] std::string_view toString(OutlineSkip skip);

}  // namespace deskpet::model
