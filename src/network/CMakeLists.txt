add_library(connecttool_network STATIC
    tcp/multiplex_session.cpp
    tcp/tcp_tunnel_server.cpp
    vpn/heartbeat_manager.cpp
    vpn/ip_negotiator.cpp
    vpn/node_identity.cpp
    vpn/sha256.cpp)
add_library(ConnectTool::Network ALIAS connecttool_network)

target_include_directories(connecttool_network PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(connecttool_network
    PUBLIC connecttool_domain connecttool_protocol Boost::headers Threads::Threads)
target_compile_features(connecttool_network PUBLIC cxx_std_23)

connecttool_enable_strict_warnings(connecttool_network)
