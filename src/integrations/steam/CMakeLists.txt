add_library(connecttool_steam STATIC
    steam_message_handler.cpp
    steam_networking_manager.cpp
    steam_reliable_channel.cpp
    steam_room_manager.cpp
    steam_utils.cpp
    steam_vpn_bridge.cpp
    steam_vpn_networking_manager.cpp
    vpn_message_handler.cpp)
add_library(ConnectTool::Steam ALIAS connecttool_steam)

target_include_directories(connecttool_steam PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(connecttool_steam
    PUBLIC connecttool_domain connecttool_network connecttool_platform Steamworks::Steamworks
    PRIVATE Qt6::Core Boost::headers Threads::Threads)
target_compile_features(connecttool_steam PUBLIC cxx_std_23)

connecttool_enable_strict_warnings(connecttool_steam)
