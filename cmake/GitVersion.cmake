# =============================================================================
# GitVersion.cmake
# 구성(configure) 시점의 Git 커밋 해시를 읽어 Version.h를 생성합니다.
#   - 결과: ${PROJECT_BINARY_DIR}/generated/deskpet/Version.h
#   - 주의: 커밋 후 다시 구성하지 않으면 이전 해시가 남습니다. CI는 매번 새로 구성합니다.
# =============================================================================
set(DESKPET_GIT_HASH "unknown")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=7 HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE _hash
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _hash_result)
  if(_hash_result EQUAL 0 AND _hash)
    set(DESKPET_GIT_HASH "${_hash}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
      WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
      OUTPUT_VARIABLE _dirty
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(_dirty)
      string(APPEND DESKPET_GIT_HASH "-dirty")
    endif()
  endif()
endif()

set(DESKPET_GENERATED_DIR "${PROJECT_BINARY_DIR}/generated")
configure_file(
  "${PROJECT_SOURCE_DIR}/cmake/Version.h.in"
  "${DESKPET_GENERATED_DIR}/deskpet/Version.h"
  @ONLY)
