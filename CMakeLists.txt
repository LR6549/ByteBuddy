cmake_minimum_required(VERSION 4.1)
project(ByteBuddy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    message(STATUS "Running on Windows")
    list(APPEND CMAKE_MODULE_PATH "F:/Dropbox/Dropbox/CompilerUndScripts/CMake/modules")
    include(osSpecificLibs)

elseif(UNIX)
    message(STATUS "Running on Linux / Unix")
    list(APPEND CMAKE_MODULE_PATH "/home/lr6549/Dropbox/CompilerUndScripts/CMake/Modules")
    include(osSpecificLibs)
endif()

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Absoluten Pfad ins .rc file schreiben
set(ICON_PATH "${CMAKE_SOURCE_DIR}/resources/icon/icon.ico")

configure_file(
        "${CMAKE_SOURCE_DIR}/resources/icon/app.rc.in"
        "${CMAKE_BINARY_DIR}/resources/icon/app.rc"
)

add_executable(${PROJECT_NAME}
        main.cpp
        include/buddy.hpp
        include/buddy.cpp
        "${CMAKE_BINARY_DIR}/resources/icon/app.rc"
)

set_target_properties(${PROJECT_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/subLibs
        ${CMAKE_CURRENT_SOURCE_DIR}/classes
)

target_compile_definitions(${PROJECT_NAME} PRIVATE SDL_STATIC)

# ---- Link libraries ----
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            osSpecificLibs

            imgui.lib
            SDL3-static.lib
            SDL3_image-static.lib
            SDL3_mixer-static.lib
            SDL3_net-static.lib
            SDL3_ttf-static.lib
            SDL3_rtf-static.lib
            freetype.lib
            harfbuzz.lib
            bz2_static.lib

            JFLX_SDL3_AudioHandler.lib
            JFLX_SDL3_TextRenderer.lib
            JFLX_SDL3_TextureHandler.lib

            user32
            winmm
            setupapi
            cfgmgr32
            version
            imm32
            usp10
            rpcrt4
    )
elseif(UNIX)
    # Add Target Link Libraries
endif()