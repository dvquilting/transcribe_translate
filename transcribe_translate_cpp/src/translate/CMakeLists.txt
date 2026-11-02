cmake_minimum_required(VERSION 3.21)
project(tt_translate LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# tt_translate: the translation library of Transcribe & Translate - llama.cpp
# (a newer one than CrispASR's built-in copy, with its own ggml) and the
# translator, as one shared library that tt_engine loads for "translate".
#
# Two copies of ggml in one process must not see each other: everything here
# is compiled with hidden symbols and the library exports only
# tt_translate_main(); on Linux the static libraries' symbols are also kept
# local (--exclude-libs), and tt_engine loads it with RTLD_LOCAL.
#
#   cmake -S src/translate -B build-translate -DGGML_CUDA=ON ...   (build_all.* do this)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()
# MSVC: static C runtime (/MT), as tt_engine - no Visual C++ Redistributable.
cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_CUDA_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # llama.cpp and ggml inside the library

get_filename_component(TT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
include("${TT_ROOT}/cmake/sources.cmake")
set(TT_THIRD_PARTY "${TT_ROOT}/third_party" CACHE PATH "llama.cpp and miniz sources")
foreach(dep llama.cpp/CMakeLists.txt miniz/miniz.c)
    if(NOT EXISTS ${TT_THIRD_PARTY}/${dep})
        message(FATAL_ERROR "${TT_THIRD_PARTY}/${dep} is missing (build_all.bat / build_all.sh download it).")
    endif()
endforeach()

# ---- miniz (zip, for .docx) -------------------------------------------------
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/miniz_gen/miniz_export.h CONTENT "#pragma once\n#define MINIZ_EXPORT\n")
add_library(tl_miniz STATIC
    ${TT_THIRD_PARTY}/miniz/miniz.c ${TT_THIRD_PARTY}/miniz/miniz_tdef.c
    ${TT_THIRD_PARTY}/miniz/miniz_tinfl.c ${TT_THIRD_PARTY}/miniz/miniz_zip.c)
target_include_directories(tl_miniz PUBLIC ${TT_THIRD_PARTY}/miniz ${CMAKE_BINARY_DIR}/miniz_gen)
target_compile_definitions(tl_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# ---- llama.cpp: library + common (for the GPU memory fitting), nothing else ----
set(LLAMA_BUILD_COMMON ON CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_UI OFF CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
set(LLAMA_SUBPROCESS OFF CACHE BOOL "" FORCE)
add_subdirectory(${TT_THIRD_PARTY}/llama.cpp ${CMAKE_BINARY_DIR}/llama.cpp EXCLUDE_FROM_ALL)

# ---- the library -------------------------------------------------------------
add_library(tt_translate SHARED
    cli_main.cpp llm.cpp translator.cpp download.cpp docx.cpp
    ${TT_TRANSLATE_CORE_SOURCES} ${TT_COMMON_SOURCES})
target_include_directories(tt_translate PRIVATE . ${TT_ROOT}/src/common ${TT_THIRD_PARTY}/llama.cpp/common)
target_link_libraries(tt_translate PRIVATE llama-common llama tl_miniz)
if(MSVC)
    target_compile_options(tt_translate PRIVATE /utf-8 /W3)
    target_compile_definitions(tt_translate PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(tt_translate PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(tt_translate PRIVATE winhttp shell32)
else()
    # Keep the static libraries' symbols (ggml, llama) inside: CrispASR in tt_engine
    # has its own ggml with the same names.
    target_link_options(tt_translate PRIVATE -Wl,--exclude-libs,ALL -Wl,-Bsymbolic)
endif()
if(MINGW)
    target_link_options(tt_translate PRIVATE -static)
endif()

if(GGML_VULKAN AND MSVC)
    # Delay-loaded, with a stand-in when it is missing (src/common/vulkan_optional.cpp):
    # without the Vulkan runtime the program still starts and uses the processor.
    target_sources(tt_translate PRIVATE ${TT_ROOT}/src/common/vulkan_optional.cpp)
    target_link_options(tt_translate PRIVATE "/DELAYLOAD:vulkan-1.dll")
    target_link_libraries(tt_translate PRIVATE delayimp)
endif()

if(GGML_CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(_cublas "cublas64_${CUDAToolkit_VERSION_MAJOR}.dll")
    set(_cublaslt "cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll")
    target_compile_definitions(tt_translate PRIVATE TL_CUBLAS_DLL="${_cublas}" TL_CUBLASLT_DLL="${_cublaslt}")
    if(MSVC)
        # Delay-loaded: without the cuBLAS DLLs the library still loads and uses Vulkan.
        target_link_options(tt_translate PRIVATE "/DELAYLOAD:${_cublas}" "/DELAYLOAD:${_cublaslt}")
        target_link_libraries(tt_translate PRIVATE delayimp)
    endif()
endif()
message(STATUS "tt_translate: llama.cpp compiled in (CUDA=${GGML_CUDA}, Vulkan=${GGML_VULKAN})")
