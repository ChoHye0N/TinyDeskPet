# Win32 창 구현 (Windows 전용)
add_library(deskpet_platform_win32 STATIC
  Win32Strings.cpp
  Win32Window.cpp)
add_library(deskpet::platform_win32 ALIAS deskpet_platform_win32)

target_link_libraries(deskpet_platform_win32
  PUBLIC deskpet::platform_api
  PRIVATE user32 gdi32 shell32)
deskpet_apply_compile_options(deskpet_platform_win32)
