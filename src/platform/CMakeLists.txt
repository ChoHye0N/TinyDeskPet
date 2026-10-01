# platform: 창·입력 추상화 (docs/03-detailed-design/platform.md)

# 인터페이스만 담은 헤더 전용 타깃 (플랫폼 독립)
add_library(deskpet_platform_api INTERFACE)
add_library(deskpet::platform_api ALIAS deskpet_platform_api)
target_link_libraries(deskpet_platform_api INTERFACE deskpet::core)

# Win32 구현
if(WIN32)
  add_subdirectory(win32)
endif()
