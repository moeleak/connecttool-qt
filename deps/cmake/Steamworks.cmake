include_guard(GLOBAL)

set(STEAMWORKS_PATH_HINT "${CMAKE_SOURCE_DIR}/steamworks" CACHE PATH
    "Path to the Steamworks SDK (contains public/ and redistributable_bin)")
set(STEAMWORKS_SDK_DIR "${CMAKE_SOURCE_DIR}/sdk" CACHE PATH
    "Optional fallback Steamworks SDK path")

set(_steam_include_hints
    ${STEAMWORKS_PATH_HINT}/public
    ${STEAMWORKS_PATH_HINT}/public/steam
    ${STEAMWORKS_SDK_DIR}/public
    ${STEAMWORKS_SDK_DIR}/public/steam)

if(WIN32)
    set(_steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/win64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/win64)
elseif(APPLE)
    set(_steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/osx
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/osx)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/linuxarm64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/linuxarm64)
else()
    set(_steam_lib_hints
        ${STEAMWORKS_PATH_HINT}/redistributable_bin/linux64
        ${STEAMWORKS_SDK_DIR}/redistributable_bin/linux64)
endif()

find_path(STEAMWORKS_INCLUDE_DIR steam_api.h PATHS ${_steam_include_hints})
find_library(STEAMWORKS_LIBRARY NAMES steam_api steam_api64 PATHS ${_steam_lib_hints})
if(NOT STEAMWORKS_INCLUDE_DIR OR NOT STEAMWORKS_LIBRARY)
    message(FATAL_ERROR
        "Steamworks SDK not found. Set STEAMWORKS_PATH_HINT or place it in ./steamworks or ./sdk.")
endif()

if(WIN32)
    set(_steam_runtime_names steam_api64.dll steam_api.dll)
elseif(APPLE)
    set(_steam_runtime_names libsteam_api.dylib)
else()
    set(_steam_runtime_names libsteam_api.so)
endif()

find_file(STEAMWORKS_RUNTIME_LIBRARY NAMES ${_steam_runtime_names} PATHS ${_steam_lib_hints})
if(WIN32 AND NOT STEAMWORKS_RUNTIME_LIBRARY)
    message(FATAL_ERROR "Steamworks redistributable DLL not found.")
endif()
find_file(STEAMWORKS_WEBRTC_LIBRARY
    NAMES libsteamwebrtc.dylib libsteamwebrtc.so steamwebrtc64.dll
    PATHS ${_steam_lib_hints})

if(NOT STEAMWORKS_RUNTIME_LIBRARY)
    set(STEAMWORKS_RUNTIME_LIBRARY ${STEAMWORKS_LIBRARY})
endif()
set(STEAMWORKS_RUNTIME_LIBRARIES ${STEAMWORKS_RUNTIME_LIBRARY})
if(STEAMWORKS_WEBRTC_LIBRARY)
    list(APPEND STEAMWORKS_RUNTIME_LIBRARIES ${STEAMWORKS_WEBRTC_LIBRARY})
endif()

add_library(Steamworks::Steamworks UNKNOWN IMPORTED GLOBAL)
set_target_properties(Steamworks::Steamworks PROPERTIES
    IMPORTED_LOCATION "${STEAMWORKS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${STEAMWORKS_INCLUDE_DIR}")
