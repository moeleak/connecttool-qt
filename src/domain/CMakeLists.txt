add_library(connecttool_domain INTERFACE)
add_library(ConnectTool::Domain ALIAS connecttool_domain)
target_include_directories(connecttool_domain INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(connecttool_domain INTERFACE cxx_std_23)
