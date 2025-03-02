# Project
cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(nchat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
include(CheckCXXSourceCompiles)

# Paths
if (NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR "lib")
endif()
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# Build type (Debug, Release, RelWithDebInfo)
set(DEFAULT_BUILD_TYPE "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Using build type '${DEFAULT_BUILD_TYPE}' (default).")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}")
else()
  message(STATUS "Using build type '${CMAKE_BUILD_TYPE}'.")
endif()

# Platform specifics
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  add_definitions(-D_XOPEN_SOURCE_EXTENDED)
  execute_process(COMMAND sh "-c"
                  "command -v brew &> /dev/null && brew --prefix ncurses 2> /dev/null | tr -d '\n'"
                  OUTPUT_VARIABLE CMAKE_PREFIX_PATH)
  if (EXISTS "${CMAKE_PREFIX_PATH}")
    message(STATUS "Ncurses cmake prefix '${CMAKE_PREFIX_PATH}' (detected).")
  else()
    set(CMAKE_PREFIX_PATH /opt/local)
    message(STATUS "Ncurses cmake prefix '${CMAKE_PREFIX_PATH}' (default).")
  endif()
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Android")
  add_compile_definitions(_XOPEN_SOURCE_EXTENDED)
endif()

# Optional component - Dummy Protocol
option(HAS_DUMMY "Dummy" ON)
message(STATUS "Dummy: ${HAS_DUMMY}")

# Optional component - Telegram
option(HAS_TELEGRAM "Telegram" ON)
message(STATUS "Telegram: ${HAS_TELEGRAM}")

# Optional component - WhatsApp
option(HAS_WHATSAPP "WhatsApp" ON)
message(STATUS "WhatsApp: ${HAS_WHATSAPP}")

# Feature - Dynamic library load
option(HAS_DYNAMICLOAD "Dynamic Load" ON)
message(STATUS "Dynamic Load: ${HAS_DYNAMICLOAD}")

# Feature - Multi protocol
option(HAS_MULTIPROTOCOL "Multi Protocol" ON)
message(STATUS "Multi Protocol: ${HAS_MULTIPROTOCOL}")

# Feature - Core dump
option(HAS_COREDUMP "Core Dump" ON)
message(STATUS "Core Dump: ${HAS_COREDUMP}")

# Feature - Static Go library
option(HAS_STATICGOLIB "Static Go" ON)
message(STATUS "Static Go: ${HAS_STATICGOLIB}")

# Check Golang version for whatsmeow minimum requirement
set(GO_VERSION_MIN 1.22)
execute_process(COMMAND bash "-c" "go version 2> /dev/null | cut -c14- | cut -d' ' -f1 | tr -d '\n'" OUTPUT_VARIABLE GO_VERSION)
if(HAS_WHATSAPP AND (NOT GO_VERSION VERSION_GREATER_EQUAL GO_VERSION_MIN))
  message(FATAL_ERROR "Go version ${GO_VERSION} (need >= ${GO_VERSION_MIN} to build WhatsApp).")
endif()

# Application
add_executable(nchat
  ext/apathy/path.hpp
  src/main.cpp
  src/ui.cpp
  src/ui.h
  src/uichatlistdialog.cpp
  src/uichatlistdialog.h
  src/uicolorconfig.cpp
  src/uicolorconfig.h
  src/uiconfig.cpp
  src/uiconfig.h
  src/uicontactlistdialog.cpp
  src/uicontactlistdialog.h
  src/uicontroller.cpp
  src/uicontroller.h
  src/uidialog.cpp
  src/uidialog.h
  src/uiemojilistdialog.cpp
  src/uiemojilistdialog.h
  src/uientryview.cpp
  src/uientryview.h
  src/uifilelistdialog.cpp
  src/uifilelistdialog.h
  src/uihelpview.cpp
  src/uihelpview.h
  src/uihistoryview.cpp
  src/uihistoryview.h
  src/uikeyconfig.cpp
  src/uikeyconfig.h
  src/uikeydump.cpp
  src/uikeydump.h
  src/uikeyinput.cpp
  src/uikeyinput.h
  src/uilistborderview.cpp
  src/uilistborderview.h
  src/uilistdialog.cpp
  src/uilistdialog.h
  src/uilistview.cpp
  src/uilistview.h
  src/uimessagedialog.cpp
  src/uimessagedialog.h
  src/uimodel.cpp
  src/uimodel.h
  src/uiscreen.cpp
  src/uiscreen.h
  src/uistatusview.cpp
  src/uistatusview.h
  src/uitextinputdialog.cpp
  src/uitextinputdialog.h
  src/uitopview.cpp
  src/uitopview.h
  src/uiview.cpp
  src/uiview.h
  src/uiviewbase.cpp
  src/uiviewbase.h
)
install(TARGETS nchat DESTINATION bin)

