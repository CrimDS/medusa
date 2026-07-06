cmake_minimum_required(VERSION 3.21)

project(GameCQ
    VERSION 1.3.8
    DESCRIPTION "GameCQ 1 ⅜ - Darkspace lobby client"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GAMECQ_MEDUSA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/medusa-vs26" CACHE PATH "Path to the Medusa engine source tree")

option(GAMECQ_BUILD_APP "Build the Qt GameCQ shell" ON)
option(GAMECQ_BUILD_LIBNET "Build the Medusa lobby networking static library" ON)
option(GAMECQ_BUILD_LEGACY_D9_HELPER "Build the x86 DarkSpace D9 legacy updater helper" ON)

include(CTest)

if(WIN32 AND NOT Vulkan_INCLUDE_DIR)
    set(GAMECQ_VULKAN_HEADERS "C:/Qt/Tools/Vulkan-Headers/include" CACHE PATH "Path to Vulkan headers for Qt WebEngine")
    if(EXISTS "${GAMECQ_VULKAN_HEADERS}/vulkan/vulkan.h")
        set(Vulkan_INCLUDE_DIR "${GAMECQ_VULKAN_HEADERS}" CACHE PATH "Path to Vulkan headers" FORCE)
    endif()
endif()

if(GAMECQ_BUILD_LIBNET)
    add_subdirectory(libnet)
endif()

if(GAMECQ_BUILD_APP)
    add_subdirectory(app)
endif()
