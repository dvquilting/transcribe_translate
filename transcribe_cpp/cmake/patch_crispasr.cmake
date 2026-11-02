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
