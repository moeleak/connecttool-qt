add_library(connecttool_platform STATIC
    audio/sound_notifier.cpp
    system/platform_environment.cpp)
add_library(ConnectTool::Platform ALIAS connecttool_platform)

if(WIN32)
    target_sources(connecttool_platform PRIVATE
        tun/windows_network_config.cpp
        tun/tun_windows.cpp
        route/route_manager_windows.cpp
        windows/firewall_windows.cpp
        windows/windows_accessibility_guard.cpp
        windows/windows_crash_handler.cpp
        windows/windows_renderer_supervisor.cpp)
    target_compile_definitions(connecttool_platform PUBLIC
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00)
    target_link_libraries(connecttool_platform PRIVATE
        iphlpapi ws2_32 dsound dbghelp ole32 oleaut32)
elseif(APPLE)
    target_sources(connecttool_platform PRIVATE
        tun/tun_macos.cpp
        tun/tun_privileged_helper.cpp
        route/route_manager_macos.cpp)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    target_link_libraries(connecttool_platform PRIVATE
        ${COREAUDIO_LIBRARY} ${AUDIOUNIT_LIBRARY} ${AUDIOTOOLBOX_LIBRARY})
else()
    target_sources(connecttool_platform PRIVATE
        tun/tun_linux.cpp
        route/route_manager_linux.cpp)
    find_package(SDL2 REQUIRED)
    target_link_libraries(connecttool_platform PRIVATE SDL2::SDL2)
endif()

target_include_directories(connecttool_platform PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(connecttool_platform
    PUBLIC Qt6::Core Qt6::Gui
    PRIVATE ConnectTool::CuteSound Threads::Threads)
target_compile_features(connecttool_platform PUBLIC cxx_std_23)

connecttool_enable_strict_warnings(connecttool_platform)
