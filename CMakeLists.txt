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

option(
  NIXBENCH_BUILD_EXAMPLES
  "Build experimental NixBench client applications"
  ON
)

option(
  NIXBENCH_BUILD_APPLICATIONS
  "Build NixBench native applications"
  ON
)

option(
  NIXBENCH_BUILD_WSDISPLAY_SMOKE
  "Build the supervised, console-taking wsdisplay smoke harness"
  OFF
)

option(
  NIXBENCH_BUILD_WSDISPLAY_SESSION
  "Build the privilege-separated NetBSD wsdisplay session"
  OFF
)

set(
  NIXBENCH_LIBDRM "AUTO"
  CACHE STRING "Build detailed DRM/KMS capability queries (AUTO, ON, or OFF)"
)
set_property(CACHE NIXBENCH_LIBDRM PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${NIXBENCH_LIBDRM}" NIXBENCH_LIBDRM)
set(
  NIXBENCH_LIBDRM "${NIXBENCH_LIBDRM}"
  CACHE STRING "Build detailed DRM/KMS capability queries (AUTO, ON, or OFF)"
  FORCE
)

if(
  NOT NIXBENCH_LIBDRM STREQUAL "AUTO"
  AND NOT NIXBENCH_LIBDRM STREQUAL "ON"
  AND NOT NIXBENCH_LIBDRM STREQUAL "OFF"
)
  message(
    FATAL_ERROR
    "NIXBENCH_LIBDRM must be one of AUTO, ON, or OFF "
    "(received '${NIXBENCH_LIBDRM}')"
  )
endif()

set(NIXBENCH_LIBDRM_ENABLED FALSE)
if(
  NOT NIXBENCH_LIBDRM STREQUAL "OFF"
  AND CMAKE_SYSTEM_NAME STREQUAL "NetBSD"
)
  find_path(
    NIXBENCH_LIBDRM_INCLUDE_DIR
    NAMES xf86drm.h
    PATHS /usr/X11R7
    PATH_SUFFIXES include
  )
  find_path(
    NIXBENCH_LIBDRM_MODE_INCLUDE_DIR
    NAMES xf86drmMode.h
    PATHS /usr/X11R7
    PATH_SUFFIXES include
  )
  find_path(
    NIXBENCH_LIBDRM_KERNEL_INCLUDE_DIR
    NAMES drm.h
    PATHS /usr/X11R7
    PATH_SUFFIXES include/libdrm libdrm
  )
  find_library(
    NIXBENCH_LIBDRM_LIBRARY
    NAMES drm
    PATHS /usr/X11R7
    PATH_SUFFIXES lib
  )

  set(_nixbench_libdrm_missing)
  if(NOT NIXBENCH_LIBDRM_INCLUDE_DIR)
    list(APPEND _nixbench_libdrm_missing "xf86drm.h")
  endif()
  if(NOT NIXBENCH_LIBDRM_MODE_INCLUDE_DIR)
    list(APPEND _nixbench_libdrm_missing "xf86drmMode.h")
  endif()
  if(NOT NIXBENCH_LIBDRM_KERNEL_INCLUDE_DIR)
    list(APPEND _nixbench_libdrm_missing "libdrm/drm.h")
  endif()
  if(NOT NIXBENCH_LIBDRM_LIBRARY)
    list(APPEND _nixbench_libdrm_missing "libdrm")
  endif()

  if(_nixbench_libdrm_missing)
    string(JOIN ", " _nixbench_libdrm_missing_text
           ${_nixbench_libdrm_missing})
    if(NIXBENCH_LIBDRM STREQUAL "ON")
      message(
        FATAL_ERROR
        "Detailed DRM probing was requested, but these components were not "
        "found: ${_nixbench_libdrm_missing_text}"
      )
    else()
      message(
        STATUS
        "Detailed DRM probing is unavailable: "
        "${_nixbench_libdrm_missing_text}"
      )
    endif()
  else()
    set(NIXBENCH_LIBDRM_ENABLED TRUE)
    add_library(nixbench_libdrm INTERFACE)
    add_library(NixBench::Libdrm ALIAS nixbench_libdrm)
    target_include_directories(
      nixbench_libdrm
      INTERFACE
        "${NIXBENCH_LIBDRM_INCLUDE_DIR}"
        "${NIXBENCH_LIBDRM_MODE_INCLUDE_DIR}"
        "${NIXBENCH_LIBDRM_KERNEL_INCLUDE_DIR}"
    )
    target_link_libraries(
      nixbench_libdrm
      INTERFACE "${NIXBENCH_LIBDRM_LIBRARY}"
    )
    message(STATUS "Detailed DRM probing enabled (${NIXBENCH_LIBDRM_LIBRARY})")
  endif()
elseif(NIXBENCH_LIBDRM STREQUAL "ON")
  message(
    FATAL_ERROR
    "Detailed DRM probing is currently supported only on NetBSD"
  )
elseif(NOT NIXBENCH_LIBDRM STREQUAL "OFF")
  message(
    STATUS
    "Detailed DRM probing is unavailable: currently supported only on NetBSD"
  )
endif()

set(
  NIXBENCH_WAYLAND "AUTO"
  CACHE STRING "Build the experimental Wayland server (AUTO, ON, or OFF)"
)
set_property(CACHE NIXBENCH_WAYLAND PROPERTY STRINGS AUTO ON OFF)
string(TOUPPER "${NIXBENCH_WAYLAND}" NIXBENCH_WAYLAND)
set(
  NIXBENCH_WAYLAND "${NIXBENCH_WAYLAND}"
  CACHE STRING "Build the experimental Wayland server (AUTO, ON, or OFF)"
  FORCE
)

if(
  NOT NIXBENCH_WAYLAND STREQUAL "AUTO"
  AND NOT NIXBENCH_WAYLAND STREQUAL "ON"
  AND NOT NIXBENCH_WAYLAND STREQUAL "OFF"
)
  message(
    FATAL_ERROR
    "NIXBENCH_WAYLAND must be one of AUTO, ON, or OFF "
    "(received '${NIXBENCH_WAYLAND}')"
  )
endif()

set(NIXBENCH_WAYLAND_ENABLED FALSE)

if(NOT NIXBENCH_WAYLAND STREQUAL "OFF")
  find_path(
    NIXBENCH_WAYLAND_SERVER_INCLUDE_DIR
    NAMES wayland-server-core.h
    PATH_SUFFIXES include
  )
  find_library(
    NIXBENCH_WAYLAND_SERVER_LIBRARY
    NAMES wayland-server
    PATH_SUFFIXES lib
  )
  find_path(
    NIXBENCH_XKBCOMMON_INCLUDE_DIR
    NAMES xkbcommon/xkbcommon.h
    PATH_SUFFIXES include
  )
  find_library(
    NIXBENCH_XKBCOMMON_LIBRARY
    NAMES xkbcommon
    PATH_SUFFIXES lib
  )
  find_program(NIXBENCH_WAYLAND_SCANNER NAMES wayland-scanner)
  find_file(
    NIXBENCH_XDG_SHELL_XML
    NAMES xdg-shell.xml
    PATH_SUFFIXES
      share/wayland-protocols/stable/xdg-shell
      wayland-protocols/stable/xdg-shell
  )

  set(_nixbench_wayland_missing)
  if(NOT NIXBENCH_WAYLAND_SERVER_INCLUDE_DIR)
    list(APPEND _nixbench_wayland_missing "wayland-server-core.h")
  endif()
  if(NOT NIXBENCH_WAYLAND_SERVER_LIBRARY)
    list(APPEND _nixbench_wayland_missing "libwayland-server")
  endif()
  if(NOT NIXBENCH_XKBCOMMON_INCLUDE_DIR)
    list(APPEND _nixbench_wayland_missing "xkbcommon/xkbcommon.h")
  endif()
  if(NOT NIXBENCH_XKBCOMMON_LIBRARY)
    list(APPEND _nixbench_wayland_missing "libxkbcommon")
  endif()
  if(NOT NIXBENCH_WAYLAND_SCANNER)
    list(APPEND _nixbench_wayland_missing "wayland-scanner")
  endif()
  if(NOT NIXBENCH_XDG_SHELL_XML)
    list(APPEND _nixbench_wayland_missing "stable xdg-shell.xml")
  endif()

  if(_nixbench_wayland_missing)
    string(JOIN ", " _nixbench_wayland_missing_text ${_nixbench_wayland_missing})
    if(NIXBENCH_WAYLAND STREQUAL "ON")
      message(
        FATAL_ERROR
        "Wayland support was requested, but these components were not found: "
        "${_nixbench_wayland_missing_text}"
      )
    else()
      message(
        STATUS
        "Wayland support is unavailable: ${_nixbench_wayland_missing_text}"
      )
    endif()
  else()
    set(NIXBENCH_WAYLAND_ENABLED TRUE)
    message(STATUS "Wayland support enabled (${NIXBENCH_XDG_SHELL_XML})")
  endif()
endif()

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

if(NIXBENCH_WAYLAND_ENABLED)
  set(
    NIXBENCH_WAYLAND_GENERATED_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
  )
  set(
    NIXBENCH_XDG_SHELL_SERVER_HEADER
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/xdg-shell-server-protocol.h"
  )
  set(
    NIXBENCH_XDG_SHELL_CLIENT_HEADER
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h"
  )
  set(
    NIXBENCH_XDG_SHELL_PRIVATE_CODE
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c"
  )
  set(
    NIXBENCH_APPLICATION_MENU_XML
    "${CMAKE_CURRENT_SOURCE_DIR}/protocols/nixbench-application-menu-v1.xml"
  )
  set(
    NIXBENCH_APPLICATION_MENU_SERVER_HEADER
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/nixbench-application-menu-v1-server-protocol.h"
  )
  set(
    NIXBENCH_APPLICATION_MENU_CLIENT_HEADER
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/nixbench-application-menu-v1-client-protocol.h"
  )
  set(
    NIXBENCH_APPLICATION_MENU_PRIVATE_CODE
    "${NIXBENCH_WAYLAND_GENERATED_DIR}/nixbench-application-menu-v1-protocol.c"
  )

  add_custom_command(
    OUTPUT
      "${NIXBENCH_XDG_SHELL_SERVER_HEADER}"
      "${NIXBENCH_XDG_SHELL_CLIENT_HEADER}"
      "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
      "${NIXBENCH_APPLICATION_MENU_SERVER_HEADER}"
      "${NIXBENCH_APPLICATION_MENU_CLIENT_HEADER}"
      "${NIXBENCH_APPLICATION_MENU_PRIVATE_CODE}"
    COMMAND
      "${CMAKE_COMMAND}" -E make_directory
      "${NIXBENCH_WAYLAND_GENERATED_DIR}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict server-header
      "${NIXBENCH_XDG_SHELL_XML}"
      "${NIXBENCH_XDG_SHELL_SERVER_HEADER}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict client-header
      "${NIXBENCH_XDG_SHELL_XML}"
      "${NIXBENCH_XDG_SHELL_CLIENT_HEADER}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict private-code
      "${NIXBENCH_XDG_SHELL_XML}"
      "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict server-header
      "${NIXBENCH_APPLICATION_MENU_XML}"
      "${NIXBENCH_APPLICATION_MENU_SERVER_HEADER}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict client-header
      "${NIXBENCH_APPLICATION_MENU_XML}"
      "${NIXBENCH_APPLICATION_MENU_CLIENT_HEADER}"
    COMMAND
      "${NIXBENCH_WAYLAND_SCANNER}" --strict private-code
      "${NIXBENCH_APPLICATION_MENU_XML}"
      "${NIXBENCH_APPLICATION_MENU_PRIVATE_CODE}"
    DEPENDS
      "${NIXBENCH_XDG_SHELL_XML}"
      "${NIXBENCH_APPLICATION_MENU_XML}"
    COMMENT "Generating the Wayland protocol bindings"
    VERBATIM
  )

  add_custom_target(
    nixbench_wayland_protocol
    DEPENDS
      "${NIXBENCH_XDG_SHELL_SERVER_HEADER}"
      "${NIXBENCH_XDG_SHELL_CLIENT_HEADER}"
      "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
      "${NIXBENCH_APPLICATION_MENU_SERVER_HEADER}"
      "${NIXBENCH_APPLICATION_MENU_CLIENT_HEADER}"
      "${NIXBENCH_APPLICATION_MENU_PRIVATE_CODE}"
  )

  add_library(
    nixbench_wayland STATIC
    src/wayland_server.c
    "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
    "${NIXBENCH_APPLICATION_MENU_PRIVATE_CODE}"
  )
  add_dependencies(nixbench_wayland nixbench_wayland_protocol)
  target_include_directories(
    nixbench_wayland
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${NIXBENCH_WAYLAND_GENERATED_DIR}"
      "${NIXBENCH_WAYLAND_SERVER_INCLUDE_DIR}"
    PRIVATE "${NIXBENCH_XKBCOMMON_INCLUDE_DIR}"
  )
  target_link_libraries(
    nixbench_wayland
    PUBLIC "${NIXBENCH_WAYLAND_SERVER_LIBRARY}" m
    PRIVATE "${NIXBENCH_XKBCOMMON_LIBRARY}"
  )
  nixbench_configure_c_target(nixbench_wayland)
endif()

set(NIXBENCH_WAYLAND_CLIENT_AVAILABLE FALSE)
if(NIXBENCH_WAYLAND_ENABLED)
  find_path(
    NIXBENCH_WAYLAND_CLIENT_INCLUDE_DIR
    NAMES wayland-client.h
    PATH_SUFFIXES include
  )
  find_library(
    NIXBENCH_WAYLAND_CLIENT_LIBRARY
    NAMES wayland-client
    PATH_SUFFIXES lib
  )
  if(
    NIXBENCH_WAYLAND_CLIENT_INCLUDE_DIR
    AND NIXBENCH_WAYLAND_CLIENT_LIBRARY
  )
    set(NIXBENCH_WAYLAND_CLIENT_AVAILABLE TRUE)
  else()
    message(
      STATUS
      "Wayland client development files not found; client programs disabled"
    )
  endif()
endif()

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

add_library(
  nixbench_host STATIC
  src/host.c
  src/host_headless.c
)
target_include_directories(
  nixbench_host
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
nixbench_configure_c_target(nixbench_host)

add_library(nixbench_host_fd_wait STATIC src/host_fd_wait.c)
target_include_directories(
  nixbench_host_fd_wait
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
nixbench_configure_c_target(nixbench_host_fd_wait)

add_library(nixbench_privsep_protocol STATIC src/privsep_protocol.c)
target_include_directories(
  nixbench_privsep_protocol
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
nixbench_configure_c_target(nixbench_privsep_protocol)

add_library(nixbench_privsep_helper STATIC src/privsep_helper.c)
target_include_directories(
  nixbench_privsep_helper
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_privsep_helper
  PUBLIC nixbench_host nixbench_privsep_protocol
)
nixbench_configure_c_target(nixbench_privsep_helper)

add_library(nixbench_host_sdl STATIC src/host_sdl.c)
target_include_directories(
  nixbench_host_sdl
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_host_sdl
  PUBLIC nixbench_host
  PRIVATE SDL3::SDL3
)
nixbench_configure_c_target(nixbench_host_sdl)

add_library(nixbench_software_canvas STATIC src/software_canvas.c)
target_include_directories(
  nixbench_software_canvas
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_software_canvas
  PUBLIC nixbench_host
  PRIVATE SDL3::SDL3
)
nixbench_configure_c_target(nixbench_software_canvas)

add_library(
  nixbench_renderers STATIC
  src/desktop_renderer.c
  src/menu_renderer.c
  src/nixinfo_renderer.c
  src/shell_renderer.c
  src/window_renderer.c
)
target_include_directories(
  nixbench_renderers
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_renderers
  PUBLIC SDL3::SDL3 nixbench_model
)
nixbench_configure_c_target(nixbench_renderers)

add_library(nixbench_desktop_preview STATIC src/desktop_preview.c)
target_include_directories(
  nixbench_desktop_preview
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_desktop_preview
  PUBLIC nixbench_host
  PRIVATE
    SDL3::SDL3
    nixbench_model
    nixbench_renderers
    nixbench_software_canvas
)
nixbench_configure_c_target(nixbench_desktop_preview)

add_library(nixbench_desktop_runtime STATIC src/desktop_runtime.c)
target_include_directories(
  nixbench_desktop_runtime
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_desktop_runtime
  PUBLIC nixbench_host
  PRIVATE
    SDL3::SDL3
    nixbench_model
    nixbench_renderers
    nixbench_software_canvas
)
if(NIXBENCH_WAYLAND_ENABLED)
  target_sources(nixbench_desktop_runtime PRIVATE src/wayland_renderer.c)
  target_link_libraries(nixbench_desktop_runtime PRIVATE nixbench_wayland)
  target_compile_definitions(
    nixbench_desktop_runtime
    PRIVATE NIXBENCH_HAS_WAYLAND=1
  )
else()
  target_compile_definitions(
    nixbench_desktop_runtime
    PRIVATE NIXBENCH_HAS_WAYLAND=0
  )
endif()
nixbench_configure_c_target(nixbench_desktop_runtime)

add_library(
  nixbench_framebuffer STATIC
  src/framebuffer_format.c
  src/framebuffer_shadow.c
)
target_include_directories(
  nixbench_framebuffer
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
nixbench_configure_c_target(nixbench_framebuffer)

add_library(nixbench_host_wsdisplay STATIC src/host_wsdisplay.c)
target_include_directories(
  nixbench_host_wsdisplay
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  nixbench_host_wsdisplay
  PUBLIC nixbench_host
  PRIVATE nixbench_framebuffer nixbench_host_fd_wait
)
nixbench_configure_c_target(nixbench_host_wsdisplay)

add_library(nixbench_wscons_input STATIC src/wscons_input.c)
target_include_directories(
  nixbench_wscons_input
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(nixbench_wscons_input PUBLIC nixbench_host)
nixbench_configure_c_target(nixbench_wscons_input)

if(
  BUILD_TESTING
  OR NIXBENCH_BUILD_WSDISPLAY_SMOKE
  OR NIXBENCH_BUILD_WSDISPLAY_SESSION
)
  add_library(
    nixbench_wsdisplay_console_session STATIC
    src/wsdisplay_console_session.c
  )
  target_include_directories(
    nixbench_wsdisplay_console_session
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  nixbench_configure_c_target(nixbench_wsdisplay_console_session)

  add_library(nixbench_wsdisplay_smoke_support STATIC src/wsdisplay_smoke.c)
  target_include_directories(
    nixbench_wsdisplay_smoke_support
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(
    nixbench_wsdisplay_smoke_support
    PUBLIC nixbench_host
  )
  nixbench_configure_c_target(nixbench_wsdisplay_smoke_support)
endif()

if(
  UNIX
  AND (
    BUILD_TESTING
    OR NIXBENCH_BUILD_WSDISPLAY_SESSION
  )
)
  add_library(
    nixbench_session_credentials STATIC
    src/session_credentials.c
  )
  target_include_directories(
    nixbench_session_credentials
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  nixbench_configure_c_target(nixbench_session_credentials)

  add_library(
    nixbench_host_privsep_client STATIC
    src/host_privsep_client.c
  )
  target_include_directories(
    nixbench_host_privsep_client
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(
    nixbench_host_privsep_client
    PUBLIC nixbench_host nixbench_privsep_protocol
  )
  nixbench_configure_c_target(nixbench_host_privsep_client)

  add_library(
    nixbench_wsdisplay_recovery STATIC
    src/wsdisplay_recovery.c
  )
  target_include_directories(
    nixbench_wsdisplay_recovery
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  nixbench_configure_c_target(nixbench_wsdisplay_recovery)
endif()

if(NIXBENCH_BUILD_WSDISPLAY_SMOKE)
  add_executable(
    nixbench-wsdisplay-smoke
    src/wsdisplay_smoke_main.c
    src/wsdisplay_smoke_runner.c
  )
  target_link_libraries(
    nixbench-wsdisplay-smoke
    PRIVATE
      nixbench_desktop_runtime
      nixbench_desktop_preview
      nixbench_host_wsdisplay
      nixbench_wsdisplay_console_session
      nixbench_wscons_input
      nixbench_wsdisplay_smoke_support
  )
  nixbench_configure_c_target(nixbench-wsdisplay-smoke)
endif()

if(NIXBENCH_WAYLAND_ENABLED)
  target_link_libraries(nixbench_wayland PUBLIC nixbench_model)
endif()

add_library(
  nixbench_backend_probe STATIC
  src/backend_probe.c
  src/backend_probe_drm.c
)
target_include_directories(
  nixbench_backend_probe
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(nixbench_backend_probe PRIVATE SDL3::SDL3)
if(NIXBENCH_LIBDRM_ENABLED)
  target_compile_definitions(
    nixbench_backend_probe PRIVATE NIXBENCH_HAS_LIBDRM=1
  )
  target_link_libraries(nixbench_backend_probe PRIVATE NixBench::Libdrm)
else()
  target_compile_definitions(
    nixbench_backend_probe PRIVATE NIXBENCH_HAS_LIBDRM=0
  )
endif()
nixbench_configure_c_target(nixbench_backend_probe)

add_executable(
  nixbench-backend-probe
  src/backend_probe_main.c
)
target_link_libraries(
  nixbench-backend-probe
  PRIVATE SDL3::SDL3 nixbench_backend_probe
)
nixbench_configure_c_target(nixbench-backend-probe)

add_executable(
  nixbench
  src/main.c
)
nixbench_configure_c_target(nixbench)

target_compile_definitions(
  nixbench
  PRIVATE NIXBENCH_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(
  nixbench
  PRIVATE
    SDL3::SDL3
    nixbench_desktop_runtime
    nixbench_host_sdl
)

install(
  TARGETS nixbench nixbench-backend-probe
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

set(NIXBENCH_WAYLAND_DEMO_AVAILABLE FALSE)
if(
  NIXBENCH_WAYLAND_ENABLED
  AND NIXBENCH_WAYLAND_CLIENT_AVAILABLE
  AND NIXBENCH_BUILD_EXAMPLES
)
  set(NIXBENCH_WAYLAND_DEMO_AVAILABLE TRUE)
endif()

if(BUILD_TESTING OR NIXBENCH_WAYLAND_DEMO_AVAILABLE)
  add_library(
    nixbench_wayland_demo_ui STATIC
    examples/wayland_demo_ui.c
  )
  target_include_directories(
    nixbench_wayland_demo_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/examples"
  )
  nixbench_configure_c_target(nixbench_wayland_demo_ui)
endif()

if(NIXBENCH_WAYLAND_DEMO_AVAILABLE)
  add_executable(
    nixbench-wayland-demo
    examples/wayland_demo.c
    "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
  )
  add_dependencies(nixbench-wayland-demo nixbench_wayland_protocol)
  target_include_directories(
    nixbench-wayland-demo
    PRIVATE
      "${NIXBENCH_WAYLAND_GENERATED_DIR}"
      "${NIXBENCH_WAYLAND_CLIENT_INCLUDE_DIR}"
      "${NIXBENCH_XKBCOMMON_INCLUDE_DIR}"
  )
  target_link_libraries(
    nixbench-wayland-demo
    PRIVATE
      nixbench_wayland_demo_ui
      "${NIXBENCH_WAYLAND_CLIENT_LIBRARY}"
      "${NIXBENCH_XKBCOMMON_LIBRARY}"
  )
  nixbench_configure_c_target(nixbench-wayland-demo)
endif()

set(NIXBENCH_NIXCLOCK_AVAILABLE FALSE)
if(
  NIXBENCH_WAYLAND_ENABLED
  AND NIXBENCH_WAYLAND_CLIENT_AVAILABLE
  AND NIXBENCH_BUILD_APPLICATIONS
)
  set(NIXBENCH_NIXCLOCK_AVAILABLE TRUE)
endif()

if(BUILD_TESTING OR NIXBENCH_NIXCLOCK_AVAILABLE)
  add_library(
    nixbench_nixclock_ui STATIC
    apps/nixclock/nixclock_ui.c
  )
  target_include_directories(
    nixbench_nixclock_ui
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/apps/nixclock"
  )
  target_link_libraries(nixbench_nixclock_ui PUBLIC m)
  nixbench_configure_c_target(nixbench_nixclock_ui)
endif()

if(NIXBENCH_NIXCLOCK_AVAILABLE)
  add_executable(
    nixclock
    apps/nixclock/nixclock.c
    "${NIXBENCH_XDG_SHELL_PRIVATE_CODE}"
    "${NIXBENCH_APPLICATION_MENU_PRIVATE_CODE}"
  )
  add_dependencies(nixclock nixbench_wayland_protocol)
  target_include_directories(
    nixclock
    PRIVATE
      "${NIXBENCH_WAYLAND_GENERATED_DIR}"
      "${NIXBENCH_WAYLAND_CLIENT_INCLUDE_DIR}"
  )
  target_compile_definitions(
    nixclock
    PRIVATE NIXCLOCK_VERSION="${PROJECT_VERSION}"
  )
  target_link_libraries(
    nixclock
    PRIVATE
      nixbench_nixclock_ui
      "${NIXBENCH_WAYLAND_CLIENT_LIBRARY}"
  )
  nixbench_configure_c_target(nixclock)
  install(TARGETS nixclock RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(NIXBENCH_BUILD_WSDISPLAY_SESSION)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    message(
      FATAL_ERROR
      "The privilege-separated wsdisplay session is supported only on NetBSD"
    )
  endif()
  if(NOT NIXBENCH_WAYLAND_ENABLED)
    message(
      FATAL_ERROR
      "The wsdisplay session requires NIXBENCH_WAYLAND=ON"
    )
  endif()
  if(NOT NIXBENCH_NIXCLOCK_AVAILABLE)
    message(
      FATAL_ERROR
      "The wsdisplay session requires Wayland client development files and "
      "NIXBENCH_BUILD_APPLICATIONS=ON so NixClock can be launched"
    )
  endif()
endif()

if(
  UNIX
  AND NIXBENCH_WAYLAND_ENABLED
  AND (BUILD_TESTING OR NIXBENCH_BUILD_WSDISPLAY_SESSION)
)
  add_library(nixbench_session_core STATIC src/session_core.c)
  target_include_directories(
    nixbench_session_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(
    nixbench_session_core
    PRIVATE nixbench_desktop_runtime nixbench_host_privsep_client
  )
  nixbench_configure_c_target(nixbench_session_core)
endif()

if(UNIX AND (BUILD_TESTING OR NIXBENCH_BUILD_WSDISPLAY_SESSION))
  add_library(
    nixbench_session_runtime_sentinel STATIC
    src/session_runtime_sentinel.c
  )
  target_include_directories(
    nixbench_session_runtime_sentinel
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  nixbench_configure_c_target(nixbench_session_runtime_sentinel)

  add_library(nixbench_session_watchdog STATIC src/session_watchdog.c)
  target_include_directories(
    nixbench_session_watchdog
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  nixbench_configure_c_target(nixbench_session_watchdog)

  add_library(
    nixbench_wsdisplay_session_runner STATIC
    src/wsdisplay_session_runner.c
  )
  target_include_directories(
    nixbench_wsdisplay_session_runner
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_link_libraries(
    nixbench_wsdisplay_session_runner
    PRIVATE nixbench_session_watchdog
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    target_link_libraries(
      nixbench_wsdisplay_session_runner
      PRIVATE
        nixbench_host_wsdisplay
        nixbench_privsep_helper
        nixbench_session_credentials
        nixbench_wscons_input
        nixbench_wsdisplay_console_session
        nixbench_wsdisplay_recovery
    )
  endif()
  nixbench_configure_c_target(nixbench_wsdisplay_session_runner)
endif()

if(NIXBENCH_BUILD_WSDISPLAY_SESSION)
  add_executable(nixbench-session-core src/session_core_main.c)
  target_link_libraries(
    nixbench-session-core
    PRIVATE nixbench_session_core
  )
  nixbench_configure_c_target(nixbench-session-core)

  add_executable(
    nixbench-wsdisplay-session
    src/wsdisplay_session_main.c
  )
  target_link_libraries(
    nixbench-wsdisplay-session
    PRIVATE nixbench_wsdisplay_session_runner
  )
  nixbench_configure_c_target(nixbench-wsdisplay-session)

  install(
    TARGETS nixbench-session-core nixbench-wsdisplay-session
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  )
endif()

if(BUILD_TESTING)
  add_executable(
    nixbench_backend_probe_tests
    tests/backend_probe_test.c
  )
  target_link_libraries(
    nixbench_backend_probe_tests
    PRIVATE nixbench_backend_probe
  )
  nixbench_configure_c_target(nixbench_backend_probe_tests)

  add_executable(
    nixbench_backend_probe_drm_tests
    tests/backend_probe_drm_test.c
  )
  target_link_libraries(
    nixbench_backend_probe_drm_tests
    PRIVATE nixbench_backend_probe
  )
  nixbench_configure_c_target(nixbench_backend_probe_drm_tests)

  add_executable(nixbench_desktop_tests tests/desktop_test.c)
  target_link_libraries(nixbench_desktop_tests PRIVATE nixbench_model)
  nixbench_configure_c_target(nixbench_desktop_tests)

  add_executable(nixbench_host_tests tests/host_test.c)
  target_link_libraries(nixbench_host_tests PRIVATE nixbench_host)
  nixbench_configure_c_target(nixbench_host_tests)

  add_executable(
    nixbench_host_fd_wait_tests
    tests/host_fd_wait_test.c
  )
  target_link_libraries(
    nixbench_host_fd_wait_tests
    PRIVATE nixbench_host_fd_wait
  )
  nixbench_configure_c_target(nixbench_host_fd_wait_tests)

  add_executable(
    nixbench_privsep_protocol_tests
    tests/privsep_protocol_test.c
  )
  target_link_libraries(
    nixbench_privsep_protocol_tests
    PRIVATE nixbench_privsep_protocol
  )
  nixbench_configure_c_target(nixbench_privsep_protocol_tests)

  add_executable(
    nixbench_privsep_helper_tests
    tests/privsep_helper_test.c
  )
  target_link_libraries(
    nixbench_privsep_helper_tests
    PRIVATE nixbench_privsep_helper
  )
  nixbench_configure_c_target(nixbench_privsep_helper_tests)

  if(UNIX)
    if(NIXBENCH_WAYLAND_ENABLED)
      add_executable(
        nixbench_session_test_application
        tests/session_test_application.c
      )
      nixbench_configure_c_target(nixbench_session_test_application)
    endif()

    add_executable(
      nixbench_session_credentials_tests
      tests/session_credentials_test.c
    )
    target_link_libraries(
      nixbench_session_credentials_tests
      PRIVATE nixbench_session_credentials
    )
    nixbench_configure_c_target(nixbench_session_credentials_tests)

    add_executable(
      nixbench_session_watchdog_tests
      tests/session_watchdog_test.c
    )
    target_link_libraries(
      nixbench_session_watchdog_tests
      PRIVATE nixbench_session_watchdog
    )
    nixbench_configure_c_target(nixbench_session_watchdog_tests)

    add_executable(
      nixbench_session_runtime_sentinel_tests
      tests/session_runtime_sentinel_test.c
    )
    target_link_libraries(
      nixbench_session_runtime_sentinel_tests
      PRIVATE nixbench_session_runtime_sentinel
    )
    nixbench_configure_c_target(nixbench_session_runtime_sentinel_tests)

    add_executable(
      nixbench_host_privsep_client_tests
      tests/session_core_test.c
    )
    target_link_libraries(
      nixbench_host_privsep_client_tests
      PRIVATE nixbench_host_privsep_client
    )
    if(NIXBENCH_WAYLAND_ENABLED)
      target_compile_definitions(
        nixbench_host_privsep_client_tests
        PRIVATE NIXBENCH_TEST_SESSION_CORE=1
      )
      target_link_libraries(
        nixbench_host_privsep_client_tests
        PRIVATE nixbench_session_core
      )
    else()
      target_compile_definitions(
        nixbench_host_privsep_client_tests
        PRIVATE NIXBENCH_TEST_SESSION_CORE=0
      )
    endif()
    nixbench_configure_c_target(nixbench_host_privsep_client_tests)

    add_executable(
      nixbench_wsdisplay_recovery_tests
      tests/wsdisplay_recovery_test.c
    )
    target_link_libraries(
      nixbench_wsdisplay_recovery_tests
      PRIVATE nixbench_wsdisplay_recovery
    )
    nixbench_configure_c_target(nixbench_wsdisplay_recovery_tests)

    add_executable(
      nixbench_wsdisplay_session_tests
      tests/wsdisplay_session_test.c
    )
    target_link_libraries(
      nixbench_wsdisplay_session_tests
      PRIVATE nixbench_wsdisplay_session_runner
    )
    nixbench_configure_c_target(nixbench_wsdisplay_session_tests)
  endif()

  add_executable(nixbench_host_sdl_tests tests/host_sdl_test.c)
  target_link_libraries(
    nixbench_host_sdl_tests
    PRIVATE SDL3::SDL3 nixbench_host_sdl
  )
  nixbench_configure_c_target(nixbench_host_sdl_tests)

  add_executable(
    nixbench_software_canvas_tests
    tests/software_canvas_test.c
  )
  target_link_libraries(
    nixbench_software_canvas_tests
    PRIVATE SDL3::SDL3 nixbench_software_canvas
  )
  nixbench_configure_c_target(nixbench_software_canvas_tests)

  add_executable(
    nixbench_framebuffer_format_tests
    tests/framebuffer_format_test.c
  )
  target_link_libraries(
    nixbench_framebuffer_format_tests
    PRIVATE nixbench_framebuffer
  )
  nixbench_configure_c_target(nixbench_framebuffer_format_tests)

  add_executable(
    nixbench_framebuffer_shadow_tests
    tests/framebuffer_shadow_test.c
  )
  target_link_libraries(
    nixbench_framebuffer_shadow_tests
    PRIVATE nixbench_framebuffer
  )
  nixbench_configure_c_target(nixbench_framebuffer_shadow_tests)

  add_executable(
    nixbench_host_wsdisplay_tests
    tests/host_wsdisplay_test.c
  )
  target_link_libraries(
    nixbench_host_wsdisplay_tests
    PRIVATE nixbench_host_wsdisplay
  )
  nixbench_configure_c_target(nixbench_host_wsdisplay_tests)

  add_executable(
    nixbench_wsdisplay_smoke_tests
    tests/wsdisplay_smoke_test.c
  )
  target_link_libraries(
    nixbench_wsdisplay_smoke_tests
    PRIVATE nixbench_wsdisplay_smoke_support
  )
  nixbench_configure_c_target(nixbench_wsdisplay_smoke_tests)

  add_executable(
    nixbench_wsdisplay_console_session_tests
    tests/wsdisplay_console_session_test.c
  )
  target_link_libraries(
    nixbench_wsdisplay_console_session_tests
    PRIVATE nixbench_wsdisplay_console_session
  )
  nixbench_configure_c_target(nixbench_wsdisplay_console_session_tests)

  add_executable(
    nixbench_wscons_input_tests
    tests/wscons_input_test.c
  )
  target_link_libraries(
    nixbench_wscons_input_tests
    PRIVATE nixbench_wscons_input
  )
  nixbench_configure_c_target(nixbench_wscons_input_tests)

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
  )
  target_link_libraries(
    nixbench_renderer_tests
    PRIVATE nixbench_renderers
  )
  nixbench_configure_c_target(nixbench_renderer_tests)

  add_executable(
    nixbench_desktop_preview_tests
    tests/desktop_preview_test.c
  )
  target_link_libraries(
    nixbench_desktop_preview_tests
    PRIVATE SDL3::SDL3 nixbench_desktop_preview
  )
  nixbench_configure_c_target(nixbench_desktop_preview_tests)

  add_executable(
    nixbench_desktop_runtime_tests
    tests/desktop_runtime_test.c
  )
  target_link_libraries(
    nixbench_desktop_runtime_tests
    PRIVATE SDL3::SDL3 nixbench_desktop_runtime
  )
  nixbench_configure_c_target(nixbench_desktop_runtime_tests)

  add_executable(
    nixbench_wayland_demo_ui_tests
    tests/wayland_demo_ui_test.c
  )
  target_link_libraries(
    nixbench_wayland_demo_ui_tests
    PRIVATE nixbench_wayland_demo_ui
  )
  nixbench_configure_c_target(nixbench_wayland_demo_ui_tests)

  add_executable(
    nixbench_nixclock_ui_tests
    tests/nixclock_ui_test.c
  )
  target_link_libraries(
    nixbench_nixclock_ui_tests
    PRIVATE nixbench_nixclock_ui
  )
  nixbench_configure_c_target(nixbench_nixclock_ui_tests)

  if(NIXBENCH_WAYLAND_CLIENT_AVAILABLE)
    add_executable(nixbench_wayland_tests tests/wayland_test.c)
    add_dependencies(nixbench_wayland_tests nixbench_wayland_protocol)
    target_include_directories(
      nixbench_wayland_tests
      PRIVATE
        "${NIXBENCH_WAYLAND_GENERATED_DIR}"
        "${NIXBENCH_WAYLAND_CLIENT_INCLUDE_DIR}"
        "${NIXBENCH_XKBCOMMON_INCLUDE_DIR}"
    )
    target_link_libraries(
      nixbench_wayland_tests
      PRIVATE
        nixbench_wayland
        "${NIXBENCH_WAYLAND_CLIENT_LIBRARY}"
        "${NIXBENCH_XKBCOMMON_LIBRARY}"
    )
    nixbench_configure_c_target(nixbench_wayland_tests)
    add_test(NAME nixbench.wayland.protocol COMMAND nixbench_wayland_tests)
  endif()

  add_test(NAME nixbench.desktop.model COMMAND nixbench_desktop_tests)
  add_test(NAME nixbench.host.contract COMMAND nixbench_host_tests)
  add_test(NAME nixbench.host.fd-wait COMMAND nixbench_host_fd_wait_tests)
  add_test(
    NAME nixbench.privsep.protocol
    COMMAND nixbench_privsep_protocol_tests
  )
  add_test(
    NAME nixbench.privsep.helper
    COMMAND nixbench_privsep_helper_tests
  )
  if(UNIX)
    add_test(
      NAME nixbench.session.credentials
      COMMAND nixbench_session_credentials_tests
    )
    add_test(
      NAME nixbench.session.watchdog
      COMMAND nixbench_session_watchdog_tests
    )
    add_test(
      NAME nixbench.session.runtime-sentinel
      COMMAND nixbench_session_runtime_sentinel_tests
    )
    if(NIXBENCH_WAYLAND_ENABLED)
      add_test(
        NAME nixbench.privsep.core-host
        COMMAND
          nixbench_host_privsep_client_tests
          $<TARGET_FILE:nixbench_session_test_application>
      )
    else()
      add_test(
        NAME nixbench.privsep.core-host
        COMMAND nixbench_host_privsep_client_tests
      )
    endif()
    add_test(
      NAME nixbench.wsdisplay.recovery-record
      COMMAND nixbench_wsdisplay_recovery_tests
    )
    add_test(
      NAME nixbench.wsdisplay.session-options
      COMMAND nixbench_wsdisplay_session_tests
    )
  endif()
  add_test(NAME nixbench.host.sdl COMMAND nixbench_host_sdl_tests)
  set_tests_properties(
    nixbench.host.sdl
    PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
  )
  add_test(
    NAME nixbench.software-canvas
    COMMAND nixbench_software_canvas_tests
  )
  add_test(
    NAME nixbench.framebuffer.format
    COMMAND nixbench_framebuffer_format_tests
  )
  add_test(
    NAME nixbench.framebuffer.shadow
    COMMAND nixbench_framebuffer_shadow_tests
  )
  add_test(
    NAME nixbench.host.wsdisplay
    COMMAND nixbench_host_wsdisplay_tests
  )
  add_test(
    NAME nixbench.wsdisplay.smoke-support
    COMMAND nixbench_wsdisplay_smoke_tests
  )
  add_test(
    NAME nixbench.wsdisplay.console-session
    COMMAND nixbench_wsdisplay_console_session_tests
  )
  add_test(NAME nixbench.wscons.input COMMAND nixbench_wscons_input_tests)
  if(NIXBENCH_BUILD_WSDISPLAY_SMOKE)
    add_test(
      NAME nixbench.wsdisplay.smoke-help
      COMMAND nixbench-wsdisplay-smoke --help
    )
  endif()
  if(NIXBENCH_BUILD_WSDISPLAY_SESSION)
    add_test(
      NAME nixbench.wsdisplay.session-help
      COMMAND nixbench-wsdisplay-session --help
    )
  endif()
  if(UNIX)
    add_test(
      NAME nixbench.wsdisplay.session-runner-script-syntax
      COMMAND
        sh -n "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-wsdisplay-session.sh"
    )
    add_test(
      NAME nixbench.wsdisplay.session-runner-script
      COMMAND
        sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/wsdisplay_session_runner_script_test.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-wsdisplay-session.sh"
    )
  endif()
  if(UNIX AND EXISTS "/usr/bin/timeout")
    add_test(
      NAME nixbench.wsdisplay.runner-script
      COMMAND
        sh
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/wsdisplay_runner_script_test.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/run-wsdisplay-smoke.sh"
    )
  endif()
  add_test(
    NAME nixbench.backend-probe.unit
    COMMAND nixbench_backend_probe_tests
  )
  add_test(
    NAME nixbench.backend-probe.drm
    COMMAND nixbench_backend_probe_drm_tests
  )
  add_test(
    NAME nixbench.backend-probe.help
    COMMAND nixbench-backend-probe --help
  )
  add_test(
    NAME nixbench.backend-probe.smoke
    COMMAND nixbench-backend-probe
  )
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
  add_test(
    NAME nixbench.desktop.preview
    COMMAND nixbench_desktop_preview_tests
  )
  add_test(
    NAME nixbench.desktop.runtime
    COMMAND nixbench_desktop_runtime_tests
  )
  add_test(
    NAME nixbench.wayland.demo-ui
    COMMAND nixbench_wayland_demo_ui_tests
  )
  add_test(
    NAME nixbench.nixclock.ui
    COMMAND nixbench_nixclock_ui_tests
  )
  add_test(NAME nixbench.cli.version COMMAND nixbench --version)
  add_test(NAME nixbench.cli.help COMMAND nixbench --help)
  add_test(NAME nixbench.cli.unknown-option COMMAND nixbench --unknown-option)
  set_tests_properties(nixbench.cli.unknown-option PROPERTIES WILL_FAIL YES)
  add_test(NAME nixbench.sdl.smoke COMMAND nixbench --exit-after-first-frame)
  set_tests_properties(
    nixbench.sdl.smoke
    PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_RENDER_DRIVER=software"
  )
  if(NIXBENCH_WAYLAND_DEMO_AVAILABLE)
    add_test(NAME nixbench.wayland.demo-help COMMAND nixbench-wayland-demo --help)
  endif()
  if(NIXBENCH_NIXCLOCK_AVAILABLE)
    add_test(NAME nixbench.nixclock.help COMMAND nixclock --help)
    add_test(NAME nixbench.nixclock.version COMMAND nixclock --version)
    add_test(NAME nixbench.nixclock.unknown-option
             COMMAND nixclock --definitely-not-an-option)
    set_tests_properties(
      nixbench.nixclock.unknown-option
      PROPERTIES WILL_FAIL TRUE
    )
  endif()
endif()
