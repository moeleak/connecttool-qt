if(NOT PROJECT_ROOT)
    message(FATAL_ERROR "PROJECT_ROOT is required")
endif()

file(GLOB_RECURSE _core_sources LIST_DIRECTORIES FALSE
    "${PROJECT_ROOT}/src/domain/*.h"
    "${PROJECT_ROOT}/src/domain/*.hpp"
    "${PROJECT_ROOT}/src/domain/*.cpp"
    "${PROJECT_ROOT}/src/network/*.h"
    "${PROJECT_ROOT}/src/network/*.hpp"
    "${PROJECT_ROOT}/src/network/*.cpp")

set(_violations)
foreach(_source IN LISTS _core_sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES
       "#[ \t]*include[ \t]*[<\"](steam_api|steamnetworking|isteam|Qt|Q[A-Z])")
        list(APPEND _violations
            "${_source}: core layer imports a Steamworks or Qt header")
    endif()
    if(_contents MATCHES
       "#[ \t]*include[ \t]*[<\"][^>\"]*integrations/steam/")
        list(APPEND _violations
            "${_source}: core layer depends on the Steam adapter")
    endif()
endforeach()

file(GLOB_RECURSE _first_party_sources LIST_DIRECTORIES FALSE
    "${PROJECT_ROOT}/src/*.h"
    "${PROJECT_ROOT}/src/*.hpp"
    "${PROJECT_ROOT}/src/*.cpp"
    "${PROJECT_ROOT}/modules/*.h"
    "${PROJECT_ROOT}/modules/*.hpp"
    "${PROJECT_ROOT}/modules/*.cpp")
foreach(_source IN LISTS _first_party_sources)
    file(READ "${_source}" _contents)
    if(_contents MATCHES "#[ \t]*include[ \t]*[\"]\\.\\./")
        list(APPEND _violations
            "${_source}: use a target include root instead of ../ includes")
    endif()
endforeach()

if(_violations)
    list(JOIN _violations "\n  " _message)
    message(FATAL_ERROR "Architecture boundary violations:\n  ${_message}")
endif()
