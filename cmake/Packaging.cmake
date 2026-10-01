# =============================================================================
# Packaging.cmake
# `cpack` 또는 `cmake --workflow --preset ci-windows-release` 의 package 단계에서
# 배포용 zip을 만듭니다. 구조는 docs/02-architecture/SAD.md §11 참고.
# =============================================================================
if(TARGET DeskPet)
  install(TARGETS DeskPet RUNTIME DESTINATION bin)
endif()
install(FILES "${PROJECT_SOURCE_DIR}/config/deskpet.ini" DESTINATION bin)
install(FILES
  "${PROJECT_SOURCE_DIR}/README.md"
  "${PROJECT_SOURCE_DIR}/CHANGELOG.md"
  "${PROJECT_SOURCE_DIR}/LICENSE"
  DESTINATION .)

if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(_deskpet_platform "win64")
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" _deskpet_platform)
endif()

set(CPACK_PACKAGE_NAME "DeskPet")
set(CPACK_PACKAGE_VENDOR "DeskPet Project")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "DeskPet-${PROJECT_VERSION}-${_deskpet_platform}")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_PACKAGE_DIRECTORY "${PROJECT_BINARY_DIR}/package")
include(CPack)
