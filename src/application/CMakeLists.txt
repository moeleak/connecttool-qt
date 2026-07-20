add_library(connecttool_application STATIC
    runtime/application_runtime.cpp
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/chat_model.cpp
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/friends_model.cpp
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/lobbies_model.cpp
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/members_model.cpp)
add_library(ConnectTool::Application ALIAS connecttool_application)

# The model metaobjects and QML annotations belong to the ConnectTool backing
# module. Avoid generating a second copy while compiling their implementations
# into the application service library.
set_source_files_properties(
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/chat_model.h
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/friends_model.h
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/lobbies_model.h
    ${PROJECT_SOURCE_DIR}/modules/ConnectTool/models/members_model.h
    PROPERTIES SKIP_AUTOMOC ON)

set_target_properties(connecttool_application PROPERTIES AUTOMOC ON)
target_include_directories(connecttool_application PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/modules)
target_link_libraries(connecttool_application
    PUBLIC connecttool_update connecttool_steam Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network
    PRIVATE connecttool_network connecttool_platform
            Boost::headers Threads::Threads)
target_compile_features(connecttool_application PUBLIC cxx_std_23)

connecttool_enable_strict_warnings(connecttool_application)
