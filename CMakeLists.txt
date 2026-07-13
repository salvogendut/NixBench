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

add_executable(nixbench src/main.c)

set_target_properties(
  nixbench
  PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)

target_compile_definitions(
  nixbench
  PRIVATE NIXBENCH_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(nixbench PRIVATE SDL3::SDL3)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(nixbench PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
  target_compile_options(nixbench PRIVATE /W4)
endif()

install(TARGETS nixbench RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

if(BUILD_TESTING)
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
