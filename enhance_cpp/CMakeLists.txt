cmake_minimum_required(VERSION 3.24)

# MSVC: static C runtime (/MT), so no Visual C++ Redistributable is needed.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(enhance_audio VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(ENHANCE_BUILD_CLI "Build the command-line program (enhance_audio), needs ONNX Runtime" ON)
option(ENHANCE_BUILD_GUI "Build the Qt6 GUI (enhance_audio_gui)" ON)

# ---------------------------------------------------------------------------
# Shared code: ../common (file writing, UTF-8 paths, the GUI's child process) and
# transcribe_cpp's ffmpeg decoding (../transcribe_cpp/src/audio.cpp). Both folders
# sit next to this one, as in the parakeet folder.
# ---------------------------------------------------------------------------
set(PK_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../common" CACHE PATH "The shared common/ folder next to this project")
set(TMX_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../transcribe_cpp/src" CACHE PATH "transcribe_cpp's src/ folder (audio.cpp)")
if(NOT EXISTS "${PK_COMMON}/common.cmake")
    message(FATAL_ERROR "The shared code folder was not found at ${PK_COMMON}: common/ must sit next to "
                        "enhance_cpp (as in the parakeet folder), or set PK_COMMON to it.")
endif()
if(NOT EXISTS "${TMX_SRC}/audio.cpp")
    message(FATAL_ERROR "transcribe_cpp/src/audio.cpp was not found at ${TMX_SRC}: transcribe_cpp must sit next "
                        "to enhance_cpp, or set TMX_SRC to its src folder.")
endif()
include("${PK_COMMON}/common.cmake")

set(THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

function(enh_set_compile_options target)
    # This project's own headers first: transcribe_cpp/src has a dsp.h too.
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/engine ${PK_COMMON_INCLUDE_DIRS}
                               ${TMX_SRC} ${THIRD_PARTY})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3 /EHsc)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Command-line program (does the actual processing) and its tests
# ---------------------------------------------------------------------------
if(ENHANCE_BUILD_CLI)
    set(ORT_DIR "${THIRD_PARTY}/onnxruntime-dml" CACHE PATH "Unpacked Microsoft.ML.OnnxRuntime.DirectML NuGet package")
    set(DML_DIR "${THIRD_PARTY}/directml" CACHE PATH "Unpacked Microsoft.AI.DirectML NuGet package")
    set(ORT_BIN "${ORT_DIR}/runtimes/win-x64/native")
    if(NOT EXISTS "${ORT_BIN}/onnxruntime.lib" OR NOT EXISTS "${DML_DIR}/bin/x64-win/DirectML.dll"
       OR NOT EXISTS "${THIRD_PARTY}/pocketfft_hdronly.hpp")
        message(FATAL_ERROR "ONNX Runtime, DirectML or pocketfft is missing in ${THIRD_PARTY}: run build_all.bat, "
                            "which downloads them.")
    endif()

    set(ENGINE_SOURCES src/engine/dsp.cpp src/engine/models.cpp src/engine/enhancer.cpp src/engine/monitor.cpp
        ${TMX_SRC}/audio.cpp ${PK_COMMON_DIR}/src/fileio.cpp)

    add_executable(enhance_audio src/engine/cli_main.cpp ${ENGINE_SOURCES} src/engine/engine.rc)
    enh_set_compile_options(enhance_audio)
    target_include_directories(enhance_audio PRIVATE "${ORT_DIR}/build/native/include" "${DML_DIR}/include")
    target_link_libraries(enhance_audio PRIVATE "${ORT_BIN}/onnxruntime.lib" d3d12 dxgi shell32 winmm)

    # ONNX Runtime and the DirectML redistributable next to the program (Windows'
    # own DirectML.dll is too old for this ONNX Runtime).
    add_custom_command(TARGET enhance_audio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_BIN}/onnxruntime.dll"
                "${ORT_BIN}/onnxruntime_providers_shared.dll" "${DML_DIR}/bin/x64-win/DirectML.dll"
                "$<TARGET_FILE_DIR:enhance_audio>"
        COMMENT "Copying ONNX Runtime and DirectML next to enhance_audio")

    # Unit tests of the signal processing (no models needed): ctest
    enable_testing()
    add_executable(enhance_tests tests/test_dsp.cpp src/engine/dsp.cpp ${PK_COMMON_DIR}/src/fileio.cpp)
    enh_set_compile_options(enhance_tests)
    add_test(NAME dsp COMMAND enhance_tests "${CMAKE_CURRENT_SOURCE_DIR}/test_data")
endif()

# ---------------------------------------------------------------------------
# Qt6 GUI (runs enhance_audio as a child process, so it can be built with a
# different compiler than the CLI, e.g. Qt's MinGW kit)
# ---------------------------------------------------------------------------
if(ENHANCE_BUILD_GUI)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets Multimedia)
    qt_standard_project_setup()

    qt_add_executable(enhance_audio_gui WIN32
        src/gui/main.cpp
        src/gui/mainwindow.cpp
        src/gui/mainwindow.h
        ${PK_COMMON_DIR}/qt/child_process.cpp ${PK_COMMON_DIR}/qt/child_process.h ${PK_COMMON_DIR}/qt/process_tree.h
        ${PK_COMMON_DIR}/qt/help_window.cpp ${PK_COMMON_DIR}/qt/help_window.h)
    target_include_directories(enhance_audio_gui PRIVATE ${PK_COMMON_INCLUDE_DIRS} ${PK_COMMON_QT_INCLUDE_DIRS})
    if(WIN32)
        enable_language(RC)
        target_sources(enhance_audio_gui PRIVATE src/gui/app.rc)  # icon + version info
    endif()
    target_link_libraries(enhance_audio_gui PRIVATE Qt6::Widgets Qt6::Multimedia)
    set_property(TARGET enhance_audio_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    if(MSVC)
        target_compile_options(enhance_audio_gui PRIVATE /utf-8)
    else()
        target_compile_options(enhance_audio_gui PRIVATE -Wall -Wextra)
    endif()
    if(MINGW)
        target_link_options(enhance_audio_gui PRIVATE -static-libgcc -static-libstdc++)
    endif()

    if(WIN32)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin "${_qmake}" DIRECTORY)
        find_program(WINDEPLOYQT windeployqt HINTS "${_qt_bin}")
        if(WINDEPLOYQT)
            add_custom_command(TARGET enhance_audio_gui POST_BUILD
                COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:enhance_audio_gui>"
                COMMENT "Running windeployqt")
        endif()
    endif()
endif()
