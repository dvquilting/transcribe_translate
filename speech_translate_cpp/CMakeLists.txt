cmake_minimum_required(VERSION 3.21)
project(speech_translate LANGUAGES CXX)

# ---------------------------------------------------------------------------
# speech_translate_gui: audio/video -> translated subtitles. It runs
# transcribe_multi (../transcribe_cpp) and translate_text (../translate_cpp) as
# child processes, so it is only a Qt program; build_all.bat builds all three.
# It reuses translate_cpp's model/language tables and subtitle code, and the
# code all three share in ../common (SubRip reader, model catalog, ChildProcess).
# ---------------------------------------------------------------------------
option(SPEECH_BUILD_GUI   "Build the Qt6 GUI" ON)
option(SPEECH_BUILD_TESTS "Build the unit tests" ON)
set(TRANSLATE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../translate_cpp/src" CACHE PATH "translate_cpp/src")
if(NOT EXISTS "${TRANSLATE_SRC}/formats.cpp")
    message(FATAL_ERROR "translate_cpp not found next to this folder (set TRANSLATE_SRC)")
endif()
set(PK_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../common" CACHE PATH "The shared common/ folder next to this project")
if(NOT EXISTS "${PK_COMMON}/common.cmake")
    message(FATAL_ERROR "The shared code folder was not found at ${PK_COMMON}: common/ must sit next to "
                        "speech_translate_cpp (as in the parakeet folder), or set PK_COMMON to it.")
endif()
include("${PK_COMMON}/common.cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(SHARED_SOURCES src/subtitles.cpp
    ${TRANSLATE_SRC}/formats.cpp ${TRANSLATE_SRC}/text.cpp ${TRANSLATE_SRC}/languages.cpp ${TRANSLATE_SRC}/models.cpp
    ${TRANSLATE_SRC}/models_help.cpp
    ${PK_COMMON_SOURCES})

function(st_options target)
    target_include_directories(${target} PRIVATE src ${TRANSLATE_SRC} ${PK_COMMON_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

if(SPEECH_BUILD_TESTS)
    enable_testing()
    add_executable(speech_translate_tests tests/test_subtitles.cpp ${SHARED_SOURCES})
    st_options(speech_translate_tests)
    add_test(NAME subtitles COMMAND speech_translate_tests)
endif()

if(SPEECH_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(_src src/main.cpp src/mainwindow.cpp src/mainwindow.h src/help.cpp src/help.h ${SHARED_SOURCES}
            ${PK_COMMON_QT_SOURCES})
        if(WIN32)
            enable_language(RC)
            list(APPEND _src src/app.rc)
        endif()
        add_executable(speech_translate_gui WIN32 ${_src})
        st_options(speech_translate_gui)
        target_include_directories(speech_translate_gui PRIVATE ${PK_COMMON_QT_INCLUDE_DIRS})
        target_link_libraries(speech_translate_gui PRIVATE Qt6::Widgets)
        if(WIN32)
            get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
            get_filename_component(_qtbin "${_qmake}" DIRECTORY)
            find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}")
            if(WINDEPLOYQT)
                add_custom_command(TARGET speech_translate_gui POST_BUILD
                    COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                            "$<TARGET_FILE:speech_translate_gui>"
                    COMMENT "Copying the Qt DLLs next to speech_translate_gui.exe")
            endif()
        endif()
    else()
        message(WARNING "Qt6 Widgets not found: skipping the GUI (set CMAKE_PREFIX_PATH to your Qt kit)")
    endif()
endif()

# GUI regressions run offscreen and use temporary settings/files only.
if(SPEECH_BUILD_TESTS AND SPEECH_BUILD_GUI AND Qt6_FOUND)
    enable_testing()
    add_executable(speech_translate_cpp_gui_tests tests/test_gui.cpp src/mainwindow.h src/help.cpp ${SHARED_SOURCES}
        ${PK_COMMON_QT_SOURCES})
    target_include_directories(speech_translate_cpp_gui_tests PRIVATE src ${PK_COMMON_QT_INCLUDE_DIRS})
    target_link_libraries(speech_translate_cpp_gui_tests PRIVATE Qt6::Widgets)
    st_options(speech_translate_cpp_gui_tests)
    add_dependencies(speech_translate_cpp_gui_tests speech_translate_gui)
    get_target_property(_test_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_test_qtbin "${_test_qmake}" DIRECTORY)
    add_test(NAME gui_regressions COMMAND speech_translate_cpp_gui_tests)
    set_tests_properties(gui_regressions PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_test_qtbin}/../plugins")
endif()

# The shared code's own tests run here (the lightest of the three builds).
if(SPEECH_BUILD_TESTS)
    if(SPEECH_BUILD_GUI AND Qt6_FOUND)
        pk_add_common_tests(ON)
    else()
        pk_add_common_tests(OFF)
    endif()
endif()
