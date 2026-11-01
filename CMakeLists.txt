cmake_minimum_required(VERSION 3.21)
project(zpilot LANGUAGES CXX)

if(NOT MSVC)
    message(FATAL_ERROR "zpilot targets the Windows SDK through MSVC; no other toolchain is supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The shared platform layer is C++20. Keep the executable on the same static
# CRT so its callbacks and audio stream cross the library boundary safely.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBDESKTOP
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBDESKTOP "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop"
        CACHE PATH "A libdesktop checkout to build against")
endif()

include(FetchContent)
FetchContent_Declare(libdesktop
    GIT_REPOSITORY https://github.com/ZacWalk/libdesktop.git
    GIT_TAG        31aae322af6e0696a9597726705b1121bd31580b
)
FetchContent_MakeAvailable(libdesktop)

# Screenshots use the shared PNG codec only; zpilot keeps its HDR scene and
# post-processing backend, but does not carry a second PNG implementation.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBFILES
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libfiles/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBFILES "${CMAKE_CURRENT_SOURCE_DIR}/../libfiles"
        CACHE PATH "A libfiles checkout to build against")
endif()
set(LIBFILES_JPEG OFF)
set(LIBFILES_PNG ON)
set(LIBFILES_WEBP OFF)
set(LIBFILES_HEIF OFF)
set(LIBFILES_JXL OFF)
set(LIBFILES_RAW OFF)
set(LIBFILES_GIF OFF)
set(LIBFILES_TIFF OFF)
FetchContent_Declare(libfiles
    GIT_REPOSITORY https://github.com/ZacWalk/libfiles.git
    GIT_TAG        27de9d9fb67d7be7fcf9ff8a9f87dad5a91ae3eb
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(libfiles)

platform_add_app(zpilot
    SOURCES
        src/Main.cpp
        src/Window.cpp
        src/Renderer.cpp
        src/RendererD3D.cpp
        src/Geometry.cpp
        src/Field.cpp
        src/Font.cpp
        src/Synth.cpp
        src/Physics.cpp
        src/Profile.cpp
        src/Game.cpp
        src/Common.h
        src/Window.h
        src/Renderer.h
        src/RendererD3D.h
        src/Shaders.h
        src/Geometry.h
        src/Field.h
        src/Font.h
        src/Synth.h
        src/Physics.h
        src/Profile.h
        src/Game.h
    DESCRIPTION "zpilot"
    OUTPUT_NAME zpilot
)

target_include_directories(zpilot PRIVATE src)
target_link_libraries(zpilot PRIVATE d3d11 dxgi d3dcompiler ole32 libfiles::libfiles)

if (MSVC)
    target_compile_options(zpilot PRIVATE /W3 /MP /EHsc /fp:fast)
    target_compile_definitions(zpilot PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(zpilot_tests tests/smoke.cpp tests/window.cpp src/Profile.cpp src/Window.cpp
        src/Synth.cpp)
    target_include_directories(zpilot_tests PRIVATE src)
    target_link_libraries(zpilot_tests PRIVATE libdesktop::core)
    if(MSVC)
        target_compile_options(zpilot_tests PRIVATE /W3 /EHsc)
        target_compile_definitions(zpilot_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME zpilot_unit COMMAND zpilot_tests)
    set_tests_properties(zpilot_unit PROPERTIES TIMEOUT 30 LABELS zpilot)

    add_test(NAME zpilot_capture
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/tests/capture.ps1"
            -Exe "$<TARGET_FILE:zpilot>" -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(zpilot_capture PROPERTIES TIMEOUT 660 LABELS zpilot)
endif()
