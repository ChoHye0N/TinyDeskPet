# =============================================================================
# DeskPet — 루트 CMake 스크립트
# 구조 설명: docs/02-architecture/SAD.md §10, docs/05-devops/ci-cd-implementation.md
# =============================================================================
cmake_minimum_required(VERSION 3.25)

# 버전의 단일 원천(single source of truth)은 저장소 루트의 VERSION 파일입니다.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" DESKPET_VERSION LIMIT_COUNT 1)
string(STRIP "${DESKPET_VERSION}" DESKPET_VERSION)

project(DeskPet
  VERSION ${DESKPET_VERSION}
  DESCRIPTION "Lightweight desktop companion built with C++20, Direct3D 11 and DirectComposition"
  LANGUAGES C CXX)  # C: ufbx.c (ADR-0009)

# -----------------------------------------------------------------------------
# 옵션
# -----------------------------------------------------------------------------
option(DESKPET_BUILD_TESTS "단위 테스트를 빌드합니다" ON)
option(DESKPET_WARNINGS_AS_ERRORS "컴파일러 경고를 오류로 취급합니다 (CI에서 ON)" OFF)
option(DESKPET_ENABLE_SANITIZERS "AddressSanitizer/UBSan을 켭니다 (GCC/Clang 전용)" OFF)

# -----------------------------------------------------------------------------
# 전역 설정
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC 런타임을 정적 링크(/MT, /MTd)하여 재배포 패키지 설치 없이 실행되게 합니다.
# 하위 프로젝트(GoogleTest 등)도 같은 런타임을 쓰도록 정책 기본값을 지정합니다.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Release에도 디버그 정보(PDB)를 만듭니다. 최적화는 그대로이고 실행 파일 크기도 거의 같으며,
# 사용자 PC에서 난 크래시 주소를 함수 이름으로 바꿀 때 필요합니다 (PDB는 배포하지 않고 보관).
# /OPT:REF·ICF: /DEBUG를 켜면 기본값이 꺼지므로 다시 켜서 쓰지 않는 코드를 제거
if(MSVC)
  add_compile_options($<$<CONFIG:Release>:/Zi>)
  add_link_options($<$<CONFIG:Release>:/DEBUG> $<$<CONFIG:Release>:/OPT:REF> $<$<CONFIG:Release>:/OPT:ICF>)
endif()

# 실행 파일을 build/<preset>/bin[/<Config>] 에 모읍니다.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerOptions)
include(Sanitizers)
include(GitVersion)

# -----------------------------------------------------------------------------
# 소스와 테스트
# -----------------------------------------------------------------------------
add_subdirectory(src)

if(DESKPET_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Packaging)

message(STATUS "DeskPet ${PROJECT_VERSION} (${DESKPET_GIT_HASH}) | tests=${DESKPET_BUILD_TESTS} "
               "werror=${DESKPET_WARNINGS_AS_ERRORS} sanitizers=${DESKPET_ENABLE_SANITIZERS}")
