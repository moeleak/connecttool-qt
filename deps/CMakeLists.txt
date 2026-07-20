add_library(connecttool_cute_sound INTERFACE)
target_include_directories(connecttool_cute_sound SYSTEM INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/cute_sound)
add_library(ConnectTool::CuteSound ALIAS connecttool_cute_sound)
