# crispasr_overlay.cmake - build transcribe_multi with CrispASR linked statically.
#
# CrispASR's CMake files assume they are the top-level project (they use
# CMAKE_SOURCE_DIR), so instead of add_subdirectory(CrispASR) we configure
# CrispASR itself and inject our program into its build:
#
#   cmake -S <CrispASR> -B build-static \
#         -DCMAKE_PROJECT_crispasr_INCLUDE=<this file> \
#         -DBUILD_SHARED_LIBS=OFF -DGGML_CUDA=ON -DGGML_VULKAN=ON ...
#
# CMake includes this file right after CrispASR's project() call. We defer
# including static_cli.cmake to the end of CrispASR's top-level CMakeLists.txt,
# when the crispasr-lib target exists. build_static.bat passes all the options.

# Languages can't be enabled from the deferred include, so enable the resource
# compiler (for the program icon) here, during CrispASR's project() call.
if(WIN32)
    enable_language(RC)
endif()

# Small source patches (see the file) before any target is defined.
include("${CMAKE_CURRENT_LIST_DIR}/patch_crispasr.cmake")

get_filename_component(_tmx_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
cmake_language(EVAL CODE
    "cmake_language(DEFER CALL include [[${_tmx_root}/cmake/static_cli.cmake]])")
unset(_tmx_root)
