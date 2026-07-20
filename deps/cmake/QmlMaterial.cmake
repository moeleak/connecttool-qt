include_guard(GLOBAL)

include(FetchContent)
set(QM_BUILD_EXAMPLE OFF CACHE BOOL "Build QmlMaterial example" FORCE)
set(QM_BUILD_TESTS OFF CACHE BOOL "Build QmlMaterial tests" FORCE)
set(QML_MATERIAL_BUILD_TYPE STATIC CACHE STRING "QmlMaterial library type" FORCE)
FetchContent_Declare(
    qml_material
    GIT_REPOSITORY https://github.com/moeleak/QmlMaterial.git
    GIT_TAG 4ab7fd009071755b607d5393dd1af07b52c80b5c
    PATCH_COMMAND
        ${CMAKE_COMMAND}
        -DQML_MATERIAL_SOURCE_DIR=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_LIST_DIR}/PatchQmlMaterial.cmake
    EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(qml_material)
