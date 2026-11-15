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
    GIT_TAG        d172a12c6e3df0831a641133b96d073559a99db5
)
FetchContent_MakeAvailable(libdesktop)

# Screenshots are encoded with the shared PNG codec; zpilot carries no PNG
# implementation of its own.
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
    GIT_TAG        75e5e1dd703f095e08da8a1293aa59c9e3c4f92b
    SYSTEM
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(libfiles)

find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)
set(ZPILOT_ICON "${CMAKE_CURRENT_BINARY_DIR}/generated/zpilot.ico")
set(ZPILOT_ICON_RC "${CMAKE_CURRENT_BINARY_DIR}/generated/zpilot-icon.rc")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(TO_NATIVE_PATH "${ZPILOT_ICON}" ZPILOT_ICON_NATIVE)
string(REPLACE "\\" "\\\\" ZPILOT_ICON_NATIVE "${ZPILOT_ICON_NATIVE}")
file(GENERATE OUTPUT "${ZPILOT_ICON_RC}" CONTENT "1 ICON \"${ZPILOT_ICON_NATIVE}\"\n")
add_custom_command(OUTPUT "${ZPILOT_ICON}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_icon.py"
        --output "${ZPILOT_ICON}"
    DEPENDS tools/generate_icon.py tools/iconlib.py
    COMMENT "Generating zpilot's procedural desktop icon"
    VERBATIM)
set_source_files_properties("${ZPILOT_ICON}" PROPERTIES HEADER_FILE_ONLY TRUE)
set_source_files_properties("${ZPILOT_ICON_RC}" PROPERTIES OBJECT_DEPENDS "${ZPILOT_ICON}")

libdesktop_add_app(zpilot
    SOURCES
        "${ZPILOT_ICON}" "${ZPILOT_ICON_RC}"
        src/Main.cpp
        src/Input.cpp
        src/Renderer.cpp
        src/Geometry.cpp
        src/Field.cpp
        src/Font.cpp
        src/Synth.cpp
        src/Physics.cpp
        src/Profile.cpp
        src/Game.cpp
        src/Common.h
        src/Input.h
        src/Renderer.h
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
target_link_libraries(zpilot PRIVATE libdesktop::ui libfiles::libfiles)

if (MSVC)
    target_compile_options(zpilot PRIVATE /W4 /MP /EHsc /fp:fast)
    target_compile_definitions(zpilot PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME zpilot_icon COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_icon.py" --output "${ZPILOT_ICON}" --check)
    set_tests_properties(zpilot_icon PROPERTIES TIMEOUT 30 LABELS "zpilot;headless")

    add_executable(zpilot_tests tests/smoke.cpp tests/input.cpp src/Profile.cpp src/Input.cpp
        src/Synth.cpp)
    target_include_directories(zpilot_tests PRIVATE src)
    target_link_libraries(zpilot_tests PRIVATE libdesktop::ui)
    if(MSVC)
        target_compile_options(zpilot_tests PRIVATE /W4 /EHsc)
        target_compile_definitions(zpilot_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    add_test(NAME zpilot_unit COMMAND zpilot_tests)
    set_tests_properties(zpilot_unit PROPERTIES TIMEOUT 30 LABELS "zpilot;headless")

    add_test(NAME zpilot_capture
        COMMAND pwsh -NoProfile -File "${PROJECT_SOURCE_DIR}/tests/capture.ps1"
            -Exe "$<TARGET_FILE:zpilot>" -WorkDir "${CMAKE_CURRENT_BINARY_DIR}" -Headless)
    set_tests_properties(zpilot_capture PROPERTIES TIMEOUT 660 LABELS "zpilot;headless")
endif()
