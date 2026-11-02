cmake_minimum_required(VERSION 3.24)

# MSVC: static C runtime (/MT), so no Visual C++ Redistributable is needed.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(transcribe_multi VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(TRANSCRIBE_BUILD_CLI "Build the command-line program (transcribe_multi)" ON)
option(TRANSCRIBE_BUILD_GUI "Build the Qt6 GUI (transcribe_multi_gui)" ON)

# ---------------------------------------------------------------------------
# This file builds the GUI and the *dynamic* command-line program, which loads
# crispasr.dll at run time (CRISPASR_DIR = unzipped CrispASR release; MinGW or MSVC).
#
# The *static* command-line program (CrispASR compiled into the .exe) is built
# with CrispASR as the top-level project; see cmake/crispasr_overlay.cmake and
# build_static.bat.
# ---------------------------------------------------------------------------
set(CRISPASR_DIR "" CACHE PATH "Folder containing crispasr.dll (unzipped CrispASR release)")

# Code shared with translate_cpp and speech_translate_cpp (subtitles, files, model table, GUI child processes).
set(PK_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../common" CACHE PATH "The shared common/ folder next to this project")
if(NOT EXISTS "${PK_COMMON}/common.cmake")
    message(FATAL_ERROR "The shared code folder was not found at ${PK_COMMON}: common/ must sit next to "
                        "transcribe_cpp (as in the parakeet folder), or set PK_COMMON to it.")
endif()
include("${PK_COMMON}/common.cmake")

function(tmx_set_compile_options target)
    target_include_directories(${target} PRIVATE ${PK_COMMON_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
    if(MINGW)
        target_link_options(${target} PRIVATE -static)  # no libstdc++/libgcc DLLs needed
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Command-line program (does the actual transcription)
# ---------------------------------------------------------------------------
if(TRANSCRIBE_BUILD_CLI)
    add_executable(transcribe_multi
        src/cli_main.cpp
        src/audio.cpp
        src/engine.cpp
        src/transcript.cpp
        src/crispasr_loader.cpp
        src/llm.cpp
        src/dsp.cpp
        src/dub.cpp
        src/dub_plan.cpp
        src/pipeline.cpp
        ${PK_COMMON_SOURCES})
    tmx_set_compile_options(transcribe_multi)
    if(WIN32)
        target_link_libraries(transcribe_multi PRIVATE shell32)
    endif()

    if(NOT WIN32)
        target_link_libraries(transcribe_multi PRIVATE ${CMAKE_DL_LIBS})
    endif()
    if(CRISPASR_DIR)
        if(WIN32)
            file(GLOB CRISPASR_DLLS "${CRISPASR_DIR}/*.dll")
        else()
            # Release tarball (flat or lib/) or a CrispASR build directory (src/, ggml/src/)
            file(GLOB CRISPASR_DLLS "${CRISPASR_DIR}/*.so*" "${CRISPASR_DIR}/lib/*.so*"
                                    "${CRISPASR_DIR}/src/*.so*" "${CRISPASR_DIR}/ggml/src/*.so*")
        endif()
        if(NOT CRISPASR_DLLS)
            message(FATAL_ERROR "No CrispASR libraries found in CRISPASR_DIR=${CRISPASR_DIR}")
        endif()
        add_custom_command(TARGET transcribe_multi POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CRISPASR_DLLS} "$<TARGET_FILE_DIR:transcribe_multi>"
            COMMENT "Copying CrispASR runtime libraries next to transcribe_multi"
            COMMAND_EXPAND_LISTS)
        message(STATUS "CrispASR: run-time loaded from ${CRISPASR_DIR}")
    else()
        message(WARNING "CRISPASR_DIR is not set: put crispasr.dll and its DLLs into ${CMAKE_BINARY_DIR}/bin yourself.")
    endif()

    # Unit tests for the pure helpers (no model needed): ctest
    enable_testing()
    add_executable(transcribe_tests tests/test_helpers.cpp src/audio.cpp src/transcript.cpp src/dsp.cpp
        src/dub_plan.cpp ${PK_COMMON_SOURCES})
    target_include_directories(transcribe_tests PRIVATE src)
    tmx_set_compile_options(transcribe_tests)
    add_test(NAME helpers COMMAND transcribe_tests)
endif()

# ---------------------------------------------------------------------------
# Qt6 GUI (launches transcribe_multi as a child process, so it can be built
# with a different compiler than the CLI, e.g. Qt's MinGW kit)
# ---------------------------------------------------------------------------
if(TRANSCRIBE_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()

    qt_add_executable(transcribe_multi_gui WIN32
        src/gui/main.cpp
        src/gui/mainwindow.cpp
        src/gui/mainwindow.h
        ${PK_COMMON_DIR}/src/model_catalog.cpp  # the models' languages, for the Help window
        ${PK_COMMON_QT_SOURCES})
    target_include_directories(transcribe_multi_gui PRIVATE ${PK_COMMON_INCLUDE_DIRS} ${PK_COMMON_QT_INCLUDE_DIRS})
    if(WIN32)
        enable_language(RC)
        target_sources(transcribe_multi_gui PRIVATE src/gui/app.rc)  # icon + version info
    endif()
    target_link_libraries(transcribe_multi_gui PRIVATE Qt6::Widgets)
    # Qt's MSVC DLLs use the DLL C runtime; the GUI must match them.
    set_property(TARGET transcribe_multi_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if(MSVC)
        target_compile_options(transcribe_multi_gui PRIVATE /utf-8)
    endif()
    if(TARGET transcribe_multi)
        add_dependencies(transcribe_multi_gui transcribe_multi)
    endif()

    # Copy the Qt DLLs/plugins (and, for MinGW, its runtime DLLs) next to the GUI.
    if(WIN32)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
        find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
        if(WINDEPLOYQT)
            add_custom_command(TARGET transcribe_multi_gui POST_BUILD
                COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:transcribe_multi_gui>"
                COMMENT "Running windeployqt")
        endif()
    endif()
endif()

# GUI regressions run offscreen and use temporary settings/files only.
if(TRANSCRIBE_BUILD_CLI AND TRANSCRIBE_BUILD_GUI)
    enable_testing()
    add_executable(transcribe_cpp_gui_tests tests/test_gui.cpp src/gui/mainwindow.h ${PK_COMMON_DIR}/src/model_catalog.cpp
        ${PK_COMMON_QT_SOURCES})
    target_include_directories(transcribe_cpp_gui_tests PRIVATE src ${PK_COMMON_INCLUDE_DIRS} ${PK_COMMON_QT_INCLUDE_DIRS})
    target_link_libraries(transcribe_cpp_gui_tests PRIVATE Qt6::Widgets)
    tmx_set_compile_options(transcribe_cpp_gui_tests)
    add_dependencies(transcribe_cpp_gui_tests transcribe_multi_gui)
    get_target_property(_test_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_test_qtbin "${_test_qmake}" DIRECTORY)
    add_test(NAME gui_regressions COMMAND transcribe_cpp_gui_tests)
    set_tests_properties(gui_regressions PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_test_qtbin}/../plugins")
endif()
