# ui: 설정 창 (Dear ImGui, 플랫폼 독립, docs/03-detailed-design/ui.md, ADR-0016)
include(FetchContent)

# Dear ImGui: 즉시 모드 GUI (MIT). 1.92부터 글꼴 글리프를 필요할 때 만들어 한글 범위를 미리 굽지 않음
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9b
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# ImGui 본체는 우리 경고 옵션을 적용하지 않는 별도 타깃 (모든 사용처가 같은 설정 매크로를 보도록 PUBLIC)
add_library(deskpet_imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(deskpet_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}")
target_compile_definitions(deskpet_imgui PUBLIC
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
  # 오버레이 창은 키보드 포커스를 받지 않으므로 클립보드·IME 기본 구현(Win32 API)을 빼서
  # imgui.cpp가 user32·imm32에 의존하지 않게 함
  IMGUI_DISABLE_WIN32_DEFAULT_CLIPBOARD_FUNCTIONS
  IMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS)
if(MSVC)
  target_compile_options(deskpet_imgui PRIVATE /utf-8)
endif()

# Direct3D 11 백엔드 (Windows 전용, 렌더러가 사용). 런타임 셰이더 컴파일에 d3dcompiler 필요
if(WIN32)
  add_library(deskpet_imgui_dx11 STATIC "${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp")
  target_include_directories(deskpet_imgui_dx11 SYSTEM PUBLIC "${imgui_SOURCE_DIR}/backends")
  target_link_libraries(deskpet_imgui_dx11 PUBLIC deskpet_imgui PRIVATE d3d11 d3dcompiler dxgi)
endif()

add_library(deskpet_ui STATIC
  Icons.cpp
  SettingsUi.cpp
  Theme.cpp)
add_library(deskpet::ui ALIAS deskpet_ui)

target_link_libraries(deskpet_ui
  PUBLIC deskpet::core
  PRIVATE deskpet_imgui)
deskpet_apply_compile_options(deskpet_ui)
