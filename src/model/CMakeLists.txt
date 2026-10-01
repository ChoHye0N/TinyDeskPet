# model: VRM/glTF, PMX, FBX 로딩 (플랫폼 독립, docs/03-detailed-design/model.md, ADR-0009)
include(FetchContent)

# 서드파티 버전 고정 ---------------------------------------------------------------
# cgltf: 단일 헤더 glTF 파서 (MIT, ADR-0005)
FetchContent_Declare(cgltf
  GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE)
# ufbx: 단일 파일 FBX 로더 (MIT 또는 퍼블릭 도메인, ADR-0009)
FetchContent_Declare(ufbx
  GIT_REPOSITORY https://github.com/ufbx/ufbx.git
  GIT_TAG        v0.23.1
  GIT_SHALLOW    TRUE)
# stb_image: WIC가 못 읽는 TGA 디코딩 (MIT 또는 퍼블릭 도메인). 태그가 없어 커밋으로 고정
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG        2c980bb59875b0d32144a71867fbdebb2f77cd20)
FetchContent_MakeAvailable(cgltf ufbx stb)

# 서드파티 구현부는 우리 경고 옵션을 적용하지 않는 별도 타깃으로 둡니다.
add_library(deskpet_model_thirdparty STATIC
  ThirdPartyImpl.cpp
  "${ufbx_SOURCE_DIR}/ufbx.c")
target_include_directories(deskpet_model_thirdparty SYSTEM PUBLIC
  "${cgltf_SOURCE_DIR}"
  "${ufbx_SOURCE_DIR}"
  "${stb_SOURCE_DIR}")
if(MSVC)
  target_compile_definitions(deskpet_model_thirdparty PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen 경고
  target_compile_options(deskpet_model_thirdparty PRIVATE /utf-8)
endif()

add_library(deskpet_model STATIC
  FbxImporter.cpp
  GltfImporter.cpp
  Humanoid.cpp
  ModelLoader.cpp
  PmxImporter.cpp
  TextureSource.cpp)
add_library(deskpet::model ALIAS deskpet_model)

target_link_libraries(deskpet_model
  PUBLIC deskpet::core
  PRIVATE deskpet_model_thirdparty)
deskpet_apply_compile_options(deskpet_model)
