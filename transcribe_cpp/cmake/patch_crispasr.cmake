# patch_crispasr.cmake - small changes to the CrispASR source, applied when the
# static build is configured (included from crispasr_overlay.cmake).
#
# Each patch replaces one exact block of text. It is skipped when it is already
# applied, and stops the configuration when the block is not found.

# CrispASR is pinned to a tested commit (CRISPASR_COMMIT in build_static.bat/.sh),
# so a patch that doesn't apply means the checkout isn't that commit: stop, rather
# than build a program that silently lacks the feature. -DTMX_ALLOW_UNPATCHED=ON
# turns this into a warning (for trying a newer CrispASR).
option(TMX_ALLOW_UNPATCHED "Build even if a CrispASR source patch doesn't apply" OFF)
function(tmx_patch_failed name why)
    if(TMX_ALLOW_UNPATCHED)
        message(WARNING "CrispASR patch '${name}': ${why} - skipped (TMX_ALLOW_UNPATCHED)")
    else()
        message(FATAL_ERROR "CrispASR patch '${name}': ${why}.\n"
                            "The build expects the CrispASR commit pinned in build_static.bat/.sh; delete "
                            "third_party/CrispASR to fetch it again, or pass -DTMX_ALLOW_UNPATCHED=ON to build "
                            "without this change.")
    endif()
endfunction()

# Sets <result_var> (in the caller's scope) to TRUE when the patch is in place.
function(tmx_patch_source result_var file name old new)
    set(${result_var} FALSE PARENT_SCOPE)
    set(path "${CMAKE_SOURCE_DIR}/${file}")
    if(NOT EXISTS "${path}")
        tmx_patch_failed("${name}" "${file} was not found")
        return()
    endif()
    file(READ "${path}" text)
    string(REPLACE "\r\n" "\n" text "${text}")
    string(FIND "${text}" "${new}" already)
    if(NOT already EQUAL -1)
        message(STATUS "CrispASR patch '${name}': already applied")
        set(${result_var} TRUE PARENT_SCOPE)
        return()
    endif()
    string(FIND "${text}" "${old}" at)
    if(at EQUAL -1)
        tmx_patch_failed("${name}" "the code it changes was not found in ${file} (CrispASR has changed)")
        return()
    endif()
    string(REPLACE "${old}" "${new}" text "${text}")
    file(WRITE "${path}" "${text}")
    message(STATUS "CrispASR patch '${name}': applied to ${file}")
    set(${result_var} TRUE PARENT_SCOPE)
endfunction()

