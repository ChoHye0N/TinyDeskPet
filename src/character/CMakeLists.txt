# character: 캐릭터 도메인 로직 (docs/03-detailed-design/character.md)
add_library(deskpet_character STATIC
  CharacterController.cpp
  CharacterState.cpp)
add_library(deskpet::character ALIAS deskpet_character)

target_link_libraries(deskpet_character PUBLIC deskpet::core)
deskpet_apply_compile_options(deskpet_character)
