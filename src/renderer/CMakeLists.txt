# renderer: 렌더링 추상화 (docs/03-detailed-design/renderer.md)

add_library(deskpet_renderer_api INTERFACE)
add_library(deskpet::renderer_api ALIAS deskpet_renderer_api)
target_link_libraries(deskpet_renderer_api INTERFACE deskpet::core deskpet::model)

if(WIN32)
  add_subdirectory(d3d11)
endif()
