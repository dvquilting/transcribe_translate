// crispasr_api.h - the subset of the CrispASR C ABI used by this program.
//
// Two build modes share this table of function pointers:
//  * static  (TMX_STATIC_CRISPASR): CrispASR is compiled into the .exe and the
//    table points straight at the linked functions;
//  * dynamic: crispasr.dll (libcrispasr.so) is loaded at run time, so no import
//    library or headers are needed and any compiler (MSVC or MinGW) works.
// The signatures are copied from CrispASR's include/crispasr_session.h,
// crispasr.h and crispasr_chat.h. The static build checks them against those
// headers at compile time (crispasr_abi_check.cpp), so a CrispASR change that
// breaks one fails the build instead of misbehaving at run time.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>

extern "C" {
typedef struct crispasr_session crispasr_session;
typedef struct crispasr_session_result crispasr_session_result;
typedef struct crispasr_align_result crispasr_align_result;

// Mirrors the library's crispasr_open_params_v1 (abi_version 2 layout).
typedef struct crispasr_open_params_v1 {
    int abi_version;   // = 2
    int n_threads;
    int use_gpu;       // 0 = CPU only, non-zero = GPU when available
    int verbosity;     // 0 = silent
    int flash_attn;    // v2: non-zero = on
    int n_gpu_layers;  // v2: -1 = all
    int reserved[6];
} crispasr_open_params_v1;

typedef void (*crispasr_progress_callback)(int processed, int total, void* user_data);

// ---- speaker detection (crispasr_session.h declares these structs without
// their fields; the layout is CrispASR's documented, append-only ABI, checked
// by its own static_asserts against these same sizes) ----
typedef struct crispasr_diarize_seg_abi {
    int64_t t0_cs, t1_cs;  // in: centiseconds
    int32_t speaker;       // out: -1 if unassigned
    int32_t _pad;
} crispasr_diarize_seg_abi;
typedef struct crispasr_diarize_turn_abi {
    int64_t t0_cs, t1_cs;
    int32_t speaker;  // dense, zero-based
    int32_t _pad;
} crispasr_diarize_turn_abi;
typedef struct crispasr_diarize_opts_abi {
    int32_t method;  // 4 = FoxNose (WeSpeaker embeddings + clustering)
    int32_t n_threads;
    int64_t slice_t0_cs;
    const char* pyannote_model_path;    // methods 3 and 5
    const char* foxnose_embedder_path;  // method 4
    int32_t min_speakers;               // 0 -> 1
    int32_t max_speakers;               // 0 -> 8
    int32_t num_speakers;               // > 0 pins the count
    int32_t _pad2;
} crispasr_diarize_opts_abi;
typedef int (*crispasr_diarize_segments_turns_fn)(const float* left_pcm, const float* right_pcm, int32_t n_samples,
                                                  int32_t is_stereo, crispasr_diarize_seg_abi* segs, int32_t n_segs,
                                                  const crispasr_diarize_opts_abi* opts,
                                                  crispasr_diarize_turn_abi* out_turns, int32_t n_turns_cap,
                                                  int32_t* out_n_turns);

// ---- text LLM (crispasr_chat.h): mirrors of the library's POD structs ----
// (in their own file, so the static build can compare them with the real ones:
// see crispasr_abi_check.cpp)
#ifndef TMX_ABI_CHECK_REAL_HEADERS
#include "crispasr_chat_mirror.h"
#endif
}

