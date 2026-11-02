# engine.cmake - tt_engine with CrispASR + ggml linked statically.
# Included at the end of CrispASR's top-level CMakeLists.txt by engine_overlay.cmake.

get_filename_component(TT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
include("${TT_ROOT}/cmake/sources.cmake")

add_executable(tt_engine ${TT_ENGINE_SOURCES} ${TT_COMMON_SOURCES}
    # Compile-time check of our copied CrispASR declarations against its headers.
    ${TT_ROOT}/src/engine/crispasr_abi_check.cpp)
target_include_directories(tt_engine PRIVATE ${TT_ROOT}/src/engine ${TT_ROOT}/src/common)
set_target_properties(tt_engine PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(tt_engine PRIVATE TMX_STATIC_CRISPASR)
# Optional functions added by cmake/patch_crispasr.cmake (only when the patch applied).
if(TMX_PATCH_ALIGN_FREE)
    target_compile_definitions(tt_engine PRIVATE TMX_HAVE_ALIGN_FREE)
endif()
target_link_libraries(tt_engine PRIVATE crispasr-lib)

if(MSVC)
    target_compile_options(tt_engine PRIVATE /utf-8 /W3)
    target_compile_definitions(tt_engine PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(tt_engine PRIVATE -Wall -Wextra)
endif()
if(WIN32)
    target_link_libraries(tt_engine PRIVATE shell32)
    if(CMAKE_RC_COMPILER)
        target_sources(tt_engine PRIVATE ${TT_ROOT}/src/engine/engine.rc)
    endif()
else()
    target_link_libraries(tt_engine PRIVATE ${CMAKE_DL_LIBS})  # dlopen of the translation library
endif()
if(MINGW)
    target_link_options(tt_engine PRIVATE -static)
endif()

if(GGML_VULKAN AND MSVC)
    # Delay-loaded, with a stand-in when it is missing (src/common/vulkan_optional.cpp):
    # without the Vulkan runtime the program still starts and uses the processor.
    target_sources(tt_engine PRIVATE ${TT_ROOT}/src/common/vulkan_optional.cpp)
    target_link_options(tt_engine PRIVATE "/DELAYLOAD:vulkan-1.dll")
    target_link_libraries(tt_engine PRIVATE delayimp)
endif()

if(GGML_CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(_cublas "cublas64_${CUDAToolkit_VERSION_MAJOR}.dll")
    set(_cublaslt "cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll")
    target_compile_definitions(tt_engine PRIVATE TMX_CUBLAS_DLL="${_cublas}" TMX_CUBLASLT_DLL="${_cublaslt}")
    if(MSVC)
        # Windows has no static cuBLAS. Delay-load it so the .exe still starts when
        # the cuBLAS DLLs are absent; the program then uses Vulkan instead of CUDA.
        target_link_options(tt_engine PRIVATE "/DELAYLOAD:${_cublas}" "/DELAYLOAD:${_cublaslt}")
        target_link_libraries(tt_engine PRIVATE delayimp)
    endif()
    if(WIN32)
        find_file(TT_CUBLAS_DLL_PATH ${_cublas} HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
        find_file(TT_CUBLASLT_DLL_PATH ${_cublaslt} HINTS "${CUDAToolkit_BIN_DIR}" "${CUDAToolkit_BIN_DIR}/x64" NO_DEFAULT_PATH)
        if(TT_CUBLAS_DLL_PATH AND TT_CUBLASLT_DLL_PATH)
            add_custom_command(TARGET tt_engine POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TT_CUBLAS_DLL_PATH}" "${TT_CUBLASLT_DLL_PATH}"
                        "$<TARGET_FILE_DIR:tt_engine>"
                COMMENT "Copying cuBLAS DLLs next to tt_engine.exe")
        else()
            message(WARNING "cuBLAS DLLs not found in ${CUDAToolkit_BIN_DIR}; copy them next to the .exe by hand.")
        endif()
    endif()
endif()

message(STATUS "tt_engine: static CrispASR (CUDA=${GGML_CUDA}, Vulkan=${GGML_VULKAN})")
