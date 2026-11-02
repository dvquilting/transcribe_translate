// engine_api.h - what the Android app asks of the C++ engine: the model and
// language lists, a transcription, a translation. Requests and results are
// JSON (they cross JNI as strings, see jni_bridge.cpp). The same code as the
// desktop programs does the work:
//   transcription: transcribe_cpp/src/pipeline.cpp (+ llm.cpp for the corrected copy)
//   translation:   translate_cpp/src/translator.cpp, formats.cpp, docx.cpp
// Everything runs on the phone's processor. Long jobs are run by the app's
// engine service in a process of its own, which is ended to cancel a job
// that doesn't stop by itself.
#pragma once

#include <atomic>
#include <functional>
#include <string>

namespace pkm {

struct Callbacks {
    std::function<void(const std::string& line)> log;  // a line for the log
    // A step's progress: phase ("transcribe", "align", "polish", "translate", ...), done of total units.
    std::function<void(const std::string& phase, long long done, long long total, const std::string& unit)> progress;
    // The local path of a model file, downloading it first if needed (the app does
    // the download). Throws (std::runtime_error) if it can't be had.
    std::function<std::string(const std::string& file, const std::string& url, const std::string& display,
                              const std::string& size)>
        model_file;
    const std::atomic<bool>* cancel = nullptr;  // set: stop as soon as possible
};

// {"asr": [...], "aligner": {...}, "punctuation": {...}, "translation": [...],
//  "polish": {...}, "languages": [...]}: everything the app's menus and model
// screen show. Each model: key, display, file, url, size, description (+ langs,
// chunked, needs_language where they apply).
std::string catalog_json();

// Transcribe 16 kHz mono float32 audio (a raw little-endian file, made by the
// app's decoder). Request:
//   {"pcm": path, "seconds": audio length, "model": key, "language": "auto" | code,
//    "word_timing": bool, "punctuation": "auto"|"on"|"off", "polish": bool, "threads": n,
//    "out_dir": folder, "name": file name without extension, "title": document title,
//    "formats": ["txt", "srt", "docx"]}
// Result: {"ok": true, "text": transcript, "clean": corrected copy or "",
//          "files": [paths], "seconds": time taken, "polish_error": "" | message}
//      or {"ok": false, "error": message, "cancelled": bool}
std::string transcribe(const std::string& request_json, const Callbacks& cb);

// Translate a file. Request:
//   {"input": path, "format": "txt"|"md"|"srt"|"docx" (default: from the extension),
//    "model": key, "from": "auto" | code, "to": code, "context": bool, "threads": n,
//    "output": path}
// Result: {"ok": true, "output": path, "text": the translation (not for docx),
//          "problems": units kept in the original language, "seconds": time taken}
//      or {"ok": false, "error": message, "cancelled": bool}
std::string translate(const std::string& request_json, const Callbacks& cb);

// Paragraphs of `text` for display and the text/PDF outputs (as the .txt is written).
std::string paragraphs(const std::string& text);

}  // namespace pkm
