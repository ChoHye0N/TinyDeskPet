# anim: 휴머노이드 뼈대 계산과 코드로 만드는 동작 (플랫폼 독립, docs/03-detailed-design/anim.md)
add_library(deskpet_anim STATIC
  MotionPlayer.cpp
  ProceduralAnimator.cpp
  Skeleton.cpp
  SpringBone.cpp)
add_library(deskpet::anim ALIAS deskpet_anim)

target_link_libraries(deskpet_anim PUBLIC deskpet::model)
deskpet_apply_compile_options(deskpet_anim)
