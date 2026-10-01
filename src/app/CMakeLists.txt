# app: 메인 루프 (docs/03-detailed-design/app.md)

# 플랫폼 독립 애플리케이션 로직 → Linux에서도 테스트됨
add_library(deskpet_app STATIC
  Application.cpp
  CameraFit.cpp)
add_library(deskpet::app ALIAS deskpet_app)

target_link_libraries(deskpet_app PUBLIC
  deskpet::character
  deskpet::model
  deskpet::anim
  deskpet::platform_api
  deskpet::renderer_api)
deskpet_apply_compile_options(deskpet_app)

# Windows 실행 파일
if(WIN32)
  configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/DeskPet.rc.in"
    "${DESKPET_GENERATED_DIR}/DeskPet.rc"
    @ONLY)

  add_executable(DeskPet WIN32
    main_win32.cpp
    win32/DeskPet.manifest                # DPI 인식, UTF-8 코드 페이지
    "${DESKPET_GENERATED_DIR}/DeskPet.rc") # 버전 정보 리소스

  target_link_libraries(DeskPet PRIVATE
    deskpet::app
    deskpet::platform_win32
    deskpet::renderer_d3d11)
  deskpet_apply_compile_options(DeskPet)

  # Visual Studio 디버거의 작업 디렉터리를 소스의 config 폴더로 (deskpet.ini를 찾을 수 있게)
  set_target_properties(DeskPet PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/config")

  # 기본 설정 파일과 모델 폴더(assets/models, 커밋되지 않음)를 실행 파일 옆에 복사 (ADR-0008).
  # POST_BUILD는 재링크될 때만 실행되므로, ini나 모델만 바뀐 경우에도 복사되도록 매 빌드 실행되는 타깃으로 둠
  add_custom_target(deskpet_copy_runtime_files ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PROJECT_SOURCE_DIR}/config/deskpet.ini"
            "$<TARGET_FILE_DIR:DeskPet>/deskpet.ini"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${PROJECT_SOURCE_DIR}/assets/models"
            "$<TARGET_FILE_DIR:DeskPet>/models"
    COMMENT "deskpet.ini, assets/models 복사")
  add_dependencies(DeskPet deskpet_copy_runtime_files)
endif()
