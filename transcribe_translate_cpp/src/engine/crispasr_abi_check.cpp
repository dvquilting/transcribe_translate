// crispasr_abi_check.cpp - compiled only into the static build (cmake/static_cli.cmake).
//
// crispasr_api.h carries copies of the CrispASR declarations this program uses,
// so the dynamic build works without CrispASR's headers. Copies can drift from
// the library. This file includes CrispASR's real headers and checks, at compile
// time, that every function in the table has exactly the signature we call it
// with and that the mirrored structs have the same layout. A mismatch stops the
// build with "CrispASR changed <name>" instead of crashing users at run time.
#include <cstddef>
#include <type_traits>

#include "crispasr.h"
#include "crispasr_chat.h"
#include "crispasr_session.h"
#include "ggml-backend.h"

namespace mirror {
#include "crispasr_chat_mirror.h"
}

#define TMX_ABI_CHECK_REAL_HEADERS
#include "crispasr_api.h"

// Functions: our pointer type must be the real function's type.
#define TMX_CHECK_FN(ret, name, args) \
    static_assert(std::is_same<decltype(&::name), ret(*) args>::value, "CrispASR changed " #name);
CRISPASR_FUNCTIONS(TMX_CHECK_FN)
#ifdef TMX_HAVE_ALIGN_FREE  // added by cmake/patch_crispasr.cmake, not in a public header
extern "C" void crispasr_align_free_cache_abi(void);
#endif
// Speaker detection: the real declaration, with our mirrored structs (which the
// public header only declares) - so the same function type.
static_assert(std::is_same<decltype(&::crispasr_diarize_segments_turns_abi), crispasr_diarize_segments_turns_fn>::value,
              "CrispASR changed crispasr_diarize_segments_turns_abi");
// CrispASR's own static_asserts pin these sizes (64-bit) for its bindings.
static_assert(sizeof(void*) != 8 || sizeof(crispasr_diarize_seg_abi) == 24, "diarize seg layout");
static_assert(sizeof(void*) != 8 || sizeof(crispasr_diarize_turn_abi) == 24, "diarize turn layout");
static_assert(sizeof(void*) != 8 || sizeof(crispasr_diarize_opts_abi) == 48, "diarize opts layout");
static_assert(sizeof(void*) != 8 || offsetof(crispasr_diarize_opts_abi, foxnose_embedder_path) == 24, "diarize opts layout");
static_assert(sizeof(void*) != 8 || offsetof(crispasr_diarize_opts_abi, min_speakers) == 32, "diarize opts layout");
// ggml: same, except ggml_backend_dev_type, which we read as int (it returns an enum).
#define TMX_CHECK_GGML(ret, name, args) \
    static_assert(std::is_same<decltype(&::name), ret(*) args>::value || \
                      std::is_same<ret, int>::value, "ggml changed " #name);
GGML_FUNCTIONS(TMX_CHECK_GGML)
static_assert(sizeof(ggml_backend_dev_type(nullptr)) == sizeof(int), "ggml_backend_dev_type is no longer int-sized");

// Structs: same size, and every field at the same offset with the same size.
#define TMX_CHECK_FIELD(T, f)                                                    \
    static_assert(offsetof(mirror::T, f) == offsetof(::T, f) &&                  \
                      sizeof(mirror::T::f) == sizeof(::T::f),                    \
                  "CrispASR changed " #T "::" #f);
#define TMX_CHECK_SIZE(T) static_assert(sizeof(mirror::T) == sizeof(::T), "CrispASR changed the size of " #T);
TMX_CHECK_SIZE(crispasr_chat_error)
TMX_CHECK_FIELD(crispasr_chat_error, code)
TMX_CHECK_FIELD(crispasr_chat_error, message)
TMX_CHECK_SIZE(crispasr_chat_message)
TMX_CHECK_FIELD(crispasr_chat_message, role)
TMX_CHECK_FIELD(crispasr_chat_message, content)
TMX_CHECK_SIZE(crispasr_chat_open_params)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_threads)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_threads_batch)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_ctx)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_batch)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_ubatch)
TMX_CHECK_FIELD(crispasr_chat_open_params, n_gpu_layers)
TMX_CHECK_FIELD(crispasr_chat_open_params, use_mmap)
TMX_CHECK_FIELD(crispasr_chat_open_params, use_mlock)
TMX_CHECK_FIELD(crispasr_chat_open_params, embeddings)
TMX_CHECK_FIELD(crispasr_chat_open_params, chat_template)
TMX_CHECK_SIZE(crispasr_chat_generate_params)
TMX_CHECK_FIELD(crispasr_chat_generate_params, max_tokens)
TMX_CHECK_FIELD(crispasr_chat_generate_params, temperature)
TMX_CHECK_FIELD(crispasr_chat_generate_params, top_k)
TMX_CHECK_FIELD(crispasr_chat_generate_params, top_p)
TMX_CHECK_FIELD(crispasr_chat_generate_params, min_p)
TMX_CHECK_FIELD(crispasr_chat_generate_params, repeat_penalty)
TMX_CHECK_FIELD(crispasr_chat_generate_params, repeat_last_n)
TMX_CHECK_FIELD(crispasr_chat_generate_params, seed)
TMX_CHECK_FIELD(crispasr_chat_generate_params, stop)
TMX_CHECK_FIELD(crispasr_chat_generate_params, n_stop)
TMX_CHECK_FIELD(crispasr_chat_generate_params, prefill_only)
// crispasr_open_params_v1 is only forward-declared in the public header (its
// definition is in crispasr_c_api.cpp); it is versioned by its abi_version field.
