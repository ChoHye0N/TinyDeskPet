# =============================================================================
# CompilerOptions.cmake
# 우리 코드 타깃에만 적용하는 공통 컴파일 옵션 (서드파티에는 적용하지 않음)
# =============================================================================

# deskpet_apply_compile_options(<target>)
#   - 경고 수준, 표준 준수 모드, UTF-8 소스, Windows 매크로를 설정합니다.
function(deskpet_apply_compile_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4            # 높은 경고 수준
      /permissive-   # 표준 준수 모드
      /utf-8         # 소스·실행 문자 집합 UTF-8 (한국어 주석/문자열)
      /Zc:__cplusplus # __cplusplus 매크로를 실제 표준 값으로
    )
    if(DESKPET_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic
      -Wshadow -Wconversion -Wnon-virtual-dtor -Wold-style-cast
      -Wcast-align -Woverloaded-virtual -Wnull-dereference)
    if(DESKPET_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  if(WIN32)
    target_compile_definitions(${target} PRIVATE
      WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
      _WIN32_WINNT=0x0A00 WINVER=0x0A00)   # Windows 10 API
  endif()
endfunction()
