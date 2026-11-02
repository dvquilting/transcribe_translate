// crispasr_chat_mirror.h - copies of the POD structs of CrispASR's
// include/crispasr_chat.h (the text LLM ABI), so the dynamic build needs no
// CrispASR headers. crispasr_abi_check.cpp compares their layout with the real
// ones in the static build. Plain C structs; no include guard on purpose
// (crispasr_abi_check.cpp includes it a second time inside a namespace).
#include <stddef.h>
#include <stdint.h>

extern "C" {
typedef struct crispasr_chat_session crispasr_chat_session;
typedef struct crispasr_chat_error {
    int32_t code;
    char message[256];
} crispasr_chat_error;
typedef struct crispasr_chat_message {
    const char* role;
    const char* content;
} crispasr_chat_message;
typedef struct crispasr_chat_open_params {
    int32_t n_threads;
    int32_t n_threads_batch;
    int32_t n_ctx;         // 0 = model default (can be huge - always set it)
    int32_t n_batch;
    int32_t n_ubatch;
    int32_t n_gpu_layers;  // -1 = all, 0 = CPU only
    bool use_mmap;
    bool use_mlock;
    bool embeddings;
    const char* chat_template;
} crispasr_chat_open_params;
typedef struct crispasr_chat_generate_params {
    int32_t max_tokens;
    float temperature;     // 0 = greedy
    int32_t top_k;
    float top_p;
    float min_p;
    float repeat_penalty;
    int32_t repeat_last_n;
    uint32_t seed;
    const char* const* stop;
    size_t n_stop;
    bool prefill_only;
} crispasr_chat_generate_params;
}
