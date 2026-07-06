cmake_minimum_required(VERSION 3.21)

project(GameCQLegacyUpdate
    VERSION 1.3.8
    DESCRIPTION "GameCQ legacy DarkSpace D9 updater helper"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "GameCQLegacyUpdate is only supported on Windows.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "GameCQLegacyUpdate must be configured as a Win32/x86 build.")
endif()

set(GAMECQ_MEDUSA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../medusa-vs26" CACHE PATH "Path to the Medusa engine source tree")

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../libnet" "${CMAKE_CURRENT_BINARY_DIR}/libnet")

add_executable(GameCQLegacyUpdate WIN32
    LegacyClientUpdateTool.cpp
)

target_sources(GameCQLegacyUpdate PRIVATE GameCQLegacyUpdate.rc)

target_link_libraries(GameCQLegacyUpdate PRIVATE GameCQ::libnet)
target_include_directories(GameCQLegacyUpdate PRIVATE "${GAMECQ_MEDUSA_ROOT}")
target_compile_definitions(GameCQLegacyUpdate PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)

set_target_properties(GameCQLegacyUpdate PROPERTIES
    OUTPUT_NAME "GameCQLegacyUpdate"
)
