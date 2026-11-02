package com.davidv.transcribe.data

import com.davidv.transcribe.engine.NativeEngine
import org.json.JSONArray
import org.json.JSONObject

data class AsrModel(
    val key: String, val display: String, val file: String, val url: String, val size: String,
    val chunked: Boolean, val needsLanguage: Boolean, val description: String,
)

data class TranslationModel(
    val key: String, val display: String, val file: String, val url: String, val size: String,
    val description: String, val langs: List<String>,
)

data class Language(val code: String, val name: String, val native: String) {
    /** "German — Deutsch" (as the desktop menus show it). */
    val label: String get() = if (native == name || native.isEmpty()) name else "$name — $native"
}

/** A file the app downloads: a model of either kind. */
data class ModelFile(val file: String, val url: String, val display: String, val size: String, val description: String)

/**
 * The models and languages, from the native engine (the same tables as the
 * desktop programs: common/src/model_catalog.cpp, translate_cpp/src/models.cpp).
 */
class Catalog(json: String) {
    val asr: List<AsrModel>
    val aligner: AsrModel
    val punctuation: AsrModel
    val translation: List<TranslationModel>
    val polish: TranslationModel
    val languages: List<Language>

    init {
        val j = JSONObject(json)
        asr = j.getJSONArray("asr").objects().map { asrOf(it) }
        aligner = asrOf(j.getJSONObject("aligner"))
        punctuation = asrOf(j.getJSONObject("punctuation"))
        translation = j.getJSONArray("translation").objects().map { translationOf(it) }
        polish = translationOf(j.getJSONObject("polish"))
        languages = j.getJSONArray("languages").objects()
            .map { Language(it.getString("code"), it.getString("name"), it.optString("native")) }
            .sortedBy { it.name }
    }

    fun asrModel(key: String) = asr.firstOrNull { it.key == key }
    fun translationModel(key: String) = translation.firstOrNull { it.key == key }
    fun language(code: String) = languages.firstOrNull { it.code.equals(code, ignoreCase = true) }

    /** Every file the app may download, without duplicates (the corrected copy shares a translation model). */
    val allFiles: List<ModelFile>
        get() {
            val all = asr.map { ModelFile(it.file, it.url, it.display, it.size, "Speech to text. " + it.description) } +
                ModelFile(aligner.file, aligner.url, aligner.display, aligner.size, aligner.description) +
                ModelFile(punctuation.file, punctuation.url, punctuation.display, punctuation.size, punctuation.description) +
                translation.map {
                    val extra = if (it.key == polish.key) " Also makes the corrected copy of transcripts." else ""
                    ModelFile(it.file, it.url, it.display, it.size, "Translation. " + it.description + extra)
                }
            return all.distinctBy { it.file }
        }

    /** As model_supports() in translate_cpp: trained on the language (or its base language). */
    fun supports(m: TranslationModel, code: String): Boolean {
        if (m.langs.isEmpty()) return true
        if (m.langs.any { it.equals(code, true) }) return true
        val base = code.substringBefore('-')
        return code.contains('-') && m.langs.any { it.equals(base, true) }
    }

    companion object {
        @Volatile private var loaded: Catalog? = null
        fun get(): Catalog = loaded ?: synchronized(this) { loaded ?: Catalog(NativeEngine.catalog()).also { loaded = it } }

        private fun asrOf(o: JSONObject) = AsrModel(
            o.getString("key"), o.getString("display"), o.getString("file"), o.getString("url"), o.getString("size"),
            o.optBoolean("chunked"), o.optBoolean("needs_language"), o.optString("description"),
        )

        private fun translationOf(o: JSONObject) = TranslationModel(
            o.getString("key"), o.getString("display"), o.getString("file"), o.getString("url"), o.getString("size"),
            o.optString("description"), o.optJSONArray("langs")?.strings() ?: emptyList(),
        )
    }
}

fun JSONArray.objects(): List<JSONObject> = (0 until length()).map { getJSONObject(it) }
fun JSONArray.strings(): List<String> = (0 until length()).map { getString(it) }