// X(return type, name, (argument list))
#define CRISPASR_FUNCTIONS(X)                                                                                        \
    /* Preferred GPU backend ("cuda", "vulkan", ...). Call before opening a session. */                              \
    X(void, crispasr_set_gpu_backend, (const char* name))                                                            \
    X(crispasr_session*, crispasr_session_open_with_params,                                                          \
      (const char* model_path, const char* backend_name, const crispasr_open_params_v1* params))                     \
    X(void, crispasr_session_close, (crispasr_session * s))                                                          \
    X(void, crispasr_session_set_progress_callback,                                                                  \
      (crispasr_session * s, crispasr_progress_callback cb, void* user_data))                                        \
    X(crispasr_session_result*, crispasr_session_transcribe_lang,                                                    \
      (crispasr_session * s, const float* pcm, int n_samples, const char* language))                                \
    X(crispasr_session_result*, crispasr_session_transcribe_chunked_lang,                                            \
      (crispasr_session * s, const float* pcm, int n_samples, int chunk_seconds, int overlap_seconds,                \
       const char* language))                                                                                        \
    X(int, crispasr_session_result_n_segments, (crispasr_session_result * r))                                        \
    X(const char*, crispasr_session_result_segment_text, (crispasr_session_result * r, int i))                       \
    X(int64_t, crispasr_session_result_segment_t0, (crispasr_session_result * r, int i)) /* centiseconds */          \
    X(int64_t, crispasr_session_result_segment_t1, (crispasr_session_result * r, int i))                             \
    X(int, crispasr_session_result_n_words, (crispasr_session_result * r, int i_seg))                                \
    X(const char*, crispasr_session_result_word_text, (crispasr_session_result * r, int i_seg, int i_word))          \
    X(int64_t, crispasr_session_result_word_t0, (crispasr_session_result * r, int i_seg, int i_word))                \
    X(int64_t, crispasr_session_result_word_t1, (crispasr_session_result * r, int i_seg, int i_word))                \
    X(void, crispasr_session_result_free, (crispasr_session_result * r))                                             \
    X(int, crispasr_session_set_ask, (crispasr_session * s, const char* prompt))                                             \
    /* Forced alignment. A model path containing "forced-aligner" selects Qwen3-ForcedAligner. */                     \
    X(crispasr_align_result*, crispasr_align_words_abi,                                                              \
      (const char* aligner_model, const char* transcript, const float* samples, int32_t n_samples,                  \
       int64_t t_offset_cs, int32_t n_threads))                                                                      \
    X(int, crispasr_align_result_n_words, (crispasr_align_result * r))                                               \
    X(const char*, crispasr_align_result_word_text, (crispasr_align_result * r, int i))                              \
    X(int64_t, crispasr_align_result_word_t0, (crispasr_align_result * r, int i))                                    \
    X(int64_t, crispasr_align_result_word_t1, (crispasr_align_result * r, int i))                                    \
    X(void, crispasr_align_result_free, (crispasr_align_result * r))                                                 \
    /* Download into the model cache (default %USERPROFILE%\.cache\crispasr) unless already there. */                \
    X(int, crispasr_cache_ensure_file_abi,                                                                           \
      (const char* filename, const char* url, int32_t quiet, const char* cache_dir_override, char* out_buf,         \
       int32_t out_cap))                                                                                             \
    /* Built-in decoder (WAV/MP3/FLAC/OGG/Opus/AAC/M4A...) -> 16 kHz mono; used when ffmpeg is absent. */           \
    X(int, crispasr_audio_load, (const char* path, float** out_pcm, int* out_samples, int* out_sample_rate))       \
    X(void, crispasr_audio_free, (float* pcm))                                                                      \
    /* ---- text to speech (dubbing) ---- */                                                                          \
    X(int, crispasr_session_set_voice_samples,                                                                       \
      (crispasr_session * s, const float* pcm, int32_t n_samples, int32_t sample_rate, const char* ref_text_or_null)) \
    X(int, crispasr_session_set_voice, (crispasr_session * s, const char* path, const char* ref_text_or_null))       \
    X(int, crispasr_session_set_codec_path, (crispasr_session * s, const char* path))                               \
    X(float*, crispasr_session_synthesize, (crispasr_session * s, const char* text, int* out_n_samples))            \
    X(void, crispasr_pcm_free, (float* pcm))                                                                        \
    X(int, crispasr_session_output_sample_rate, (crispasr_session * s))                                             \
    X(int, crispasr_session_set_target_language, (crispasr_session * s, const char* lang))                          \
    X(int, crispasr_session_set_tts_reference_language, (crispasr_session * s, const char* lang))                   \
    X(int, crispasr_session_set_tts_seed, (crispasr_session * s, uint64_t seed))                                    \
    X(float*, crispasr_session_get_disclaimer_pcm, (crispasr_session * s, int* out_n_samples))                      \
    X(const char*, crispasr_session_disclaimer_text, (void))                                                        \
    X(int, crispasr_session_accept_marking_responsibility, (crispasr_session * s, const char* attestation))         \
    X(const char*, crispasr_session_last_synth_error, (crispasr_session * s))                                       \
    /* ---- source separation (keep music/effects under the dub) ---- */                                            \
    X(int, crispasr_session_separate, (crispasr_session * s, const float* pcm_stereo, int n_samples))               \
    X(int, crispasr_session_separate_n_stems, (crispasr_session * s))                                               \
    X(const char*, crispasr_session_separate_stem_name, (crispasr_session * s, int stem_idx))                       \
    X(const float*, crispasr_session_separate_stem, (crispasr_session * s, int stem_idx, int* out_n_samples))       \
    X(int, crispasr_session_separate_sample_rate, (crispasr_session * s))                                           \
    /* ---- model registry: the files a backend needs ---- */                                                       \
    X(int, crispasr_registry_default_bundle_info_abi,                                                                \
      (const char* backend, char* out_backend, int32_t backend_cap, char* out_license, int32_t license_cap,         \
       int32_t* out_requires_acceptance))                                                                           \
    X(int, crispasr_registry_default_bundle_artifact_abi,                                                            \
      (const char* backend, int32_t index, int32_t* out_kind, char* out_filename, int32_t filename_cap,             \
       char* out_url, int32_t url_cap, char* out_size, int32_t size_cap))                                           \
    /* Text LLM chat ABI (vendored llama.cpp inside CrispASR). */                                                    \
    X(void, crispasr_chat_open_params_default, (crispasr_chat_open_params * out))                                    \
    X(void, crispasr_chat_generate_params_default, (crispasr_chat_generate_params * out))                            \
    X(crispasr_chat_session*, crispasr_chat_open,                                                                    \
      (const char* model_path, const crispasr_chat_open_params* params, crispasr_chat_error* err))                  \
    X(void, crispasr_chat_close, (crispasr_chat_session * s))                                                        \
    X(int32_t, crispasr_chat_reset, (crispasr_chat_session * s, crispasr_chat_error* err))                           \
    X(char*, crispasr_chat_generate,                                                                                 \
      (crispasr_chat_session * s, const crispasr_chat_message* messages, size_t n_messages,                          \
       const crispasr_chat_generate_params* params, crispasr_chat_error* err))                                       \
    X(int32_t, crispasr_chat_count_tokens,                                                                           \
      (crispasr_chat_session * s, const crispasr_chat_message* messages, size_t n_messages, crispasr_chat_error* err)) \
    X(int32_t, crispasr_chat_n_ctx, (crispasr_chat_session * s))                                                     \
    X(void, crispasr_chat_string_free, (char* s))                                                                    \
    /* standalone punctuation restoration (PCS) */                                                                   \
    X(void*, crispasr_punc_init, (const char* model))                                                               \
    X(const char*, crispasr_punc_process, (void* ctx, const char* text))                                            \
    X(void, crispasr_punc_free_text, (const char* text))                                                            \
    X(void, crispasr_punc_free, (void* ctx))

