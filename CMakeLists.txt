cmake_minimum_required(VERSION 3.21)
project(z-pilot LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(z-pilot WIN32
    src/Main.cpp
    src/Window.cpp
    src/Renderer.cpp
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
    src/Shaders.h
    src/Geometry.h
    src/Field.h
    src/Font.h
    src/Synth.h
    src/Physics.h
    src/Profile.h
    src/Game.h
)

target_include_directories(z-pilot PRIVATE src)
target_link_libraries(z-pilot PRIVATE d3d11 dxgi d3dcompiler xaudio2 ole32)

if (MSVC)
    target_compile_options(z-pilot PRIVATE /W3 /MP /fp:fast)
    target_compile_definitions(z-pilot PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(z_pilot_tests tests/smoke.cpp)
    add_test(NAME z_pilot_unit COMMAND z_pilot_tests)
    set_tests_properties(z_pilot_unit PROPERTIES TIMEOUT 30 LABELS z-pilot)

    add_test(NAME z_pilot_capture
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/tests/capture.ps1"
            -Exe "$<TARGET_FILE:z-pilot>" -WorkDir "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(z_pilot_capture PROPERTIES TIMEOUT 660 LABELS z-pilot)
endif()
