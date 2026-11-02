# common.cmake - code shared by transcribe_cpp, translate_cpp and speech_translate_cpp
# (see README.md). A project includes this file and compiles the sources into its
# own targets; there is no library to build or install first:
#
#   include("${CMAKE_CURRENT_SOURCE_DIR}/../common/common.cmake")
#   target_sources(app PRIVATE ${PK_COMMON_SOURCES})
#   target_include_directories(app PRIVATE ${PK_COMMON_INCLUDE_DIRS})
#
# A Qt GUI also adds ${PK_COMMON_QT_SOURCES} / ${PK_COMMON_QT_INCLUDE_DIRS}
# (with AUTOMOC on, for ChildProcess and ModelsDialog; links Qt6::Widgets).

get_filename_component(PK_COMMON_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

# Plain C++17, no dependencies (no Qt, CrispASR or llama.cpp).
set(PK_COMMON_INCLUDE_DIRS ${PK_COMMON_DIR}/src)
set(PK_COMMON_SOURCES
    ${PK_COMMON_DIR}/src/utf8_text.cpp
    ${PK_COMMON_DIR}/src/srt.cpp
    ${PK_COMMON_DIR}/src/fileio.cpp
    ${PK_COMMON_DIR}/src/model_catalog.cpp
    ${PK_COMMON_DIR}/src/docx_writer.cpp
    ${PK_COMMON_DIR}/src/speakers.cpp)

# Qt 6 (Widgets programs): running the command-line tools as child processes,
# the downloaded model files (Qt Core only), and the "Model files" window listing them
# (Qt Widgets).
set(PK_COMMON_QT_INCLUDE_DIRS ${PK_COMMON_DIR}/qt)
set(PK_COMMON_QT_SOURCES
    ${PK_COMMON_DIR}/qt/child_process.cpp
    ${PK_COMMON_DIR}/qt/child_process.h
    ${PK_COMMON_DIR}/qt/process_tree.h
    ${PK_COMMON_DIR}/qt/model_files.cpp
    ${PK_COMMON_DIR}/qt/model_files.h
    ${PK_COMMON_DIR}/qt/models_dialog.cpp
    ${PK_COMMON_DIR}/qt/models_dialog.h
    ${PK_COMMON_DIR}/qt/help_window.cpp
    ${PK_COMMON_DIR}/qt/help_window.h)

# The tests of the shared code (run by one project's ctest: speech_translate_cpp).
# `with_qt`: also the ChildProcess tests (needs Qt6::Core and AUTOMOC).
function(pk_add_common_tests with_qt)
    add_executable(common_tests ${PK_COMMON_DIR}/tests/test_common.cpp ${PK_COMMON_SOURCES})
    target_include_directories(common_tests PRIVATE ${PK_COMMON_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(common_tests PRIVATE /utf-8 /W3)
        target_compile_definitions(common_tests PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(common_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME common COMMAND common_tests)
    if(with_qt)
        add_executable(common_qt_tests ${PK_COMMON_DIR}/tests/test_child_process.cpp ${PK_COMMON_DIR}/qt/child_process.cpp
            ${PK_COMMON_DIR}/qt/child_process.h ${PK_COMMON_DIR}/qt/process_tree.h)
        target_include_directories(common_qt_tests PRIVATE ${PK_COMMON_QT_INCLUDE_DIRS})
        set_target_properties(common_qt_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(common_qt_tests PRIVATE Qt6::Core)
        if(MSVC)
            target_compile_options(common_qt_tests PRIVATE /utf-8 /W3)
        else()
            target_compile_options(common_qt_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME common_child_process COMMAND common_qt_tests)
        add_executable(common_models_tests ${PK_COMMON_DIR}/tests/test_models.cpp ${PK_COMMON_QT_SOURCES})
        target_include_directories(common_models_tests PRIVATE ${PK_COMMON_QT_INCLUDE_DIRS})
        set_target_properties(common_models_tests PROPERTIES AUTOMOC ON)
        target_link_libraries(common_models_tests PRIVATE Qt6::Widgets)
        if(MSVC)
            target_compile_options(common_models_tests PRIVATE /utf-8 /W3)
        else()
            target_compile_options(common_models_tests PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME common_models COMMAND common_models_tests)
        # Offscreen, with Qt's own plugin folder: on Windows the test program sits next
        # to the GUI, where windeployqt only copied the Windows platform plugin.
        get_target_property(_pk_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_pk_qtbin "${_pk_qmake}" DIRECTORY)
        set_tests_properties(common_models PROPERTIES ENVIRONMENT
            "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_pk_qtbin}/../plugins")
    endif()
endfunction()
