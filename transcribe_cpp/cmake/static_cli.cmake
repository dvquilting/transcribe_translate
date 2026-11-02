# transcribe_multi with CrispASR + ggml linked statically.
# Included at the end of CrispASR's top-level CMakeLists.txt by crispasr_overlay.cmake
# (see build_static.bat).

get_filename_component(TMX_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Code shared with translate_cpp and speech_translate_cpp (../common next to transcribe_cpp).
get_filename_component(TMX_COMMON "${TMX_ROOT}/../common" ABSOLUTE)
if(NOT EXISTS "${TMX_COMMON}/common.cmake")
    message(FATAL_ERROR "The shared code folder was not found at ${TMX_COMMON}: common/ must sit next to "
                        "transcribe_cpp (as in the parakeet folder).")
endif()
include("${TMX_COMMON}/common.cmake")

add_executable(transcribe_multi
    ${TMX_ROOT}/src/cli_main.cpp
    ${TMX_ROOT}/src/audio.cpp
    ${TMX_ROOT}/src/engine.cpp
    ${TMX_ROOT}/src/transcript.cpp
    ${TMX_ROOT}/src/crispasr_loader.cpp
    ${TMX_ROOT}/src/llm.cpp
    ${TMX_ROOT}/src/dsp.cpp
    ${TMX_ROOT}/src/dub.cpp
    ${TMX_ROOT}/src/dub_plan.cpp
    ${TMX_ROOT}/src/pipeline.cpp
    # Compile-time check of our copied CrispASR declarations against its headers.
    ${TMX_ROOT}/src/crispasr_abi_check.cpp
    ${PK_COMMON_SOURCES})
target_include_directories(transcribe_multi PRIVATE ${PK_COMMON_INCLUDE_DIRS})
set_target_properties(transcribe_multi PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(transcribe_multi PRIVATE TMX_STATIC_CRISPASR)
# Optional functions added by cmake/patch_crispasr.cmake (only when the patch applied).
if(TMX_PATCH_ALIGN_FREE)
    target_compile_definitions(transcribe_multi PRIVATE TMX_HAVE_ALIGN_FREE)
endif()
target_link_libraries(transcribe_multi PRIVATE crispasr-lib)

if(MSVC)
    target_compile_options(transcribe_multi PRIVATE /utf-8 /W3)
    target_compile_definitions(transcribe_multi PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(transcribe_multi PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(transcribe_multi PRIVATE shell32)
    # icon + version info (the RC language is enabled by crispasr_overlay.cmake,
    # because enable_language() is not allowed during deferred execution)
    if(CMAKE_RC_COMPILER)
        target_sources(transcribe_multi PRIVATE ${TMX_ROOT}/src/gui/app.rc)
    endif()
endif()
if(MINGW)
    target_link_options(transcribe_multi PRIVATE -static)
endif()

if(GGML_VULKAN AND MSVC)
    # Delay-loaded, with a stand-in when it is missing (../common/src/vulkan_optional.cpp):
    # without the Vulkan runtime the program still starts and uses the processor.
    target_sources(transcribe_multi PRIVATE ${TMX_COMMON}/src/vulkan_optional.cpp)
    target_link_options(transcribe_multi PRIVATE "/DELAYLOAD:vulkan-1.dll")
    target_link_libraries(transcribe_multi PRIVATE delayimp)
endif()

if(GGML_CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(_cublas "cublas64_${CUDAToolkit_VERSION_MAJOR}.dll")
    set(_cublaslt "cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll")
    target_compile_definitions(transcribe_multi PRIVATE
        TMX_CUBLAS_DLL="${_cublas}" TMX_CUBLASLT_DLL="${_cublaslt}")
    if(MSVC)
        # Windows has no static cuBLAS. Delay-load it so the .exe still starts when
        # the cuBLAS DLLs are absent; the program then uses Vulkan instead of CUDA.
        target_link_options(transcribe_multi PRIVATE "/DELAYLOAD:${_cublas}" "/DELAYLOAD:${_cublaslt}")
        target_link_libraries(transcribe_multi PRIVATE delayimp)
    endif()
    if(WIN32)
        find_file(TMX_CUBLAS_DLL_PATH ${_cublas}
            HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
        find_file(TMX_CUBLASLT_DLL_PATH ${_cublaslt}
            HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
        if(TMX_CUBLAS_DLL_PATH AND TMX_CUBLASLT_DLL_PATH)
            add_custom_command(TARGET transcribe_multi POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TMX_CUBLAS_DLL_PATH}" "${TMX_CUBLASLT_DLL_PATH}"
                        "$<TARGET_FILE_DIR:transcribe_multi>"
                COMMENT "Copying cuBLAS DLLs next to transcribe_multi.exe")
        else()
            message(WARNING "cuBLAS DLLs not found in ${CUDAToolkit_BIN_DIR}; copy them next to the .exe by hand.")
        endif()
    endif()
endif()

message(STATUS "transcribe_multi: static CrispASR (CUDA=${GGML_CUDA}, Vulkan=${GGML_VULKAN})")
