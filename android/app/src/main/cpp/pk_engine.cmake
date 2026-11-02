# pk_engine.cmake - libpkengine.so: the transcription and translation engine of
# the Android app (see android_overlay.cmake), built from the same sources as
# the desktop programs.

get_filename_component(PK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../.." ABSOLUTE)  # the parakeet folder
include("${PK_ROOT}/common/common.cmake")
set(PK_TMX "${PK_ROOT}/transcribe_cpp/src")
set(PK_TL "${PK_ROOT}/translate_cpp/src")
set(PK_MINIZ "${PK_ROOT}/translate_cpp/third_party/miniz" CACHE PATH "miniz sources (for reading .docx)")
if(NOT EXISTS "${PK_MINIZ}/miniz.c")
    message(FATAL_ERROR "miniz was not found at ${PK_MINIZ} (build_apk.sh/.bat downloads it)")
endif()

# miniz: reading and writing Word documents (as in translate_cpp).
file(CONFIGURE OUTPUT ${CMAKE_BINARY_DIR}/miniz_gen/miniz_export.h CONTENT "#pragma once\n#define MINIZ_EXPORT\n")
add_library(pk_miniz STATIC ${PK_MINIZ}/miniz.c ${PK_MINIZ}/miniz_tdef.c ${PK_MINIZ}/miniz_tinfl.c ${PK_MINIZ}/miniz_zip.c)
target_include_directories(pk_miniz PUBLIC ${PK_MINIZ} ${CMAKE_BINARY_DIR}/miniz_gen)
target_compile_definitions(pk_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(pk_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The engine without the JNI glue (also linked into the host test).
add_library(pk_engine_core STATIC
    ${CMAKE_CURRENT_LIST_DIR}/engine_api.cpp
    # speech to text (transcribe_cpp)
    ${PK_TMX}/pipeline.cpp ${PK_TMX}/engine.cpp ${PK_TMX}/transcript.cpp ${PK_TMX}/audio.cpp
    ${PK_TMX}/crispasr_loader.cpp ${PK_TMX}/llm.cpp
    # translation (translate_cpp)
    ${PK_TL}/translator.cpp ${PK_TL}/formats.cpp ${PK_TL}/text.cpp ${PK_TL}/languages.cpp ${PK_TL}/models.cpp
    ${PK_TL}/llm.cpp ${PK_TL}/docx.cpp
    ${PK_COMMON_SOURCES})
target_include_directories(pk_engine_core PUBLIC ${CMAKE_CURRENT_LIST_DIR}
    PRIVATE ${PK_ROOT} ${PK_COMMON_INCLUDE_DIRS}
            ${CMAKE_SOURCE_DIR}/examples              # json.hpp
            ${CMAKE_SOURCE_DIR}/examples/talk-llama)  # llama.h (CrispASR's built-in llama.cpp)
target_compile_definitions(pk_engine_core PRIVATE
    TMX_STATIC_CRISPASR   # CrispASR is linked in, not loaded at run time
    TL_NO_GPU_FIT         # no llama.cpp "common" library: the phone runs on its processor
    TL_CRISPASR_LLAMA)    # the translator runs on CrispASR's built-in llama.cpp (an older API in places)
if(TMX_PATCH_ALIGN_FREE)
    target_compile_definitions(pk_engine_core PRIVATE TMX_HAVE_ALIGN_FREE)
endif()
set_target_properties(pk_engine_core PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_compile_options(pk_engine_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(pk_engine_core PUBLIC crispasr-lib crispasr-llama-core pk_miniz)

if(ANDROID)
    add_library(pkengine SHARED ${CMAKE_CURRENT_LIST_DIR}/jni_bridge.cpp)
    target_link_libraries(pkengine PRIVATE pk_engine_core log android)
    set_target_properties(pkengine PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # Only the JNI entry points are exported; smaller, and faster to load.
    target_link_options(pkengine PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL)
else()
    # Host test: the engine API without a phone (android/app/src/test/cpp/host_test.cpp).
    add_executable(pk_engine_host_test ${CMAKE_CURRENT_LIST_DIR}/../../test/cpp/host_test.cpp)
    target_link_libraries(pk_engine_host_test PRIVATE pk_engine_core)
    target_include_directories(pk_engine_host_test PRIVATE ${CMAKE_SOURCE_DIR}/examples)  # json.hpp
    set_target_properties(pk_engine_host_test PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    find_package(JNI QUIET)
    if(JNI_FOUND)  # compile the JNI glue too, to catch errors before an Android build
        add_library(pkengine SHARED ${CMAKE_CURRENT_LIST_DIR}/jni_bridge.cpp)
        target_include_directories(pkengine PRIVATE ${JNI_INCLUDE_DIRS})
        target_link_libraries(pkengine PRIVATE pk_engine_core)
        set_target_properties(pkengine PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    endif()
endif()
