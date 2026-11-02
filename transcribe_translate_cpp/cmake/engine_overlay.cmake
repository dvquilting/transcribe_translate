# engine_overlay.cmake - build tt_engine with CrispASR linked statically.
#
# CrispASR's CMake files assume they are the top-level project (they use
# CMAKE_SOURCE_DIR), so instead of add_subdirectory(CrispASR) the build
# configures CrispASR itself and injects our program into its build:
#
#   cmake -S third_party/CrispASR -B build-engine \
#         -DCMAKE_PROJECT_crispasr_INCLUDE=<this file> \
#         -DBUILD_SHARED_LIBS=OFF -DGGML_CUDA=ON -DGGML_VULKAN=ON ...
#
# CMake includes this file right after CrispASR's project() call; engine.cmake
# is included at the end of CrispASR's top-level CMakeLists.txt, when the
# crispasr-lib target exists. build_all.bat / build_all.sh pass the options.

# Languages can't be enabled from the deferred include, so enable the resource
# compiler (for the program icon) here, during CrispASR's project() call.
if(WIN32)
    enable_language(RC)
endif()

# Small source patches (see the file) before any target is defined.
include("${CMAKE_CURRENT_LIST_DIR}/patch_crispasr.cmake")

cmake_language(EVAL CODE
    "cmake_language(DEFER CALL include [[${CMAKE_CURRENT_LIST_DIR}/engine.cmake]])")