# Headers
target_include_directories(nchat PRIVATE "ext/apathy")
target_include_directories(nchat PRIVATE "lib/common/src")
target_include_directories(nchat PRIVATE "lib/ncutil/src")

# Compiler flags
set_target_properties(nchat PROPERTIES COMPILE_FLAGS
                      "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                       -Wcast-qual -Wno-missing-braces -Wswitch-default \
                       -Wunreachable-code -Wundef -Wuninitialized \
                       -Wcast-align")

# Dependency libraries
add_subdirectory(lib/ncutil)
if(HAS_DUMMY)
  add_subdirectory(lib/duchat)
endif()
if(HAS_TELEGRAM)
  add_subdirectory(lib/tgchat)
endif()
if(HAS_WHATSAPP)
  add_subdirectory(lib/wmchat)
endif()

# Dependency clip
add_subdirectory(ext/clip)

# Dependency ncurses
set(CURSES_NEED_NCURSES TRUE)
set(CURSES_NEED_WIDE TRUE)
find_package(Curses REQUIRED)
include_directories(${CURSES_INCLUDE_DIR})

# Compile options
target_compile_options(nchat PUBLIC ${NCURSES_CFLAGS})

# Linking
target_link_libraries(nchat PUBLIC ncutil pthread ${CURSES_LIBRARIES} ${CMAKE_DL_LIBS})

