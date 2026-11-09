cmake_minimum_required(VERSION 3.21)
project(transcribe_translate LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Transcribe & Translate: the window (transcribe_translate, Qt 6) and the unit
# tests. The other two parts have builds of their own (build_all.bat /
# build_all.sh run all three):
#
#   tt_engine       speech to text, speakers, dubbing - CrispASR's own CMake
#                   build with cmake/engine_overlay.cmake injected
#   tt_translate    the translation library (llama.cpp) - src/translate
#
# The window only runs tt_engine as a child process, so it can be built with a
# different compiler than the engine (Qt's MinGW kit on Windows).
# ---------------------------------------------------------------------------
option(TT_BUILD_GUI   "Build the Qt 6 window" ON)
option(TT_BUILD_TESTS "Build the unit tests" ON)
# For development: tt_engine without CrispASR compiled in - it loads libcrispasr.so /
# crispasr.dll from its own folder at run time (a CrispASR release, or a test stand-in).
option(TT_BUILD_ENGINE_DYNAMIC "Build a tt_engine that loads CrispASR at run time (bin/dynamic)" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(TT_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
include(cmake/sources.cmake)
set(TT_THIRD_PARTY "${TT_ROOT}/third_party" CACHE PATH "CrispASR, llama.cpp and miniz sources")

function(tt_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

if(TT_BUILD_GUI OR TT_BUILD_TESTS)
    find_package(Qt6 6.4 COMPONENTS Widgets QUIET)
    if(NOT Qt6_FOUND)
        message(WARNING "Qt6 Widgets not found: no window and no window tests (set CMAKE_PREFIX_PATH to your Qt kit)")
    endif()
endif()

# The window's own sources (also compiled into its tests).
set(TT_GUI_SOURCES
    src/gui/mainwindow.cpp src/gui/mainwindow.h
    src/gui/transcribe_page.cpp src/gui/transcribe_page.h
    src/gui/translate_page.cpp src/gui/translate_page.h
    src/gui/speech_page.cpp src/gui/speech_page.h
    src/gui/history_page.cpp src/gui/history_page.h
    src/gui/jobgate.cpp src/gui/jobgate.h
    src/gui/help.cpp src/gui/help.h
    src/gui/subtitles.cpp src/gui/subtitles.h
    src/gui/pdf_files.cpp src/gui/pdf_files.h src/gui/faster_mode.h
    ${TT_ROOT}/src/translate/models_help.cpp
    ${TT_TRANSLATE_CORE_SOURCES} ${TT_COMMON_SOURCES} ${TT_QT_SOURCES})
set(TT_GUI_INCLUDES src/gui src/qt src/common src/translate)

# PDFium (prebuilt, https://github.com/bblanchon/pdfium-binaries; build_all
# downloads it): the window loads it to read PDFs, from next to itself.
set(TT_PDFIUM_DIR "${TT_ROOT}/third_party/pdfium" CACHE PATH "PDFium binaries (pdfium-binaries layout)")
if(WIN32)
    set(TT_PDFIUM_LIB "${TT_PDFIUM_DIR}/bin/pdfium.dll")
elseif(APPLE)
    set(TT_PDFIUM_LIB "${TT_PDFIUM_DIR}/lib/libpdfium.dylib")
else()
    set(TT_PDFIUM_LIB "${TT_PDFIUM_DIR}/lib/libpdfium.so")
endif()
if(NOT EXISTS "${TT_PDFIUM_LIB}")
    message(WARNING "${TT_PDFIUM_LIB} not found: the window will not read PDFs (build_all downloads PDFium)")
endif()

if(TT_BUILD_GUI AND Qt6_FOUND)
    set(_src src/gui/main.cpp ${TT_GUI_SOURCES})
    if(WIN32)
        enable_language(RC)
        list(APPEND _src src/gui/app.rc)
    endif()
    add_executable(transcribe_translate WIN32 ${_src})
    set_target_properties(transcribe_translate PROPERTIES AUTOMOC ON)
    target_include_directories(transcribe_translate PRIVATE ${TT_GUI_INCLUDES})
    target_link_libraries(transcribe_translate PRIVATE Qt6::Widgets)
    tt_options(transcribe_translate)
    if(EXISTS "${TT_PDFIUM_LIB}")
        add_custom_command(TARGET transcribe_translate POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TT_PDFIUM_LIB}" "$<TARGET_FILE_DIR:transcribe_translate>"
            COMMENT "Copying PDFium next to transcribe_translate")
    endif()
    if(WIN32)
        get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qtbin "${_qmake}" DIRECTORY)
        find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}")
        if(WINDEPLOYQT)
            add_custom_command(TARGET transcribe_translate POST_BUILD
                COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                        "$<TARGET_FILE:transcribe_translate>"
                COMMENT "Copying the Qt DLLs next to transcribe_translate.exe")
        endif()
    endif()
endif()

if(TT_BUILD_ENGINE_DYNAMIC)
    add_executable(tt_engine ${TT_ENGINE_SOURCES} ${TT_COMMON_SOURCES})
    target_include_directories(tt_engine PRIVATE src/engine src/common)
    set_target_properties(tt_engine PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/dynamic)
    tt_options(tt_engine)
    if(WIN32)
        target_link_libraries(tt_engine PRIVATE shell32)
    else()
        target_link_libraries(tt_engine PRIVATE ${CMAKE_DL_LIBS})
    endif()
endif()

if(TT_BUILD_TESTS)
    enable_testing()
    # Test programs go into bin/tests, out of the way of the program files in bin
    # (the window's test has a stand-in tt_engine next to it there).
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

    # The shared code: subtitles, files, UTF-8, Word writer, model tables, speakers.
    add_executable(tt_common_tests tests/test_common.cpp ${TT_COMMON_SOURCES})
    target_include_directories(tt_common_tests PRIVATE src/common)
    tt_options(tt_common_tests)
    add_test(NAME common COMMAND tt_common_tests)

    # The engine's pure helpers (no CrispASR, no model).
    add_executable(tt_engine_tests tests/test_helpers.cpp src/engine/audio.cpp src/engine/transcript.cpp
        src/engine/dsp.cpp src/engine/dub_plan.cpp ${TT_COMMON_SOURCES})
    target_include_directories(tt_engine_tests PRIVATE src/engine src/common)
    tt_options(tt_engine_tests)
    add_test(NAME engine_helpers COMMAND tt_engine_tests)

    # Translation: formats and the translator with a stand-in model (llm_stub.cpp),
    # and the command's exit codes. Needs miniz (third_party, for .docx).
    if(EXISTS ${TT_THIRD_PARTY}/miniz/miniz.c)
        file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/miniz_gen/miniz_export.h CONTENT "#pragma once\n#define MINIZ_EXPORT\n")
        add_library(tt_test_miniz STATIC
            ${TT_THIRD_PARTY}/miniz/miniz.c ${TT_THIRD_PARTY}/miniz/miniz_tdef.c
            ${TT_THIRD_PARTY}/miniz/miniz_tinfl.c ${TT_THIRD_PARTY}/miniz/miniz_zip.c)
        target_include_directories(tt_test_miniz PUBLIC ${TT_THIRD_PARTY}/miniz ${CMAKE_BINARY_DIR}/miniz_gen)
        target_compile_definitions(tt_test_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

        add_executable(tt_translate_tests tests/test_formats.cpp tests/llm_stub.cpp src/translate/docx.cpp
            src/translate/translator.cpp ${TT_TRANSLATE_CORE_SOURCES} ${TT_COMMON_SOURCES})
        target_include_directories(tt_translate_tests PRIVATE src/translate src/common tests)
        target_link_libraries(tt_translate_tests PRIVATE tt_test_miniz)
        tt_options(tt_translate_tests)
        add_test(NAME translate_formats COMMAND tt_translate_tests)

        add_executable(tt_translate_contract tests/translate_contract_main.cpp src/translate/cli_main.cpp
            tests/llm_stub.cpp src/translate/download.cpp src/translate/docx.cpp src/translate/translator.cpp
            ${TT_TRANSLATE_CORE_SOURCES} ${TT_COMMON_SOURCES})
        target_include_directories(tt_translate_contract PRIVATE src/translate src/common tests)
        target_link_libraries(tt_translate_contract PRIVATE tt_test_miniz)
        if(WIN32)
            target_link_libraries(tt_translate_contract PRIVATE winhttp shell32)
        endif()
        tt_options(tt_translate_contract)
        add_test(NAME translate_partial_exit_contract COMMAND ${CMAKE_COMMAND}
            -DTOOL=$<TARGET_FILE:tt_translate_contract> -P ${TT_ROOT}/tests/test_cli_contract.cmake)
    else()
        message(STATUS "miniz not in ${TT_THIRD_PARTY}: the translation tests are skipped")
    endif()

    # Speech translation: bilingual subtitles and subtitle text.
    add_executable(tt_subtitles_tests tests/test_subtitles.cpp src/gui/subtitles.cpp
        ${TT_TRANSLATE_CORE_SOURCES} ${TT_COMMON_SOURCES})
    target_include_directories(tt_subtitles_tests PRIVATE src/gui src/translate src/common)
    tt_options(tt_subtitles_tests)
    add_test(NAME subtitles COMMAND tt_subtitles_tests)

    if(Qt6_FOUND)
        get_target_property(_test_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_test_qtbin "${_test_qmake}" DIRECTORY)
        set(_offscreen "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_test_qtbin}/../plugins;TT_PDFIUM=${TT_PDFIUM_LIB}")
        if(WIN32)
            # The offscreen platform looks for fonts in Qt's own lib/fonts, which Qt no
            # longer ships: give it Windows' fonts, or the PDFs written in the tests have
            # no text (the program itself uses the Windows platform and its fonts).
            file(TO_CMAKE_PATH "$ENV{SystemRoot}/Fonts" _winfonts)
            if(NOT EXISTS "${_winfonts}")
                set(_winfonts "C:/Windows/Fonts")
            endif()
            list(APPEND _offscreen "QT_QPA_FONTDIR=${_winfonts}")
        endif()

        add_executable(tt_child_process_tests tests/test_child_process.cpp src/qt/child_process.cpp
            src/qt/child_process.h src/qt/process_tree.h)
        target_include_directories(tt_child_process_tests PRIVATE src/qt)
        set_target_properties(tt_child_process_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(tt_child_process_tests PRIVATE Qt6::Core)
        tt_options(tt_child_process_tests)
        add_test(NAME child_process COMMAND tt_child_process_tests)

        add_executable(tt_models_tests tests/test_models.cpp ${TT_QT_SOURCES})
        target_include_directories(tt_models_tests PRIVATE src/qt)
        set_target_properties(tt_models_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(tt_models_tests PRIVATE Qt6::Widgets)
        tt_options(tt_models_tests)
        add_test(NAME model_files COMMAND tt_models_tests)
        set_tests_properties(model_files PROPERTIES ENVIRONMENT "${_offscreen}")

        # PDFs: writing, reading back (with PDFium), the Markdown the engine translates.
        add_executable(tt_pdf_tests tests/test_pdf.cpp src/gui/pdf_files.cpp)
        target_include_directories(tt_pdf_tests PRIVATE src/gui)
        target_link_libraries(tt_pdf_tests PRIVATE Qt6::Gui)
        tt_options(tt_pdf_tests)
        add_test(NAME pdf COMMAND tt_pdf_tests)
        set_tests_properties(pdf PROPERTIES ENVIRONMENT "${_offscreen}")

        # The window: tabs, job gate, history, the engine command lines (with a
        # stand-in engine, tests/fake_engine.cpp).
        # The stand-in is called tt_engine and sits next to the test, where the window looks for it.
        add_executable(tt_fake_engine tests/fake_engine.cpp)
        set_target_properties(tt_fake_engine PROPERTIES OUTPUT_NAME tt_engine)
        tt_options(tt_fake_engine)
        add_executable(tt_gui_tests tests/test_gui.cpp ${TT_GUI_SOURCES})
        set_target_properties(tt_gui_tests PROPERTIES AUTOMOC ON)
        target_include_directories(tt_gui_tests PRIVATE ${TT_GUI_INCLUDES})
        target_link_libraries(tt_gui_tests PRIVATE Qt6::Widgets)
        tt_options(tt_gui_tests)
        add_dependencies(tt_gui_tests tt_fake_engine)
        add_test(NAME gui COMMAND tt_gui_tests)
        set_tests_properties(gui PROPERTIES ENVIRONMENT "${_offscreen}")
    endif()
endif()
