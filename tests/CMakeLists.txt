find_package(Qt6 6.11.1 REQUIRED COMPONENTS Qml Test)

qt_add_executable(connecttool_protocol_tests
    protocol_tests.cpp
    ../src/lobbies_model.cpp)
target_include_directories(connecttool_protocol_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(connecttool_protocol_tests PRIVATE
    connecttool_protocol
    connecttool_update
    Qt6::Core
    Qt6::Qml
    Qt6::Test)
target_compile_features(connecttool_protocol_tests PRIVATE cxx_std_23)

add_test(NAME protocol COMMAND connecttool_protocol_tests)

if(WIN32)
    add_executable(connecttool_windows_firewall_tests
        windows_firewall_test.cpp
        ../src/firewall_windows.cpp)
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
