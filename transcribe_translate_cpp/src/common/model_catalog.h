// model_catalog.h - the speech-recognition and voice models of transcribe_multi,
// as data only. transcribe_multi (engine.cpp, dub.cpp) downloads and runs them;
// Speech Translate offers them in its menus. One table, so the menus can't
// drift from what the command-line program accepts.
#pragma once

#include <string>
#include <vector>

namespace pk {

// A speech-recognition (or helper) model of transcribe_multi.
struct AsrModel {
    std::string key;          // command-line name (transcribe_multi -m KEY)
    std::string display;      // human-readable name
    std::string backend;      // CrispASR backend name
    std::string file;         // GGUF file name in the model cache
    std::string url;          // download URL
    std::string size;         // approximate download size ("2.5 GB")
    bool chunked = false;     // true = LLM-style decoder fed <= 30 s chunks by transcribe_multi
    double max_chunk_seconds = 0.0;
    bool needs_language = false;  // can't detect the spoken language: it must be given
    std::string description;  // one line for a model menu
};

// The models of "transcribe_multi -m", in the order the program lists them.
const std::vector<AsrModel>& asr_models();
const AsrModel* find_asr_model(const std::string& key);
// The speech models of the Android app: small, 4-bit, for a phone's processor
// (android/). Same fields; the helpers below are shared except the aligner.
const std::vector<AsrModel>& phone_asr_models();
const AsrModel* find_phone_asr_model(const std::string& key);
const AsrModel& phone_aligner_model();  // the word-timing model, 4-bit
// Helpers transcribe_multi loads itself: word timing and punctuation restoration.
const AsrModel& aligner_model();
const AsrModel& punctuation_model();  // PCS: punctuation + capitals + sentence breaks

// A voice (text-to-speech) model of "transcribe_multi dub --tts".
struct TtsModel {
    std::string key;                 // command-line name
    std::string display;             // human-readable name
    std::string backend;             // CrispASR backend / registry key
    std::string size;                // approximate download
    std::vector<std::string> langs;  // output languages (empty = any)
    std::string description;         // one line for a model menu
};

const std::vector<TtsModel>& tts_models();
const TtsModel* find_tts_model(const std::string& key);

}  // namespace pk
