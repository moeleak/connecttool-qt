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

function(connecttool_enable_strict_warnings target)
    if(NOT CONNECTTOOL_ENABLE_STRICT_WARNINGS)
        return()
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Core Network)
qt_standard_project_setup()

add_subdirectory(deps)
add_subdirectory(src/domain)
add_subdirectory(src/network/protocol)

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

# Ensure we always get a headers-only target, regardless of which Boost package
# layout is installed.
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

# This target deliberately stays above the Steamworks/application boundary so
# the core preset proves the transport and VPN layers are SDK-independent.
add_subdirectory(src/network)

if(CONNECTTOOL_BUILD_BENCHMARKS)
    add_executable(connecttool_protocol_benchmark
        benchmarks/protocol_benchmark.cpp)
    target_link_libraries(connecttool_protocol_benchmark PRIVATE
        connecttool_protocol)
    target_compile_features(connecttool_protocol_benchmark PRIVATE cxx_std_23)
endif()

add_library(connecttool_update STATIC
    src/application/services/update_controller.cpp)
add_library(ConnectTool::Update ALIAS connecttool_update)
target_include_directories(connecttool_update PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(connecttool_update PUBLIC cxx_std_23)
target_link_libraries(connecttool_update PUBLIC Qt6::Core Qt6::Network)
connecttool_enable_strict_warnings(connecttool_update)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(NOT CONNECTTOOL_BUILD_APPLICATION)
    return()
endif()

find_package(Qt6 6.11.1 REQUIRED COMPONENTS Gui Qml Quick QuickControls2 Network)

qt_policy(SET QTP0004 NEW)
qt_policy(SET QTP0001 NEW)
if(QT_KNOWN_POLICY_QTP0005)
    qt_policy(SET QTP0005 NEW)
endif()

include(deps/cmake/QmlMaterial.cmake)

# Read version from git tag if available
set(CONNECTTOOL_VERSION "1.6.0-alpha.1")
if(DEFINED ENV{CONNECTTOOL_VERSION})
    set(CONNECTTOOL_VERSION "$ENV{CONNECTTOOL_VERSION}")
endif()
message(STATUS "ConnectTool version: ${CONNECTTOOL_VERSION}")
include(deps/cmake/Steamworks.cmake)
add_subdirectory(src/platform)
add_subdirectory(src/integrations/steam)
add_subdirectory(src/application)
add_subdirectory(modules/ConnectTool)

qt_add_executable(connecttool-qt
    src/main.cpp
    src/application/runtime/application_diagnostics.h
    src/application/runtime/application_diagnostics.cpp)

target_include_directories(connecttool-qt PRIVATE src modules)

qt_add_resources(connecttool-qt bundled_fonts
    PREFIX "/fonts"
    BASE "resources/fonts"
    FILES resources/fonts/NotoSansCJKsc-Regular.otf)

qt_add_resources(connecttool-qt third_party_notices
    PREFIX "/licenses"
    BASE "${CMAKE_SOURCE_DIR}"
    FILES
        THIRD_PARTY_NOTICES.md
        licenses/QmlMaterial-MIT.txt
        licenses/CuteSound-NOTICE.txt
        licenses/Apache-2.0.txt)

if(APPLE)
    set_target_properties(connecttool-qt PROPERTIES MACOSX_BUNDLE TRUE)
    set(APP_ICON_MACOS "${CMAKE_SOURCE_DIR}/resources/ConnectTool.icns")
    set_source_files_properties(${APP_ICON_MACOS}
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(connecttool-qt PRIVATE ${APP_ICON_MACOS})
    set_target_properties(connecttool-qt PROPERTIES MACOSX_BUNDLE_ICON_FILE "ConnectTool.icns")

    add_executable(connecttool-tun-daemon
        src/platform/tun/tun_macos_daemon.cpp
        src/platform/tun/tun_privileged_helper.cpp)
    target_include_directories(connecttool-tun-daemon PRIVATE ${CMAKE_SOURCE_DIR}/src)
    add_dependencies(connecttool-qt connecttool-tun-daemon)
elseif(WIN32)
    set_target_properties(connecttool-qt PROPERTIES WIN32_EXECUTABLE TRUE)
    target_compile_definitions(connecttool-qt PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
    target_sources(connecttool-qt PRIVATE resources/connecttool.rc)
endif()

target_link_libraries(connecttool-qt PRIVATE
    connecttool_ui
    connecttool_uiplugin
    connecttool_application
    connecttool_platform
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network)

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
                "${CMAKE_SOURCE_DIR}/licenses/CuteSound-NOTICE.txt"
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

target_link_libraries(connecttool-qt PRIVATE qml_materialplugin)

if(BUILD_TESTING)
    add_test(NAME qml-smoke COMMAND connecttool-qt --qml-smoke-test)
    set_tests_properties(qml-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 20)
endif()

target_compile_definitions(connecttool-qt PRIVATE CONNECTTOOL_VERSION="${CONNECTTOOL_VERSION}")

target_compile_features(connecttool-qt PRIVATE cxx_std_23)
connecttool_enable_strict_warnings(connecttool-qt)

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
        licenses/CuteSound-NOTICE.txt
        licenses/Apache-2.0.txt
        DESTINATION share/doc/connecttool-qt/licenses)
endif()
if(UNIX AND NOT APPLE)
    install(FILES resources/connecttool.desktop DESTINATION share/applications)
    install(FILES resources/connecttool-qt.png DESTINATION share/icons/hicolor/256x256/apps)
endif()
