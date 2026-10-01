# =============================================================================
# Sanitizers.cmake
# DESKPET_ENABLE_SANITIZERS=ON 이면 AddressSanitizer + UndefinedBehaviorSanitizer를
# 모든 타깃(GoogleTest 포함)에 적용합니다. 메모리 오류와 정의되지 않은 동작을
# 테스트 실행 중에 잡아냅니다. (Linux CI의 ci-linux 프리셋에서 사용)
# =============================================================================
if(DESKPET_ENABLE_SANITIZERS)
  if(MSVC)
    message(WARNING "DESKPET_ENABLE_SANITIZERS는 GCC/Clang에서만 지원합니다. 무시합니다.")
  else()
    set(_deskpet_sanitizer_flags -fsanitize=address,undefined -fno-omit-frame-pointer
                                 -fno-sanitize-recover=all)
    add_compile_options(${_deskpet_sanitizer_flags})
    add_link_options(${_deskpet_sanitizer_flags})
    message(STATUS "Sanitizers: address, undefined")
  endif()
endif()
