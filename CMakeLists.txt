cmake_minimum_required(VERSION 3.16)

project(ActiveWindowsLogger VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Platform independent core: CSV logging, settings, idle/pause handling.
add_library(awl_common STATIC
  common/ActivityLogger.cpp
  common/ActivityLogger.h
  common/Settings.cpp
  common/Settings.h
  common/TextUtils.cpp
  common/TextUtils.h
  common/WindowInfo.h
)
target_include_directories(awl_common PUBLIC common)

if(WIN32)
  # ---------------------------------------------------------------- Windows
  target_compile_definitions(awl_common PUBLIC UNICODE _UNICODE)
  if(MSVC)
    target_compile_options(awl_common PUBLIC /utf-8)
  endif()

  add_executable(ActiveWindowsLogger WIN32
    ActiveWindowsLogger/ActiveWindowsLogger.cpp
    ActiveWindowsLogger/SystemTray.cpp
    ActiveWindowsLogger/utils.cpp
    ActiveWindowsLogger/ActiveWindowsLogger.rc
  )
  target_link_libraries(ActiveWindowsLogger PRIVATE awl_common comctl32 wtsapi32 shell32 ole32)
  install(TARGETS ActiveWindowsLogger RUNTIME DESTINATION .)

elseif(UNIX AND NOT APPLE)
  # ---------------------------------------------------------------- Linux
  include(GNUInstallDirs)
  find_package(PkgConfig REQUIRED)

  option(AWL_WITH_X11 "X11 support (Xlib + XScreenSaver)" ON)
  option(AWL_WITH_SDBUS "D-Bus support via sd-bus: KDE Wayland, screen lock and sleep detection" ON)
  option(AWL_WITH_WAYLAND "Wayland idle detection (ext-idle-notify-v1)" ON)

  add_executable(activewindowslogger
    linux/main.cpp
    linux/ProcessInfo.cpp
    linux/Sources.h
  )
  target_link_libraries(activewindowslogger PRIVATE awl_common)
  target_compile_definitions(activewindowslogger PRIVATE AWL_VERSION="${PROJECT_VERSION}")

  set(AWL_FEATURES "")

  if(AWL_WITH_X11)
    pkg_check_modules(X11 IMPORTED_TARGET x11 xscrnsaver)
    if(X11_FOUND)
      target_sources(activewindowslogger PRIVATE linux/X11Source.cpp linux/X11Source.h)
      target_link_libraries(activewindowslogger PRIVATE PkgConfig::X11)
      target_compile_definitions(activewindowslogger PRIVATE AWL_HAVE_X11)
      list(APPEND AWL_FEATURES "X11")
    else()
      message(WARNING "x11/xscrnsaver not found: X11 support disabled (install libx11-dev libxss-dev)")
    endif()
  endif()

  if(AWL_WITH_SDBUS)
    pkg_check_modules(SDBUS IMPORTED_TARGET libsystemd)
    if(NOT SDBUS_FOUND)
      pkg_check_modules(SDBUS IMPORTED_TARGET libelogind)
    endif()
    if(SDBUS_FOUND)
      target_sources(activewindowslogger PRIVATE
        linux/KWinSource.cpp linux/KWinSource.h
        linux/SessionMonitor.cpp linux/SessionMonitor.h)
      target_link_libraries(activewindowslogger PRIVATE PkgConfig::SDBUS)
      target_compile_definitions(activewindowslogger PRIVATE AWL_HAVE_SDBUS)
      list(APPEND AWL_FEATURES "sd-bus (KWin, logind)")
    else()
      message(WARNING "libsystemd not found: KDE Wayland support disabled (install libsystemd-dev)")
    endif()
  endif()

  if(AWL_WITH_WAYLAND)
    pkg_check_modules(WAYLAND IMPORTED_TARGET wayland-client)
    find_program(WAYLAND_SCANNER wayland-scanner)
    if(WAYLAND_FOUND AND WAYLAND_SCANNER)
      set(_xml ${CMAKE_CURRENT_SOURCE_DIR}/linux/protocols/ext-idle-notify-v1.xml)
      set(_gen ${CMAKE_CURRENT_BINARY_DIR}/protocols)
      file(MAKE_DIRECTORY ${_gen})
      add_custom_command(
        OUTPUT ${_gen}/ext-idle-notify-v1-client-protocol.h ${_gen}/ext-idle-notify-v1-protocol.c
        COMMAND ${WAYLAND_SCANNER} client-header ${_xml} ${_gen}/ext-idle-notify-v1-client-protocol.h
        COMMAND ${WAYLAND_SCANNER} private-code ${_xml} ${_gen}/ext-idle-notify-v1-protocol.c
        DEPENDS ${_xml}
        VERBATIM)
      enable_language(C)
      target_sources(activewindowslogger PRIVATE
        linux/WaylandIdle.cpp linux/WaylandIdle.h
        ${_gen}/ext-idle-notify-v1-client-protocol.h ${_gen}/ext-idle-notify-v1-protocol.c)
      target_include_directories(activewindowslogger PRIVATE ${_gen})
      target_link_libraries(activewindowslogger PRIVATE PkgConfig::WAYLAND)
      target_compile_definitions(activewindowslogger PRIVATE AWL_HAVE_WAYLAND)
      list(APPEND AWL_FEATURES "Wayland idle")
    else()
      message(WARNING "wayland-client/wayland-scanner not found: Wayland idle detection disabled (install libwayland-dev)")
    endif()
  endif()

  if(NOT X11_FOUND AND NOT SDBUS_FOUND)
    message(FATAL_ERROR "Neither X11 nor sd-bus found: no way to detect the active window")
  endif()

  message(STATUS "ActiveWindowsLogger features: ${AWL_FEATURES}")

  install(TARGETS activewindowslogger RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

  configure_file(linux/packaging/activewindowslogger.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/activewindowslogger.service @ONLY)
  configure_file(linux/packaging/activewindowslogger.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/activewindowslogger.desktop @ONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/activewindowslogger.service
    DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/activewindowslogger.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
endif()
