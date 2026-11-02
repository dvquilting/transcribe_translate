# sources.cmake - the source lists, shared by the engine build (engine.cmake),
# the translation library (translate/CMakeLists.txt) and the window and tests
# (../CMakeLists.txt). TT_ROOT is the project folder.

# Plain C++17 shared by all parts: subtitles, files, UTF-8, Word writer, model tables, speakers.
set(TT_COMMON_SOURCES
    ${TT_ROOT}/src/common/utf8_text.cpp
    ${TT_ROOT}/src/common/srt.cpp
    ${TT_ROOT}/src/common/fileio.cpp
    ${TT_ROOT}/src/common/model_catalog.cpp
    ${TT_ROOT}/src/common/docx_writer.cpp
    ${TT_ROOT}/src/common/speakers.cpp)

# tt_engine: speech to text, speakers and dubbing (CrispASR).
set(TT_ENGINE_SOURCES
    ${TT_ROOT}/src/engine/main.cpp
    ${TT_ROOT}/src/engine/cli_main.cpp
    ${TT_ROOT}/src/engine/audio.cpp
    ${TT_ROOT}/src/engine/engine.cpp
    ${TT_ROOT}/src/engine/transcript.cpp
    ${TT_ROOT}/src/engine/crispasr_loader.cpp
    ${TT_ROOT}/src/engine/llm.cpp
    ${TT_ROOT}/src/engine/dsp.cpp
    ${TT_ROOT}/src/engine/dub.cpp
    ${TT_ROOT}/src/engine/dub_plan.cpp
    ${TT_ROOT}/src/engine/pipeline.cpp)

# Translation without llama.cpp (also in the window and the tests).
set(TT_TRANSLATE_CORE_SOURCES
    ${TT_ROOT}/src/translate/languages.cpp
    ${TT_ROOT}/src/translate/models.cpp
    ${TT_ROOT}/src/translate/text.cpp
    ${TT_ROOT}/src/translate/formats.cpp)

# The window's Qt helpers: child processes, model files, help.
set(TT_QT_SOURCES
    ${TT_ROOT}/src/qt/child_process.cpp ${TT_ROOT}/src/qt/child_process.h ${TT_ROOT}/src/qt/process_tree.h
    ${TT_ROOT}/src/qt/model_files.cpp ${TT_ROOT}/src/qt/model_files.h
    ${TT_ROOT}/src/qt/models_dialog.cpp ${TT_ROOT}/src/qt/models_dialog.h
    ${TT_ROOT}/src/qt/help_window.cpp ${TT_ROOT}/src/qt/help_window.h)
