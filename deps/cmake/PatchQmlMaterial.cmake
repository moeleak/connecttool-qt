if(NOT QML_MATERIAL_SOURCE_DIR)
    message(FATAL_ERROR "QML_MATERIAL_SOURCE_DIR is required")
endif()

set(_cmake_file "${QML_MATERIAL_SOURCE_DIR}/CMakeLists.txt")
file(READ "${_cmake_file}" _contents)

# The pinned QmlMaterial revision selects its Linux XDG portal implementation
# for every UNIX host, including macOS. Patch only the downloaded dependency:
# macOS can use the existing portable platform and file-dialog stubs.
set(_original_contents "${_contents}")
string(REPLACE
    "if(UNIX)\n  set(LINUX CACHE INTERNAL TRUE \"\")"
    "if(UNIX AND NOT APPLE)\n  set(LINUX CACHE INTERNAL TRUE \"\")"
    _contents "${_contents}")
string(REPLACE
    "if(UNIX AND NOT ANDROID AND NOT EMSCRIPTEN)"
    "if(UNIX AND NOT APPLE AND NOT ANDROID AND NOT EMSCRIPTEN)"
    _contents "${_contents}")
string(REPLACE
    "  if(UNIX)\n    if(ANDROID)"
    "  if(UNIX AND NOT APPLE)\n    if(ANDROID)"
    _contents "${_contents}")
string(REPLACE
    "  elseif(WIN32)\n    set(QM_ICON_FILL_0_FONT_FILE"
    "  elseif(APPLE)\n    list(APPEND SOURCES src/platform/win/win.cpp)\n    list(APPEND SOURCES src/dialog/file_dialog_stub.cpp)\n  elseif(WIN32)\n    set(QM_ICON_FILL_0_FONT_FILE"
    _contents "${_contents}")

if(NOT _contents MATCHES
   "if\\(UNIX AND NOT APPLE\\)\n    if\\(ANDROID\\)" OR
   NOT _contents MATCHES
   "elseif\\(APPLE\\)\n    list\\(APPEND SOURCES src/platform/win/win.cpp\\)")
    message(FATAL_ERROR "Pinned QmlMaterial platform block has changed")
endif()

if(NOT _contents STREQUAL _original_contents)
    file(WRITE "${_cmake_file}" "${_contents}")
endif()
