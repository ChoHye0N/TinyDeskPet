# core: 플랫폼 독립 기반 모듈 (docs/03-detailed-design/core.md)
add_library(deskpet_core STATIC
  Clock.cpp
  Config.cpp
  FixedTimestep.cpp
  Log.cpp
  LogFile.cpp)
add_library(deskpet::core ALIAS deskpet_core)

target_include_directories(deskpet_core PUBLIC
  "${PROJECT_SOURCE_DIR}/src"
  "${DESKPET_GENERATED_DIR}")   # deskpet/Version.h
deskpet_apply_compile_options(deskpet_core)