# Optionals
if(HAS_DUMMY)
  target_include_directories(nchat PRIVATE "lib/duchat/src")
  target_compile_definitions(nchat PRIVATE HAS_DUMMY="${HAS_DUMMY}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC duchat)
  endif()
endif()

if(HAS_TELEGRAM)
  target_include_directories(nchat PRIVATE "lib/tgchat/src")
  target_compile_definitions(nchat PRIVATE HAS_TELEGRAM="${HAS_TELEGRAM}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC tgchat)
  endif()
endif()

if(HAS_WHATSAPP)
  target_include_directories(nchat PRIVATE "lib/wmchat/src")
  target_compile_definitions(nchat PRIVATE HAS_WHATSAPP="${HAS_WHATSAPP}")
  if(NOT HAS_DYNAMICLOAD)
    target_link_libraries(nchat PUBLIC wmchat)
  endif()
endif()

# Features
if(HAS_MULTIPROTOCOL)
  target_compile_definitions(nchat PRIVATE HAS_MULTIPROTOCOL="${HAS_MULTIPROTOCOL}")
endif()

if(HAS_DYNAMICLOAD)
  target_compile_definitions(nchat PRIVATE HAS_DYNAMICLOAD="${HAS_DYNAMICLOAD}")
endif()

if(HAS_COREDUMP)
  target_compile_definitions(nchat PRIVATE HAS_COREDUMP="${HAS_COREDUMP}")
  if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    # Core dump entitlements
    set(SIGNSCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/utils/sign")
    set(ENTITLEMENTS "${CMAKE_CURRENT_SOURCE_DIR}/src/nchat.entitlements")
    install(CODE "execute_process(COMMAND
      \"${SIGNSCRIPT}\" \"${ENTITLEMENTS}\" \"${CMAKE_INSTALL_PREFIX}/bin/nchat\"
      )" COMPONENT Runtime)
  endif()
endif()

# Common
target_compile_definitions(nchat PRIVATE CMAKE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
                           GO_VERSION="${GO_VERSION}" GO_VERSION_MIN="${GO_VERSION_MIN}")

# Manual install dir
if(NOT CMAKE_INSTALL_MANDIR)
  if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CMAKE_INSTALL_MANDIR "${CMAKE_INSTALL_PREFIX}/share/man")
  else()
    set(CMAKE_INSTALL_MANDIR "${CMAKE_INSTALL_PREFIX}/man")
  endif()
  message(STATUS "Using fallback man dir: ${CMAKE_INSTALL_MANDIR}")
else()
  message(STATUS "Using default man dir: ${CMAKE_INSTALL_MANDIR}")
endif()

# Manual
install(FILES src/nchat.1 DESTINATION "${CMAKE_INSTALL_MANDIR}/man1")

# Themes
macro(add_theme themedir)
  configure_file(themes/${themedir}/color.conf ${CMAKE_CURRENT_BINARY_DIR}/share/nchat/themes/${themedir}/color.conf COPYONLY)
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/share/nchat/themes/${themedir}/color.conf DESTINATION share/nchat/themes/${themedir})
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/themes/${themedir}/usercolor.conf")
    configure_file(themes/${themedir}/usercolor.conf ${CMAKE_CURRENT_BINARY_DIR}/share/nchat/themes/${themedir}/usercolor.conf COPYONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/share/nchat/themes/${themedir}/usercolor.conf DESTINATION share/nchat/themes/${themedir})
  endif()
endmacro()
add_theme("default")
add_theme("basic-color")
add_theme("dracula")
add_theme("catppuccin-mocha")
add_theme("espresso")
add_theme("gruvbox-dark")
add_theme("solarized-dark-higher-contrast")
add_theme("tokyo-night")
add_theme("tomorrow-night")
add_theme("zenbones-dark")
add_theme("zenburned")

# Uninstall
add_custom_target(uninstall
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/bin/nchat"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_MANDIR}/man1/nchat.1"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libncutil${CMAKE_SHARED_LIBRARY_SUFFIX}"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libtdclientshared${CMAKE_SHARED_LIBRARY_SUFFIX}"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libtgchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libduchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
  COMMAND "${CMAKE_COMMAND}" -E remove "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/libwmchat${CMAKE_SHARED_LIBRARY_SUFFIX}"
  COMMAND "${CMAKE_COMMAND}" -E remove_directory "${CMAKE_INSTALL_PREFIX}/share/nchat"
)

# Dev Application
if(CMAKE_BUILD_TYPE MATCHES "Debug")
  message(STATUS "Building dev application.")
  add_executable(devnchat
    ext/apathy/path.hpp
    src/main.cpp
    dev/ui.cpp
    dev/ui.h
  )

  # Headers
  target_include_directories(devnchat PRIVATE "ext/apathy")
  target_include_directories(devnchat PRIVATE "lib/common/src")
  target_include_directories(devnchat PRIVATE "lib/ncutil/src")

  # Compiler flags
  set_target_properties(devnchat PROPERTIES COMPILE_FLAGS
                        "-Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith \
                         -Wcast-qual -Wno-missing-braces -Wswitch-default \
                         -Wunreachable-code -Wundef -Wuninitialized \
                         -Wcast-align")

  # Linking
  target_link_libraries(devnchat PUBLIC ncutil pthread ${CURSES_NCURSES_LIBRARY} dl)

  # Optionals
  if(HAS_DUMMY)
    target_include_directories(devnchat PRIVATE "lib/duchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_DUMMY="${HAS_DUMMY}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC duchat)
    endif()
  endif()

  if(HAS_TELEGRAM)
    target_include_directories(devnchat PRIVATE "lib/tgchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_TELEGRAM="${HAS_TELEGRAM}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC tgchat)
    endif()
  endif()

  if(HAS_WHATSAPP)
    target_include_directories(devnchat PRIVATE "lib/wmchat/src")
    target_compile_definitions(devnchat PRIVATE HAS_WHATSAPP="${HAS_WHATSAPP}")
    if(NOT HAS_DYNAMICLOAD)
      target_link_libraries(devnchat PUBLIC wmchat)
    endif()
  endif()

  # Features
  if(HAS_MULTIPROTOCOL)
    target_compile_definitions(devnchat PRIVATE HAS_MULTIPROTOCOL="${HAS_MULTIPROTOCOL}")
  endif()

  if(HAS_DYNAMICLOAD)
    target_compile_definitions(devnchat PRIVATE HAS_DYNAMICLOAD="${HAS_DYNAMICLOAD}")
  endif()

  # Common
  target_compile_definitions(devnchat PRIVATE CMAKE_INSTALL_LIBDIR="${CMAKE_INSTALL_LIBDIR}"
                             GO_VERSION="${GO_VERSION}" GO_VERSION_MIN="${GO_VERSION_MIN}")

endif()
