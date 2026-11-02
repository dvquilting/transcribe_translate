# android_overlay.cmake - build the app's engine (libpkengine.so) with CrispASR.
#
# CrispASR's CMake files assume they are the top-level project, so - as for the
# desktop's static build (transcribe_cpp/cmake/crispasr_overlay.cmake) - Gradle
# configures CrispASR itself and this file injects our library into its build:
#
#   cmake -S <CrispASR> -DCMAKE_PROJECT_crispasr_INCLUDE=<this file> ...
#
# (app/build.gradle.kts passes the options.) CMake includes this file right
# after CrispASR's project() call; pk_engine.cmake is included at the end of
# CrispASR's top-level CMakeLists.txt, when its targets exist.

get_filename_component(_pk_root "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)

# The desktop build's small CrispASR source patches (the same pinned commit).
include("${_pk_root}/transcribe_cpp/cmake/patch_crispasr.cmake")

cmake_language(EVAL CODE
    "cmake_language(DEFER CALL include [[${CMAKE_CURRENT_LIST_DIR}/pk_engine.cmake]])")
unset(_pk_root)
