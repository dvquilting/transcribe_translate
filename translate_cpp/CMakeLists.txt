cmake_minimum_required(VERSION 3.21)
project(translate_text LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# translate_text: command-line translator, llama.cpp compiled in.
# translate_gui:  Qt6 front end that runs translate_text as a child process, so
#                 it can be built with a different compiler (e.g. Qt's MinGW).
# ---------------------------------------------------------------------------
option(TRANSLATE_BUILD_CLI   "Build translate_text (needs third_party/llama.cpp)" ON)
option(TRANSLATE_BUILD_GUI   "Build the Qt6 GUI" ON)
option(TRANSLATE_BUILD_TESTS "Build the unit tests (no model needed)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(TL_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party" CACHE PATH "llama.cpp and miniz sources")

# Code shared with transcribe_cpp and speech_translate_cpp (subtitles, files, GUI child processes).
set(PK_COMMON "${CMAKE_CURRENT_SOURCE_DIR}/../common" CACHE PATH "The shared common/ folder next to this project")
if(NOT EXISTS "${PK_COMMON}/common.cmake")
    message(FATAL_ERROR "The shared code folder was not found at ${PK_COMMON}: common/ must sit next to "
                        "translate_cpp (as in the parakeet folder), or set PK_COMMON to it.")
endif()
include("${PK_COMMON}/common.cmake")

function(tl_compile_options target)
    target_include_directories(${target} PRIVATE ${PK_COMMON_INCLUDE_DIRS})
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8 /W3)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---- miniz (zip, for .docx) -------------------------------------------------
set(TL_HAVE_MINIZ OFF)
if(EXISTS ${TL_THIRD_PARTY}/miniz/miniz.c)
    set(TL_HAVE_MINIZ ON)
    # (CONFIGURE writes only when the content changes: no rebuild of everything
    # that includes miniz on every configure.)
    file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/miniz_gen/miniz_export.h
         CONTENT "#pragma once\n#define MINIZ_EXPORT\n")
    add_library(tl_miniz STATIC
        ${TL_THIRD_PARTY}/miniz/miniz.c ${TL_THIRD_PARTY}/miniz/miniz_tdef.c
        ${TL_THIRD_PARTY}/miniz/miniz_tinfl.c ${TL_THIRD_PARTY}/miniz/miniz_zip.c)
    target_include_directories(tl_miniz PUBLIC ${TL_THIRD_PARTY}/miniz ${CMAKE_BINARY_DIR}/miniz_gen)
    target_compile_definitions(tl_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
    set_target_properties(tl_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# Sources with no llama.cpp dependency (shared with the tests).
set(TL_CORE_SOURCES
    src/languages.cpp src/models.cpp src/text.cpp src/formats.cpp ${PK_COMMON_SOURCES})

# ---- command-line program -----------------------------------------------------
if(TRANSLATE_BUILD_CLI)
    if(NOT EXISTS ${TL_THIRD_PARTY}/llama.cpp/CMakeLists.txt)
        message(FATAL_ERROR "third_party/llama.cpp is missing. Run build_static.bat / build_static.sh, which clone it, "
                            "or configure with -DTRANSLATE_BUILD_CLI=OFF for the GUI only.")
    endif()
    if(NOT TL_HAVE_MINIZ)
        message(FATAL_ERROR "third_party/miniz is missing (the build scripts clone it).")
    endif()
    # llama.cpp as a subproject: library + common (for the GPU memory fitting), nothing else.
    set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_UI OFF CACHE BOOL "" FORCE)
    set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
    set(LLAMA_SUBPROCESS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${TL_THIRD_PARTY}/llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp EXCLUDE_FROM_ALL)

    add_executable(translate_text
        src/cli_main.cpp src/llm.cpp src/translator.cpp src/download.cpp src/docx.cpp
        ${TL_CORE_SOURCES})
    target_include_directories(translate_text PRIVATE src ${TL_THIRD_PARTY}/llama.cpp/common)
    target_link_libraries(translate_text PRIVATE llama-common llama tl_miniz)
    tl_compile_options(translate_text)
    if(WIN32)
        target_link_libraries(translate_text PRIVATE winhttp shell32)
        enable_language(RC)
        target_sources(translate_text PRIVATE src/gui/app.rc)
    endif()

    if(GGML_VULKAN AND MSVC)
        # Delay-loaded, with a stand-in when it is missing (../common/src/vulkan_optional.cpp):
        # without the Vulkan runtime the program still starts and uses the processor.
        target_sources(translate_text PRIVATE ${PK_COMMON}/src/vulkan_optional.cpp)
        target_link_options(translate_text PRIVATE "/DELAYLOAD:vulkan-1.dll")
        target_link_libraries(translate_text PRIVATE delayimp)
    endif()

    if(GGML_CUDA)
        find_package(CUDAToolkit REQUIRED)
        set(_cublas "cublas64_${CUDAToolkit_VERSION_MAJOR}.dll")
        set(_cublaslt "cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll")
        target_compile_definitions(translate_text PRIVATE TL_CUBLAS_DLL="${_cublas}" TL_CUBLASLT_DLL="${_cublaslt}")
        if(MSVC)
            # Windows has no static cuBLAS. Delay-load it so the program still starts
            # without the DLLs; it then uses Vulkan instead of CUDA.
            target_link_options(translate_text PRIVATE "/DELAYLOAD:${_cublas}" "/DELAYLOAD:${_cublaslt}")
            target_link_libraries(translate_text PRIVATE delayimp)
        endif()
        if(WIN32)
            find_file(TL_CUBLAS_DLL_PATH ${_cublas} HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
            find_file(TL_CUBLASLT_DLL_PATH ${_cublaslt} HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
            if(TL_CUBLAS_DLL_PATH AND TL_CUBLASLT_DLL_PATH)
                add_custom_command(TARGET translate_text POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TL_CUBLAS_DLL_PATH}" "${TL_CUBLASLT_DLL_PATH}"
                            "$<TARGET_FILE_DIR:translate_text>"
                    COMMENT "Copying cuBLAS DLLs next to translate_text.exe")
            else()
                message(WARNING "cuBLAS DLLs not found in ${CUDAToolkit_BIN_DIR}; copy them next to the .exe by hand.")
            endif()
        endif()
    endif()
    message(STATUS "translate_text: llama.cpp compiled in (CUDA=${GGML_CUDA}, Vulkan=${GGML_VULKAN})")
endif()

# ---- unit tests -----------------------------------------------------------------
if(TRANSLATE_BUILD_TESTS AND TL_HAVE_MINIZ)
    enable_testing()
    # translator.cpp is tested with a stand-in for the model (tests/llm_stub.cpp).
    add_executable(translate_tests tests/test_formats.cpp tests/llm_stub.cpp src/docx.cpp
        src/translator.cpp ${TL_CORE_SOURCES})
    target_include_directories(translate_tests PRIVATE src)
    target_link_libraries(translate_tests PRIVATE tl_miniz)
    tl_compile_options(translate_tests)
    add_test(NAME formats COMMAND translate_tests)
    # Exercise the real CLI exit contract with deterministic failed generations.
    add_executable(translate_cli_contract src/cli_main.cpp tests/llm_stub.cpp src/download.cpp
        src/docx.cpp src/translator.cpp ${TL_CORE_SOURCES})
    target_include_directories(translate_cli_contract PRIVATE src)
    target_link_libraries(translate_cli_contract PRIVATE tl_miniz)
    if(WIN32)
        target_link_libraries(translate_cli_contract PRIVATE winhttp shell32)
    endif()
    tl_compile_options(translate_cli_contract)
    add_test(NAME partial_exit_contract COMMAND ${CMAKE_COMMAND}
        -DTOOL=$<TARGET_FILE:translate_cli_contract> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli_contract.cmake)
endif()

# ---- Qt6 GUI ----------------------------------------------------------------------
if(TRANSLATE_BUILD_GUI)
    find_package(Qt6 COMPONENTS Widgets QUIET)
    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)
        set(_gui_sources src/gui/main.cpp src/gui/mainwindow.cpp src/gui/mainwindow.h
            src/languages.cpp src/models.cpp src/models_help.cpp ${PK_COMMON_QT_SOURCES})
        if(WIN32)
            enable_language(RC)
            list(APPEND _gui_sources src/gui/app.rc)
        endif()
        add_executable(translate_gui WIN32 ${_gui_sources})
        target_include_directories(translate_gui PRIVATE src ${PK_COMMON_QT_INCLUDE_DIRS})
        target_link_libraries(translate_gui PRIVATE Qt6::Widgets)
        if(MSVC)
            set_property(TARGET translate_gui PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
        endif()
        tl_compile_options(translate_gui)
        if(WIN32)
            get_target_property(_qmake Qt6::qmake IMPORTED_LOCATION)
            get_filename_component(_qtbin "${_qmake}" DIRECTORY)
            find_program(WINDEPLOYQT windeployqt HINTS "${_qtbin}")
            if(WINDEPLOYQT)
                add_custom_command(TARGET translate_gui POST_BUILD
                    COMMAND "${WINDEPLOYQT}" --no-translations --no-system-d3d-compiler --no-opengl-sw
                            "$<TARGET_FILE:translate_gui>"
                    COMMENT "Copying the Qt DLLs next to translate_gui.exe")
            endif()
        endif()
    else()
        message(WARNING "Qt6 Widgets not found: skipping the GUI (set CMAKE_PREFIX_PATH to your Qt kit)")
    endif()
endif()

# GUI regressions run offscreen and use temporary settings/files only.
if(TRANSLATE_BUILD_TESTS AND TRANSLATE_BUILD_GUI AND Qt6_FOUND)
    enable_testing()
    add_executable(translate_cpp_gui_tests tests/test_gui.cpp src/gui/mainwindow.h src/languages.cpp src/models.cpp
        src/models_help.cpp ${PK_COMMON_QT_SOURCES})
    target_include_directories(translate_cpp_gui_tests PRIVATE src ${PK_COMMON_QT_INCLUDE_DIRS})
    target_link_libraries(translate_cpp_gui_tests PRIVATE Qt6::Widgets)
    tl_compile_options(translate_cpp_gui_tests)
    add_dependencies(translate_cpp_gui_tests translate_gui)
    get_target_property(_test_qmake Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_test_qtbin "${_test_qmake}" DIRECTORY)
    add_test(NAME gui_regressions COMMAND translate_cpp_gui_tests)
    set_tests_properties(gui_regressions PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${_test_qtbin}/../plugins")
endif()
