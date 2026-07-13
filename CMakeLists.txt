cmake_minimum_required(VERSION 3.16)

project(
  NixBench
  VERSION 0.1.0
  DESCRIPTION "A NetBSD-first desktop environment built with SDL3"
  LANGUAGES C
)

include(CTest)
include(GNUInstallDirs)

find_package(SDL3 3.2.0 CONFIG REQUIRED COMPONENTS SDL3)

function(nixbench_configure_c_target target)
  set_target_properties(
    "${target}"
    PROPERTIES
      C_STANDARD 11
      C_STANDARD_REQUIRED YES
      C_EXTENSIONS NO
  )

  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options("${target}" PRIVATE -Wall -Wextra -Wpedantic)
  elseif(MSVC)
    target_compile_options("${target}" PRIVATE /W4)
  endif()
endfunction()

add_library(
  nixbench_model STATIC
  src/application.c
  src/desktop.c
  src/menu.c
  src/nixinfo.c
  src/nixinfo_system.c
  src/shell.c
  src/window.c
)
target_include_directories(
  nixbench_model
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
nixbench_configure_c_target(nixbench_model)

add_executable(
  nixbench
  src/desktop_renderer.c
  src/main.c
  src/menu_renderer.c
  src/nixinfo_renderer.c
  src/shell_renderer.c
  src/window_renderer.c
)
nixbench_configure_c_target(nixbench)

target_compile_definitions(
  nixbench
  PRIVATE NIXBENCH_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(nixbench PRIVATE SDL3::SDL3)
target_link_libraries(nixbench PRIVATE nixbench_model)

install(TARGETS nixbench RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
  add_executable(nixbench_desktop_tests tests/desktop_test.c)
  target_link_libraries(nixbench_desktop_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_desktop_tests)

  add_executable(nixbench_window_tests tests/window_test.c)
  target_link_libraries(nixbench_window_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_window_tests)

  add_executable(nixbench_menu_tests tests/menu_test.c)
  target_link_libraries(nixbench_menu_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_menu_tests)

  add_executable(nixbench_shell_tests tests/shell_test.c)
  target_link_libraries(nixbench_shell_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_shell_tests)

  add_executable(nixbench_application_tests tests/application_test.c)
  target_link_libraries(nixbench_application_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_application_tests)

  add_executable(nixbench_nixinfo_tests tests/nixinfo_test.c)
  target_link_libraries(nixbench_nixinfo_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_nixinfo_tests)

  add_executable(
    nixbench_nixinfo_system_tests
    tests/nixinfo_system_test.c
  )
  target_link_libraries(
    nixbench_nixinfo_system_tests
    PRIVATE nixbench_model
  )
  nixbench_configure_c_target(nixbench_nixinfo_system_tests)

  add_executable(
    nixbench_renderer_tests
    tests/renderer_test.c
    src/desktop_renderer.c
    src/window_renderer.c
  )
  target_link_libraries(
    nixbench_renderer_tests
    PRIVATE SDL3::SDL3 nixbench_model
  )
  nixbench_configure_c_target(nixbench_renderer_tests)

  add_test(NAME nixbench.desktop.model COMMAND nixbench_desktop_tests)
  add_test(NAME nixbench.window.model COMMAND nixbench_window_tests)
  add_test(NAME nixbench.menu.model COMMAND nixbench_menu_tests)
  add_test(NAME nixbench.shell.model COMMAND nixbench_shell_tests)
  add_test(NAME nixbench.application.model COMMAND nixbench_application_tests)
  add_test(NAME nixbench.nixinfo.model COMMAND nixbench_nixinfo_tests)
  add_test(
    NAME nixbench.nixinfo.system
    COMMAND nixbench_nixinfo_system_tests
  )
  add_test(NAME nixbench.renderer.callback COMMAND nixbench_renderer_tests)
  add_test(NAME nixbench.cli.version COMMAND nixbench --version)
  add_test(NAME nixbench.cli.help COMMAND nixbench --help)
  add_test(NAME nixbench.cli.unknown-option COMMAND nixbench --unknown-option)
  set_tests_properties(nixbench.cli.unknown-option PROPERTIES WILL_FAIL YES)
  add_test(NAME nixbench.sdl.smoke COMMAND nixbench --exit-after-first-frame)
  set_tests_properties(
    nixbench.sdl.smoke
    PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
  )
endif()
