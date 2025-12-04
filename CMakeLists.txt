cmake_minimum_required(VERSION 3.20)
project(ConnectToolQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
set(BOOST_MIN_VERSION 1.70)
if(POLICY CMP0167)
    # Prefer FindBoost when config packages are unavailable (older toolchains)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost ${BOOST_MIN_VERSION} CONFIG QUIET)
if(NOT Boost_FOUND)
    message(STATUS "Boost config package not found, falling back to CMake FindBoost.")
    find_package(Boost ${BOOST_MIN_VERSION} REQUIRED)
endif()
find_package(Threads REQUIRED)

qt_standard_project_setup()
qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0001 NEW)

set(STEAMWORKS_PATH_HINT "${CMAKE_SOURCE_DIR}/steamworks" CACHE PATH "Path to the Steamworks SDK (contains public/ and redistributable_bin)")
set(STEAMWORKS_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH "Optional fallback Steamworks SDK path")

set(_steam_include_hints
    ${STEAMWORKS_PATH_HINT}/public
    ${STEAMWORKS_PATH_HINT}/public/steam
    ${STEAMWORKS_SDK_DIR}/public
    ${STEAMWORKS_SDK_DIR}/public/steam)

set(_steam_lib_hints)
if(WIN32)
    list(APPEND _steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/win64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/win64)
elseif(APPLE)
    list(APPEND _steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/osx
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/osx)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    list(APPEND _steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/linuxarm64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/linuxarm64)
else()
    list(APPEND _steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/linux64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64)
endif()

find_path(STEAMWORKS_INCLUDE_DIR steam_api.h PATHS ${_steam_include_hints})
find_library(STEAMWORKS_LIBRARY NAMES steam_api steam_api64 PATHS ${_steam_lib_hints})

if(NOT STEAMWORKS_INCLUDE_DIR OR NOT STEAMWORKS_LIBRARY)
    message(FATAL_ERROR "Steamworks SDK not found. Set STEAMWORKS_PATH_HINT or place the SDK in ./steamworks or ./sdk.")
endif()

if(WIN32)
    set(_steam_runtime_names steam_api64.dll steam_api.dll)
elseif(APPLE)
    set(_steam_runtime_names libsteam_api.dylib)
else()
    set(_steam_runtime_names libsteam_api.so)
endif()

find_file(STEAMWORKS_RUNTIME_LIBRARY
    NAMES ${_steam_runtime_names}
    PATHS ${_steam_lib_hints})

if(WIN32 AND NOT STEAMWORKS_RUNTIME_LIBRARY)
    message(FATAL_ERROR "Steamworks redistributable DLL not found. Ensure steam_api64.dll is available in the redistributable_bin directory.")
endif()

find_file(STEAMWORKS_WEBRTC_LIBRARY
    NAMES
        libsteamwebrtc.dylib
        libsteamwebrtc.so
        steamwebrtc64.dll
    PATHS ${_steam_lib_hints})

if(NOT STEAMWORKS_RUNTIME_LIBRARY)
    set(STEAMWORKS_RUNTIME_LIBRARY ${STEAMWORKS_LIBRARY})
endif()

set(STEAMWORKS_RUNTIME_LIBRARIES ${STEAMWORKS_RUNTIME_LIBRARY})
if(STEAMWORKS_WEBRTC_LIBRARY)
    list(APPEND STEAMWORKS_RUNTIME_LIBRARIES ${STEAMWORKS_WEBRTC_LIBRARY})
endif()

add_executable(connecttool-qt
    src/main.cpp
    src/backend.cpp
    src/lobbies_model.cpp
    src/friends_model.cpp
    src/members_model.cpp
    net/multiplex_manager.cpp
    net/tcp_server.cpp
    net/ip_negotiator.cpp
    net/heartbeat_manager.cpp
    net/node_identity.cpp
    steam/steam_message_handler.cpp
    steam/steam_networking_manager.cpp
    steam/steam_room_manager.cpp
    steam/steam_utils.cpp
    steam/vpn_message_handler.cpp
    steam/steam_vpn_networking_manager.cpp
    steam/steam_vpn_bridge.cpp)

if(WIN32)
    target_sources(connecttool-qt PRIVATE tun/tun_windows.cpp)
elseif(APPLE)
    target_sources(connecttool-qt PRIVATE tun/tun_macos.cpp)
else()
    target_sources(connecttool-qt PRIVATE tun/tun_linux.cpp)
endif()

target_include_directories(connecttool-qt PRIVATE
    src
    net
    steam
    tun
    ${STEAMWORKS_INCLUDE_DIR})

# Ensure we always get a headers-only target, regardless of which Boost package layout is found.
if(NOT TARGET Boost::headers)
    if(TARGET Boost::boost)
        add_library(Boost::headers INTERFACE IMPORTED)
        target_link_libraries(Boost::headers INTERFACE Boost::boost)
    elseif(Boost_INCLUDE_DIRS OR Boost_INCLUDE_DIR)
        set(_boost_includes ${Boost_INCLUDE_DIRS})
        if(NOT _boost_includes)
            set(_boost_includes ${Boost_INCLUDE_DIR})
        endif()
        add_library(Boost_headers INTERFACE)
        target_include_directories(Boost_headers INTERFACE ${_boost_includes})
        add_library(Boost::headers ALIAS Boost_headers)
    else()
        message(FATAL_ERROR "Boost headers not found. Set Boost_ROOT or Boost_INCLUDE_DIR to a valid installation.")
    endif()
endif()

target_link_libraries(connecttool-qt PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Boost::headers
    Threads::Threads
    ${STEAMWORKS_LIBRARY})

if(WIN32)
    target_link_libraries(connecttool-qt PRIVATE iphlpapi ws2_32)
endif()

# Ensure the Steam redistributable is next to the executable at build and
# install time, and make sure the loader can find it on macOS.
add_custom_command(TARGET connecttool-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${STEAMWORKS_RUNTIME_LIBRARIES}
            $<TARGET_FILE_DIR:connecttool-qt>)

if(APPLE)
    set_target_properties(connecttool-qt PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path;@loader_path/../lib")
elseif(UNIX AND NOT APPLE)
    # Keep Steam redistributables discoverable when running from build or install tree
    set_target_properties(connecttool-qt PROPERTIES
        BUILD_RPATH "\$ORIGIN;\$ORIGIN/.."
        INSTALL_RPATH "\$ORIGIN;\$ORIGIN/..")
elseif(WIN32)
    # Ensure DLLs are discoverable on Windows build environments
    set_target_properties(connecttool-qt PROPERTIES
        BUILD_RPATH "$ORIGIN")
endif()

qt_add_qml_module(connecttool-qt
    URI ConnectTool
    VERSION 1.0
    QML_FILES
        qml/ConnectTool/Main.qml
    RESOURCES
        qml/ConnectTool/GitHub.svg
        qml/ConnectTool/QQ.svg
        qml/ConnectTool/Menu.svg)

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt "480\n")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt DESTINATION bin)
install(TARGETS connecttool-qt RUNTIME DESTINATION bin)
install(FILES ${STEAMWORKS_RUNTIME_LIBRARIES} DESTINATION bin OPTIONAL)
