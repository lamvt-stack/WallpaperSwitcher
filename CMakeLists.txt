cmake_minimum_required(VERSION 3.20)
project(WallpaperSwitcher VERSION 1.0.0 LANGUAGES CXX)

option(WS_BUILD_APP   "Build the Windows tray application" ${WIN32})
option(WS_BUILD_TESTS "Build the portable unit tests"       ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- portable core (no Windows APIs) ---------------------------------------------------------
add_library(ws_core STATIC src/core/crop_core.cpp src/core/playlist.cpp)
target_include_directories(ws_core PUBLIC src)
if(MSVC)
    target_compile_options(ws_core PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(ws_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- tests -----------------------------------------------------------------------------------
if(WS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- application -----------------------------------------------------------------------------
if(WS_BUILD_APP)
    enable_language(RC)

    add_executable(WallpaperSwitcher WIN32
        src/main.cpp
        src/app.cpp
        src/config.cpp
        src/desktop.cpp
        src/fade.cpp
        src/image_library.cpp
        src/smart_crop.cpp
        src/wic.cpp
        src/win_util.cpp
        resources/WallpaperSwitcher.rc)

    target_link_libraries(WallpaperSwitcher PRIVATE ws_core ole32 shell32 user32 gdi32 advapi32 uuid)
    target_include_directories(WallpaperSwitcher PRIVATE src resources)
    # Always the Unicode (W) Win32 APIs; win_util.h refuses to compile otherwise.
    target_compile_definitions(WallpaperSwitcher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    set_source_files_properties(resources/WallpaperSwitcher.rc PROPERTIES OBJECT_DEPENDS
        "${CMAKE_SOURCE_DIR}/resources/WallpaperSwitcher.manifest;${CMAKE_SOURCE_DIR}/resources/icons/WallpaperSwitcher-dark.ico;${CMAKE_SOURCE_DIR}/resources/icons/WallpaperSwitcher-light.ico")

    if(MSVC)
        target_compile_options(WallpaperSwitcher PRIVATE /W4 /permissive- /utf-8 /EHsc /GS /guard:cf /sdl)
        # The manifest is embedded through the .rc file, so the linker must not generate its own.
        target_link_options(WallpaperSwitcher PRIVATE /MANIFEST:NO /DYNAMICBASE /NXCOMPAT /guard:cf)
        set_property(TARGET WallpaperSwitcher PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
        target_compile_options(WallpaperSwitcher PRIVATE -Wall -Wextra -Wpedantic)
        target_link_options(WallpaperSwitcher PRIVATE -municode -static -Wl,--dynamicbase,--nxcompat,--high-entropy-va)
        if(CMAKE_BUILD_TYPE STREQUAL "Release")
            target_compile_options(WallpaperSwitcher PRIVATE -Os)
            target_link_options(WallpaperSwitcher PRIVATE -s)
        endif()
    endif()

    install(TARGETS WallpaperSwitcher RUNTIME DESTINATION .)
endif()
