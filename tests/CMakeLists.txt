find_package(Qt6 6.11.1 REQUIRED COMPONENTS Qml Test)

qt_add_executable(connecttool_protocol_tests
    protocol_tests.cpp
    ../modules/ConnectTool/models/lobbies_model.cpp)
target_include_directories(connecttool_protocol_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/modules)
target_link_libraries(connecttool_protocol_tests PRIVATE
    connecttool_network
    connecttool_protocol
    connecttool_update
    Qt6::Core
    Qt6::Qml
    Qt6::Test)
target_compile_features(connecttool_protocol_tests PRIVATE cxx_std_23)

add_test(NAME protocol COMMAND connecttool_protocol_tests)

add_test(NAME architecture-boundaries
    COMMAND ${CMAKE_COMMAND}
        -DPROJECT_ROOT=${PROJECT_SOURCE_DIR}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/verify_architecture.cmake)

if(WIN32)
    add_executable(connecttool_windows_firewall_tests
        windows_firewall_test.cpp
        ../src/platform/windows/firewall_windows.cpp)
    target_include_directories(connecttool_windows_firewall_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(connecttool_windows_firewall_tests PRIVATE
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00)
    target_compile_features(connecttool_windows_firewall_tests PRIVATE cxx_std_23)
    target_link_libraries(connecttool_windows_firewall_tests PRIVATE
        ole32
        oleaut32)
    add_test(NAME windows-firewall-native COMMAND connecttool_windows_firewall_tests)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(connecttool_route_linux_tests
        route_message_linux_test.cpp
        ../src/platform/route/route_manager_linux.cpp)
    target_include_directories(connecttool_route_linux_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(connecttool_route_linux_tests PRIVATE cxx_std_23)
    add_test(NAME route-linux-message COMMAND connecttool_route_linux_tests)
endif()

if(APPLE)
    add_executable(connecttool_route_macos_tests
        route_message_macos_test.cpp
        ../src/platform/route/route_manager_macos.cpp)
    target_include_directories(connecttool_route_macos_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/src)
    target_compile_features(connecttool_route_macos_tests PRIVATE cxx_std_23)
    add_test(NAME route-macos-message COMMAND connecttool_route_macos_tests)
endif()
