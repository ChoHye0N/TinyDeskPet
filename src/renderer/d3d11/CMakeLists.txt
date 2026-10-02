# Direct3D 11 + DirectComposition 렌더러 (Windows 전용)
add_library(deskpet_renderer_d3d11 STATIC
  D3D11Renderer.cpp
  DxCheck.cpp
  MeshPass.cpp
  PlaceholderPass.cpp
  WicTexture.cpp)
add_library(deskpet::renderer_d3d11 ALIAS deskpet_renderer_d3d11)

target_link_libraries(deskpet_renderer_d3d11
  PUBLIC deskpet::renderer_api
  PRIVATE d3d11 dxgi d2d1 dcomp dxguid windowscodecs)
deskpet_apply_compile_options(deskpet_renderer_d3d11)

# -----------------------------------------------------------------------------
# HLSL → 바이트코드 헤더 (fxc /Fh). 실행 파일에 내장되므로 셰이더 파일을 배포할 필요가 없습니다.
# fxc는 Windows SDK에 있으며, 개발자 명령 프롬프트(vcvars)에서는 PATH에 들어 있습니다.
# -----------------------------------------------------------------------------
find_program(DESKPET_FXC fxc
  HINTS "$ENV{WindowsSdkVerBinPath}x64"
        "${CMAKE_WINDOWS_KITS_10_DIR}/bin/${CMAKE_SYSTEM_VERSION}/x64"
  REQUIRED)

set(DESKPET_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

# deskpet_compile_shader(<hlsl> <entry> <profile> <variable>)
#   → ${DESKPET_SHADER_DIR}/<variable>.h 에 `const BYTE <variable>[]` 생성
function(deskpet_compile_shader hlsl entry profile variable)
  set(output "${DESKPET_SHADER_DIR}/${variable}.h")
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${DESKPET_SHADER_DIR}"
    COMMAND "${DESKPET_FXC}" /nologo /O3 /WX /E ${entry} /T ${profile} /Vn ${variable}
            /Fh "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/${hlsl}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${hlsl}"
    COMMENT "HLSL ${hlsl}:${entry} (${profile})"
    VERBATIM)
  target_sources(deskpet_renderer_d3d11 PRIVATE "${output}")
endfunction()

deskpet_compile_shader(shaders/Mesh.hlsl VSMain vs_5_0 g_MeshVS)
deskpet_compile_shader(shaders/Mesh.hlsl PSMain ps_5_0 g_MeshPS)
deskpet_compile_shader(shaders/Mesh.hlsl PSOutline ps_5_0 g_MeshOutlinePS)
target_include_directories(deskpet_renderer_d3d11 PRIVATE "${DESKPET_SHADER_DIR}")

# 생성된 헤더를 포함하는 소스는 의존성을 명시해야 합니다. Ninja Multi-Config에서 컴파일러가 보고하는
# 포함 파일 목록(/showIncludes)만으로는 생성 헤더가 바뀐 것을 놓쳐, 셰이더를 고쳐도 옛 바이트코드가
# 실행 파일에 남는 일이 있었습니다.
set_source_files_properties(MeshPass.cpp PROPERTIES OBJECT_DEPENDS
  "${DESKPET_SHADER_DIR}/g_MeshVS.h;${DESKPET_SHADER_DIR}/g_MeshPS.h;${DESKPET_SHADER_DIR}/g_MeshOutlinePS.h")