# qwen3-tts: clone from the speaker embedding only ("x-vector" mode) when no
# reference transcript is given. The reference's speech codes are then not
# used, so the original speaker's accent does not carry over into another
# language. Used by "transcribe_multi dub" for cross-language dubbing.
tmx_patch_source(TMX_PATCH_XVEC_ABI src/crispasr_c_api.cpp "qwen3-tts x-vector clone"
[=[        if (ends_with_wav(path)) {
            if (!ref_text_or_null)
                return -2;
            int rc = qwen3_tts_set_voice_prompt_with_text(s->qwen3_tts_ctx, path, ref_text_or_null);]=]
[=[        if (ends_with_wav(path)) {
            if (!ref_text_or_null) {
                // No transcript: clone from the speaker embedding only
                // (x-vector mode). The reference's speech codes are not used,
                // so its accent does not carry over into another language.
                int rc = qwen3_tts_set_voice_prompt_xvec_only(s->qwen3_tts_ctx, path);
                if (rc == 0)
                    s->qwen3_tts_voice_loaded = true;
                return rc;
            }
            int rc = qwen3_tts_set_voice_prompt_with_text(s->qwen3_tts_ctx, path, ref_text_or_null);]=])
tmx_patch_source(TMX_PATCH_XVEC_TEXT src/qwen3_tts.cpp "qwen3-tts x-vector transcript"
[=[        if (!ctx->runtime_ref_text.empty()) {
            ref_text = ctx->runtime_ref_text;
        } else if (ctx->vp_active >= 0) {]=]
[=[        if (!ctx->runtime_ref_text.empty()) {
            ref_text = ctx->runtime_ref_text;
        } else if (ctx->xvec_only) {
            ref_text = "."; // x-vector clone: never borrow a loaded voice pack's transcript
        } else if (ctx->vp_active >= 0) {]=])

# Forced aligner: CrispASR keeps the aligner model (~1 GB) cached after
# crispasr_align_words_abi, and the function that frees it is not part of the C
# ABI. Export it, so transcribe_multi can free the aligner before it loads the
# post-processing language model (otherwise both share the GPU/RAM).
tmx_patch_source(TMX_PATCH_ALIGN_FREE src/crispasr_c_api.cpp "aligner cache free"
[=[CA_EXPORT crispasr_align_result* crispasr_align_words_abi(]=]
[=[// Free the forced-aligner model that crispasr_align_words_abi keeps cached
// between calls (added by transcribe_multi's build).
CA_EXPORT void crispasr_align_free_cache_abi(void) {
    crispasr_aligner_free_cache();
}

CA_EXPORT crispasr_align_result* crispasr_align_words_abi(]=])

# Speaker detection: report how far the voice embedding (the long part) is, as
# the engine's "progress:" lines, when TT_DIARIZE_PROGRESS is set ("transcribe_multi
# speakers" sets it). One line per percent at most; several threads embed, so
# two neighbouring lines can arrive swapped (the window ignores a step back).
tmx_patch_source(TMX_PATCH_DIARIZE_PROGRESS_INC src/core/foxnose_pipeline.cpp "speaker detection progress (includes)"
[=[#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>]=]
[=[#include <algorithm>
#include <atomic> // tt_engine: progress
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>]=])
tmx_patch_source(TMX_PATCH_DIARIZE_PROGRESS src/core/foxnose_pipeline.cpp "speaker detection progress"
[=[    core_parallel::for_each_task(n_spans, n_workers, [&](int si, int slot) { do_span(si, slot); });]=]
[=[    const bool tt_progress = std::getenv("TT_DIARIZE_PROGRESS") != nullptr; // tt_engine: progress lines
    std::atomic<int> tt_done{0}, tt_shown{0};
    core_parallel::for_each_task(n_spans, n_workers, [&](int si, int slot) {
        do_span(si, slot);
        if (!tt_progress)
            return;
        const int pct = (int)((long long)++tt_done * 100 / n_spans);
        int shown = tt_shown.load();
        while (pct > shown && !tt_shown.compare_exchange_weak(shown, pct)) {
        }
        if (pct > shown) {
            std::fprintf(stdout, "progress: speakers %d/100 percent\n", pct);
            std::fflush(stdout);
        }
    });]=])

# Speaker detection, grouping the voices: the eigenvectors of the windows'
# similarity matrix are the slow part of a long recording (it grows with the
# square of its length). The same sums in the same order - the results are
# bit-identical (checked against the original on generated voices) - but four
# rows at a time, and with the threads the parallel sweep over the number of
# speakers leaves free. A fixed number of speakers gets all threads.
tmx_patch_source(TMX_PATCH_EIGEN_0 src/core/spectral_diarize.cpp "faster voice grouping (rows)"
[=[        for (int i = 0; i < n; i++) {
            for (int c = 0; c < k; c++) {
                double acc = shift * q[(size_t)i * k + c];
                for (int j = 0; j < n; j++)
                    acc += m[(size_t)i * n + j] * q[(size_t)j * k + c];
                z[(size_t)i * k + c] = acc;
            }
        }
        q.swap(z);]=]
[=[        // tt_engine: the same sums in the same order as the plain loop (the
        // results are bit-identical), but four rows at a time, so the processor
        // works on 4*k sums at once instead of one, and the rows shared between
        // the threads this solve may use (tt_eigen_threads).
        auto tt_rows = [&](int r0, int r1) {
            enum { kRows = 4, kCols = 32 };
            const double* mp = m.data();
            const double* qp = q.data();
            double* zp = z.data();
            int i = r0;
            if (k <= kCols)
                for (; i + kRows <= r1; i += kRows) {
                    double acc[kRows][kCols];
                    for (int r = 0; r < kRows; r++)
                        for (int c = 0; c < k; c++)
                            acc[r][c] = shift * qp[(size_t)(i + r) * k + c];
                    const double* m0 = mp + (size_t)i * n;
                    const double* m1 = m0 + n;
                    const double* m2 = m1 + n;
                    const double* m3 = m2 + n;
                    for (int j = 0; j < n; j++) {
                        const double* qj = qp + (size_t)j * k;
                        const double a0 = m0[j], a1 = m1[j], a2 = m2[j], a3 = m3[j];
                        for (int c = 0; c < k; c++) {
                            const double x = qj[c];
                            acc[0][c] += a0 * x;
                            acc[1][c] += a1 * x;
                            acc[2][c] += a2 * x;
                            acc[3][c] += a3 * x;
                        }
                    }
                    for (int r = 0; r < kRows; r++)
                        for (int c = 0; c < k; c++)
                            zp[(size_t)(i + r) * k + c] = acc[r][c];
                }
            for (; i < r1; i++) {
                for (int c = 0; c < k; c++) {
                    double acc = shift * qp[(size_t)i * k + c];
                    for (int j = 0; j < n; j++)
                        acc += mp[(size_t)i * n + j] * qp[(size_t)j * k + c];
                    zp[(size_t)i * k + c] = acc;
                }
            }
        };
        const int tt_threads = (double)n * n * k >= 4e6 ? std::max(1, tt_eigen_threads.load()) : 1;
        core_parallel::for_each_chunk(n, tt_threads, tt_rows);
        q.swap(z);]=])
tmx_patch_source(TMX_PATCH_EIGEN_1 src/core/spectral_diarize.cpp "faster voice grouping (threads)"
[=[std::vector<double> top_eigenvectors(const std::vector<double>& m, int n, int k, unsigned seed) {]=]
[=[// tt_engine: how many threads one top_eigenvectors() call may use for its rows
// (set by cluster_speakers(): what its own parallel sweep leaves free).
std::atomic<int> tt_eigen_threads{1};

std::vector<double> top_eigenvectors(const std::vector<double>& m, int n, int k, unsigned seed) {]=])
tmx_patch_source(TMX_PATCH_EIGEN_2 src/core/spectral_diarize.cpp "faster voice grouping (single run)"
[=[    auto run = [&](int k) {
        std::vector<int> lab = spectral_labels(aff.data(), n, k, seed);
        return refine_spherical(x, n, d, lab);
    };
]=]
[=[    auto run = [&](int k) {
        std::vector<int> lab = spectral_labels(aff.data(), n, k, seed);
        return refine_spherical(x, n, d, lab);
    };
    // tt_engine: a single spectral run gets all the threads.
    const int tt_all_threads = core_parallel::resolve_threads(n, n_threads);
    tt_eigen_threads = tt_all_threads;
]=])
tmx_patch_source(TMX_PATCH_EIGEN_3 src/core/spectral_diarize.cpp "faster voice grouping (sweep)"
[=[    const int n_cand = upper - lower + 1;
    std::vector<std::vector<int>> cand_labels((size_t)n_cand);]=]
[=[    const int n_cand = upper - lower + 1;
    tt_eigen_threads = std::max(1, tt_all_threads / n_cand); // tt_engine: the sweep runs n_cand at once
    std::vector<std::vector<int>> cand_labels((size_t)n_cand);]=])

# Speaker detection: how long listening and grouping took (one line, when
# TT_DIARIZE_PROGRESS is set), and the grouping with TT_DIARIZE_THREADS threads
# when that is more than the embedders (there is one on a graphics card).
tmx_patch_source(TMX_PATCH_DIARIZE_TIMING_0 src/core/foxnose_pipeline.cpp "speaker detection timing (include)"
[=[#include <map>
#include <set>]=]
[=[#include <map>
#include <set>
#include <chrono> // tt_engine: timing]=])
tmx_patch_source(TMX_PATCH_DIARIZE_TIMING_1 src/core/foxnose_pipeline.cpp "speaker detection timing (start)"
[=[    Result res;
    if (!pcm || n_samples <= 0 || !embed || embed_dim <= 0 || speech.empty())
        return res;
]=]
[=[    Result res;
    if (!pcm || n_samples <= 0 || !embed || embed_dim <= 0 || speech.empty())
        return res;
    const auto tt_t0 = std::chrono::steady_clock::now(); // tt_engine: timing
]=])
tmx_patch_source(TMX_PATCH_DIARIZE_TIMING_2 src/core/foxnose_pipeline.cpp "speaker detection timing (grouping)"
[=[    std::vector<int> labels =
        core_spectral::cluster_speakers(emb.data(), n, embed_dim, params.min_speakers, params.max_speakers,
                                        params.num_speakers, &est, params.seed, params.n_workers);
    res.reason = est.reason ? est.reason : "";]=]
[=[    // tt_engine: the grouping may use more threads than there are embedders (one
    // on a graphics card), and how long each part took is reported.
    const auto tt_t1 = std::chrono::steady_clock::now();
    int tt_group_threads = params.n_workers;
    if (const char* e = std::getenv("TT_DIARIZE_THREADS"))
        tt_group_threads = std::max(tt_group_threads, std::atoi(e));
    std::vector<int> labels =
        core_spectral::cluster_speakers(emb.data(), n, embed_dim, params.min_speakers, params.max_speakers,
                                        params.num_speakers, &est, params.seed, tt_group_threads);
    if (std::getenv("TT_DIARIZE_PROGRESS")) {
        const auto tt_t2 = std::chrono::steady_clock::now();
        std::fprintf(stdout, "Speakers: listened to %d pieces of speech in %.1f s, grouped the voices in %.1f s\n", n,
                     std::chrono::duration<double>(tt_t1 - tt_t0).count(),
                     std::chrono::duration<double>(tt_t2 - tt_t1).count());
        std::fflush(stdout);
    }
    res.reason = est.reason ? est.reason : "";]=])
