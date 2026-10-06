# 단위 테스트 (GoogleTest). 플랫폼 독립 모듈만 대상으로 하므로 Linux/Windows 모두에서 실행됩니다.
include(FetchContent)

# 버전 고정: v1.17.0 (commit 52eb8108c5bdec04579160ae17225d66034bd723)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.17.0
  GIT_SHALLOW    TRUE)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)  # 정적 MSVC 런타임(/MT)과 일치
FetchContent_MakeAvailable(googletest)

add_executable(deskpet_tests
  core/ConfigTests.cpp
  core/FixedTimestepTests.cpp
  core/LogFileTests.cpp
  core/LogTests.cpp
  core/Math3DTests.cpp
  core/MathTests.cpp
  anim/AnimTestModels.h
  anim/ProceduralAnimatorTests.cpp
  anim/SkeletonTests.cpp
  anim/SpringBoneTests.cpp
  model/FbxImporterTests.cpp
  model/GltfBuilder.h
  model/HumanoidTests.cpp
  model/ModelFormatTests.cpp
  model/ModelLoaderTests.cpp
  model/PmxBuilder.h
  model/PmxImporterTests.cpp
  model/TexturePaddingTests.cpp
  model/TextureSourceTests.cpp
  character/CharacterControllerTests.cpp
  renderer/SampleCountTests.cpp
  app/ApplicationTests.cpp
  app/CameraFitTests.cpp
  app/Fakes.h)

target_link_libraries(deskpet_tests PRIVATE deskpet::app GTest::gtest_main)
target_include_directories(deskpet_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
deskpet_apply_compile_options(deskpet_tests)

include(GoogleTest)
gtest_discover_tests(deskpet_tests DISCOVERY_MODE PRE_TEST)
