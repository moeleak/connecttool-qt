cmake_minimum_required(VERSION 3.28)
project(ConnectToolQt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Xcode 16's libc++ still gates C++20 stop_token/jthread behind its
# experimental-library switch. Keep the standard API on Apple platforms
# instead of maintaining a project-local threading fallback.
if(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    add_compile_options(-fexperimental-library)
    add_link_options(-fexperimental-library)
endif()

# No target currently uses C++ modules. CMake otherwise enables dependency
# scanning for C++23, and clang-scan-deps loses Nix's libc++ include paths on
# Darwin because it bypasses part of the compiler wrapper.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

option(CONNECTTOOL_BUILD_APPLICATION "Build the ConnectTool desktop application" ON)
option(CONNECTTOOL_BUILD_BENCHMARKS "Build repeatable protocol microbenchmarks" OFF)
option(CONNECTTOOL_ENABLE_STRICT_WARNINGS "Enable strict warnings for first-party code" ON)
option(CONNECTTOOL_ADHOC_SIGN_MACOS
    "Ad-hoc sign the development app after install_name_tool changes" ON)

include(CTest)

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Network)
qt_standard_project_setup()

add_library(connecttool_protocol STATIC
    net/multiplex_protocol.cpp)
target_include_directories(connecttool_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(connecttool_protocol PUBLIC cxx_std_23)

if(CONNECTTOOL_BUILD_BENCHMARKS)
    add_executable(connecttool_protocol_benchmark
        benchmarks/protocol_benchmark.cpp)
    target_link_libraries(connecttool_protocol_benchmark PRIVATE
        connecttool_protocol)
    target_compile_features(connecttool_protocol_benchmark PRIVATE cxx_std_23)
endif()

if(CONNECTTOOL_ENABLE_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(connecttool_protocol PRIVATE /W4 /permissive-)
    else()
        target_compile_options(connecttool_protocol PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endif()

add_library(connecttool_update STATIC
    src/update_controller.cpp)
target_include_directories(connecttool_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(connecttool_update PUBLIC cxx_std_23)
target_link_libraries(connecttool_update PUBLIC Qt6::Core Qt6::Network)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(NOT CONNECTTOOL_BUILD_APPLICATION)
    return()
endif()

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Network)

# QmlMaterial supplies the Material Design 3 controls used by the desktop UI.
# Keep it static so the QML plugin and its assets travel with the application
# on every platform. Nix injects a pre-fetched source directory; regular CMake
# builds fetch the same pinned revision on first configure.
include(FetchContent)
set(QM_BUILD_EXAMPLE OFF CACHE BOOL "Build QmlMaterial example" FORCE)
set(QM_BUILD_TESTS OFF CACHE BOOL "Build QmlMaterial tests" FORCE)
set(QML_MATERIAL_BUILD_TYPE STATIC CACHE STRING "QmlMaterial library type" FORCE)
FetchContent_Declare(
    qml_material
    GIT_REPOSITORY https://github.com/hypengw/QmlMaterial.git
    GIT_TAG e8b275ba4e9852fe94394d565604d628e1fef03c
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(qml_material)

# Read version from git tag if available
set(CONNECTTOOL_VERSION "1.6.0-alpha.1")
if(DEFINED ENV{CONNECTTOOL_VERSION})
    set(CONNECTTOOL_VERSION "$ENV{CONNECTTOOL_VERSION}")
endif()
message(STATUS "ConnectTool version: ${CONNECTTOOL_VERSION}")
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

qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0005)
    qt_policy(SET QTP0005 NEW)
endif()

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

qt_add_executable(connecttool-qt
    src/main.cpp
    src/application_diagnostics.h
    src/application_diagnostics.cpp
    src/application_controller.h
    src/application_controller.cpp
    src/platform_environment.cpp
    src/backend.cpp
    src/lobbies_model.h
    src/lobbies_model.cpp
    src/friends_model.h
    src/friends_model.cpp
    src/chat_model.h
    src/chat_model.cpp
    src/members_model.h
    src/members_model.cpp
    src/sound_notifier.cpp
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

qt_add_resources(connecttool-qt bundled_fonts
    PREFIX "/fonts"
    BASE "resources/fonts"
    FILES
        resources/fonts/NotoSansCJKsc-Regular.otf)

qt_add_resources(connecttool-qt third_party_notices
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES
        THIRD_PARTY_NOTICES.md
        licenses/QmlMaterial-MIT.txt
        licenses/Apache-2.0.txt)

if(APPLE)
    set_target_properties(connecttool-qt PROPERTIES MACOSX_BUNDLE TRUE)
    set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/ConnectTool.icns")
    set_source_files_properties(${APP_ICON_MACOS}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(connecttool-qt PRIVATE ${APP_ICON_MACOS})
    set_target_properties(connecttool-qt PROPERTIES
        MACOSX_BUNDLE_ICON_FILE "ConnectTool.icns")
endif()

if(WIN32)
    set_target_properties(connecttool-qt PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(connecttool-qt PRIVATE
        src/windows_accessibility_guard.h
        src/windows_accessibility_guard.cpp
        src/windows_renderer_supervisor.h
        src/windows_renderer_supervisor.cpp
        tun/tun_windows.cpp
        src/firewall_windows.cpp
        resources/connecttool.rc)
elseif(APPLE)
    target_sources(connecttool-qt PRIVATE
        tun/tun_macos.cpp
        tun/tun_privileged_helper.cpp)
else()
    target_sources(connecttool-qt PRIVATE tun/tun_linux.cpp)
endif()

if(APPLE)
    add_executable(connecttool-tun-daemon
        tun/tun_macos_daemon.cpp
        tun/tun_privileged_helper.cpp)
    target_include_directories(connecttool-tun-daemon PRIVATE tun)
    add_dependencies(connecttool-qt connecttool-tun-daemon)
endif()

target_include_directories(connecttool-qt PRIVATE
    src
    net
    steam
    tun)
target_include_directories(connecttool-qt SYSTEM PRIVATE
    third_party
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
    connecttool_protocol
    connecttool_update
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Boost::headers
    Threads::Threads
    ${STEAMWORKS_LIBRARY})

if(WIN32)
    target_link_libraries(connecttool-qt PRIVATE iphlpapi ws2_32 dsound)
elseif(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    target_link_libraries(connecttool-qt PRIVATE
        ${COREAUDIO_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY})
else()
    find_package(SDL2 REQUIRED)
    target_link_libraries(connecttool-qt PRIVATE SDL2::SDL2)
endif()

# Ensure the Steam redistributable is next to the executable at build and
# install time, and make sure the loader can find it on macOS.
if(APPLE)
    set(_steamworks_bundle_frameworks
        "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Frameworks")
    set(_steamworks_bundle_binary "$<TARGET_FILE:connecttool-qt>")
    add_custom_command(TARGET connecttool-qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "${_steamworks_bundle_frameworks}"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${STEAMWORKS_RUNTIME_LIBRARIES}
                "${_steamworks_bundle_frameworks}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/MacOS"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:connecttool-tun-daemon>"
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/connecttool-tun-daemon"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/resources/macos/com.connecttool.tunhelper.plist"
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/com.connecttool.tunhelper.plist"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/resources/fonts/LICENSE-NotoSansCJK.txt"
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/LICENSE-NotoSansCJK.txt"
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/licenses/QmlMaterial-MIT.txt"
                "${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt"
                "$<TARGET_BUNDLE_DIR:connecttool-qt>/Contents/Resources/licenses"
        COMMAND install_name_tool -id
                "@loader_path/../Frameworks/libsteam_api.dylib"
                "${_steamworks_bundle_frameworks}/libsteam_api.dylib"
        COMMAND install_name_tool -change
                "@loader_path/libsteam_api.dylib"
                "@loader_path/../Frameworks/libsteam_api.dylib"
                "${_steamworks_bundle_binary}")
    if(STEAMWORKS_WEBRTC_LIBRARY)
        add_custom_command(TARGET connecttool-qt POST_BUILD
            COMMAND install_name_tool -id
                    "@loader_path/../Frameworks/libsteamwebrtc.dylib"
                    "${_steamworks_bundle_frameworks}/libsteamwebrtc.dylib")
    endif()
    if(CONNECTTOOL_ADHOC_SIGN_MACOS)
        # install_name_tool invalidates Mach-O signatures.  Re-sign the whole
        # development bundle so current macOS versions do not kill it in dyld.
        # A distribution identity can safely replace this signature later.
        add_custom_command(TARGET connecttool-qt POST_BUILD
            COMMAND /usr/bin/codesign --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:connecttool-qt>")
    endif()
else()
    add_custom_command(TARGET connecttool-qt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${STEAMWORKS_RUNTIME_LIBRARIES}
                $<TARGET_FILE_DIR:connecttool-qt>)
endif()

if(APPLE)
    set_target_properties(connecttool-qt PROPERTIES
        BUILD_RPATH "@loader_path"
        INSTALL_RPATH "@loader_path;@loader_path/../Frameworks;@loader_path/../lib")
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

set_source_files_properties(qml/ConnectTool/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(connecttool-qt
    URI ConnectTool
    VERSION 1.0
    DEPENDENCIES
        TARGET qml_material
    QML_FILES
        qml/ConnectTool/Main.qml
        qml/ConnectTool/Theme.qml
        qml/ConnectTool/NavigationDrawer.qml
        qml/ConnectTool/AppCard.qml
        qml/ConnectTool/LinkCard.qml
        qml/ConnectTool/StatusPill.qml
        qml/ConnectTool/Avatar.qml
        qml/ConnectTool/ConnectionPanel.qml
        qml/ConnectTool/PortField.qml
        qml/ConnectTool/ChatPanel.qml
        qml/ConnectTool/ChatMessageDelegate.qml
        qml/ConnectTool/PeoplePanel.qml
        qml/ConnectTool/MemberDelegate.qml
        qml/ConnectTool/FriendDelegate.qml
        qml/ConnectTool/RoomPage.qml
        qml/ConnectTool/LobbyPage.qml
        qml/ConnectTool/NetworkPage.qml
        qml/ConnectTool/AboutPage.qml
    RESOURCES
        qml/ConnectTool/GitHub.svg
        qml/ConnectTool/QQ.svg
        qml/ConnectTool/AppIcon.png
        qml/ConnectTool/notify.wav)

# QmlMaterial is built as a static QML module. Explicitly retaining the plugin
# pairs with Q_IMPORT_QML_PLUGIN in main.cpp even though Qt itself is shared.
target_link_libraries(connecttool-qt PRIVATE qml_materialplugin)

if(BUILD_TESTING)
    add_test(NAME qml-smoke COMMAND connecttool-qt --qml-smoke-test)
    set_tests_properties(qml-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 20)
endif()

target_compile_definitions(connecttool-qt PRIVATE CONNECTTOOL_VERSION="${CONNECTTOOL_VERSION}")

target_compile_features(connecttool-qt PRIVATE cxx_std_23)
if(CONNECTTOOL_ENABLE_STRICT_WARNINGS)
    if(MSVC)
        target_compile_options(connecttool-qt PRIVATE /W4 /permissive-)
    else()
        target_compile_options(connecttool-qt PRIVATE
            -Wall -Wextra -Wpedantic)
    endif()
endif()

if(DEFINED ENV{NIXPKGS_QT6_QML_IMPORT_PATH})
    target_compile_definitions(connecttool-qt PRIVATE
        CONNECTTOOL_NIX_QML_IMPORT_PATH="$ENV{NIXPKGS_QT6_QML_IMPORT_PATH}")
endif()
if(DEFINED ENV{NIXPKGS_QT6_PLUGIN_PATH})
    target_compile_definitions(connecttool-qt PRIVATE
        CONNECTTOOL_NIX_PLUGIN_PATH="$ENV{NIXPKGS_QT6_PLUGIN_PATH}")
endif()

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt "480\n")
if(APPLE)
    install(TARGETS connecttool-qt BUNDLE DESTINATION .)
else()
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/steam_appid.txt DESTINATION bin)
    install(TARGETS connecttool-qt RUNTIME DESTINATION bin)
    install(FILES ${STEAMWORKS_RUNTIME_LIBRARIES} DESTINATION bin OPTIONAL)
    install(FILES resources/fonts/LICENSE-NotoSansCJK.txt DESTINATION share/doc/connecttool-qt)
    install(FILES THIRD_PARTY_NOTICES.md DESTINATION share/doc/connecttool-qt)
    install(FILES
        licenses/QmlMaterial-MIT.txt
        licenses/Apache-2.0.txt
        DESTINATION share/doc/connecttool-qt/licenses)
endif()
if(UNIX AND NOT APPLE)
    install(FILES resources/connecttool.desktop DESTINATION share/applications)
    install(FILES resources/connecttool-qt.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