// ggml device registry, used only to report which compute engine is active.
// (The device-type enum is returned as int: 0 CPU, 1 GPU, 2 integrated GPU, 3 accelerator.)
extern "C" {
typedef struct ggml_backend_device* ggml_backend_dev_t;
typedef struct ggml_backend_reg* ggml_backend_reg_t;
}
#define GGML_FUNCTIONS(X)                                                                                           \
    X(size_t, ggml_backend_dev_count, (void))                                                                        \
    X(ggml_backend_dev_t, ggml_backend_dev_get, (size_t index))                                                      \
    X(const char*, ggml_backend_dev_name, (ggml_backend_dev_t device))                                               \
    X(const char*, ggml_backend_dev_description, (ggml_backend_dev_t device))                                        \
    X(void, ggml_backend_dev_memory, (ggml_backend_dev_t device, size_t * free, size_t * total))                     \
    X(int, ggml_backend_dev_type, (ggml_backend_dev_t device))                                                       \
    X(ggml_backend_reg_t, ggml_backend_dev_backend_reg, (ggml_backend_dev_t device))                                 \
    X(const char*, ggml_backend_reg_name, (ggml_backend_reg_t reg))

#if defined(TMX_STATIC_CRISPASR) && !defined(TMX_ABI_CHECK_REAL_HEADERS)
#ifdef TMX_HAVE_ALIGN_FREE
extern "C" void crispasr_align_free_cache_abi(void);
#endif
extern "C" int crispasr_diarize_segments_turns_abi(const float* left_pcm, const float* right_pcm, int32_t n_samples,
                                                   int32_t is_stereo, crispasr_diarize_seg_abi* segs, int32_t n_segs,
                                                   const crispasr_diarize_opts_abi* opts,
                                                   crispasr_diarize_turn_abi* out_turns, int32_t n_turns_cap,
                                                   int32_t* out_n_turns);
extern "C" {
#define TMX_CRISPASR_DECL(ret, name, args) ret name args;
CRISPASR_FUNCTIONS(TMX_CRISPASR_DECL)
GGML_FUNCTIONS(TMX_CRISPASR_DECL)
#undef TMX_CRISPASR_DECL
}
#endif

namespace tmx {

struct CrispApi {
#define TMX_CRISPASR_PTR(ret, name, args) ret(*name) args = nullptr;
    CRISPASR_FUNCTIONS(TMX_CRISPASR_PTR)
#undef TMX_CRISPASR_PTR
};

// The CrispASR functions. Dynamic builds load crispasr.dll on the first call
// (next to the executable, then %CRISPASR_DIR%, then PATH) and throw
// std::runtime_error with a helpful message on failure.
const CrispApi& crispasr();

struct GgmlApi {
#define TMX_GGML_PTR(ret, name, args) ret(*name) args = nullptr;
    GGML_FUNCTIONS(TMX_GGML_PTR)
#undef TMX_GGML_PTR
};

// Optional CrispASR functions that only some builds have (added by the source
// patches in cmake/patch_crispasr.cmake, or by newer CrispASR releases). Each
// pointer is nullptr when the function isn't available.
struct CrispOptionalApi {
    void (*crispasr_align_free_cache_abi)(void) = nullptr;  // free the cached forced aligner
    // Speaker detection with the speaker turns (CrispASR 0.8.30 and later).
    crispasr_diarize_segments_turns_fn crispasr_diarize_segments_turns_abi = nullptr;
};
const CrispOptionalApi& crispasr_optional();

// The ggml device-registry functions, or nullptr if they can't be found (dynamic
// builds look them up in ggml.dll / ggml-base.dll next to crispasr.dll).
const GgmlApi* ggml_api();

}  // namespace tmx
