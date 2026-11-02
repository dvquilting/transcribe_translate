package com.davidv.transcribe.ui

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import com.davidv.transcribe.data.Catalog
import com.davidv.transcribe.data.Job
import com.davidv.transcribe.data.JobKind
import com.davidv.transcribe.data.JobRepository
import com.davidv.transcribe.data.ModelFile
import com.davidv.transcribe.data.ModelStore
import com.davidv.transcribe.data.Settings
import com.davidv.transcribe.data.parseSize
import com.davidv.transcribe.engine.EngineService
import kotlinx.coroutines.flow.MutableStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/** An input chosen for a job: where it is and what to call it. */
data class Picked(val uri: Uri, val name: String)

enum class Tab { TRANSCRIBE, TRANSLATE, HISTORY, MODELS }

class AppViewModel(app: Application) : AndroidViewModel(app) {
    val catalog: Catalog = Catalog.get()
    val settings = Settings(app)
    val models = ModelStore(app)
    val jobs = JobRepository(app)

    val tab = MutableStateFlow(Tab.TRANSCRIBE)
    val audio = MutableStateFlow<Picked?>(null)
    val document = MutableStateFlow<Picked?>(null)
    val pastedText = MutableStateFlow("")
    /** A job to show in full (History). */
    val openJob = MutableStateFlow<String?>(null)
    /** Bumped when model files change, so the Models screen re-reads the disk. */
    val modelsVersion = MutableStateFlow(0)

    // ---- transcription ----------------------------------------------------------------

    fun startTranscription(asrKey: String, language: String, formats: Set<String>, polish: Boolean, wordTiming: Boolean) {
        val input = audio.value ?: return
        val model = catalog.asrModel(asrKey) ?: return
        settings.asrModel = asrKey; settings.spokenLanguage = language; settings.formats = formats
        settings.polish = polish; settings.wordTiming = wordTiming
        val base = input.name.substringBeforeLast('.').ifBlank { "transcript" }
        val spec = JSONObject().apply {
            put("input", input.uri.toString()); put("model", model.key); put("language", language)
            put("formats", JSONArray(formats.toList())); put("polish", polish)
            put("word_timing", wordTiming && model.chunked && "srt" in formats)
            put("name", base); put("title", base)
        }
        launch(Job.new(JobKind.TRANSCRIBE, input.name, spec))
    }

    // ---- translation ----------------------------------------------------------------

    fun startTranslation(modelKey: String, from: String, to: String, context: Boolean, pdf: Boolean) {
        settings.translationModel = modelKey; settings.translateFrom = from; settings.translateTo = to
        settings.useContext = context
        val doc = document.value
        val spec = JSONObject().put("model", modelKey).put("from", from).put("to", to).put("context", context).put("pdf", pdf)
        val title: String
        if (doc != null) {
            val ext = doc.name.substringAfterLast('.', "txt").lowercase()
            val format = when (ext) { "srt" -> "srt"; "docx" -> "docx"; "md", "markdown" -> "md"; else -> "txt" }
            spec.put("input", doc.uri.toString()).put("format", format)
            val base = doc.name.substringBeforeLast('.')
            spec.put("name", "$base.$to").put("title", base)
            title = doc.name
        } else {
            val text = pastedText.value.trim()
            if (text.isEmpty()) return
            spec.put("text", text).put("format", "txt").put("name", "Translation.$to").put("title", "Translation")
            title = text.take(40).replace('\n', ' ') + if (text.length > 40) "..." else ""
        }
        launch(Job.new(JobKind.TRANSLATE, title, spec))
    }

    /** Translate a finished transcript: its text goes to the Translate tab. */
    fun translateText(text: String) {
        document.value = null
        pastedText.value = text
        tab.value = Tab.TRANSLATE
    }

    // ---- models ------------------------------------------------------------------------

    fun download(files: List<ModelFile>) {
        if (files.isEmpty()) return
        val spec = JSONObject().put("files", JSONArray().apply {
            files.forEach { put(JSONObject().put("file", it.file).put("url", it.url).put("display", it.display).put("size", it.size)) }
        })
        launch(Job.new(JobKind.DOWNLOAD, files.joinToString { it.display }, spec))
    }

    fun deleteModel(file: String) {
        models.delete(file)
        modelsVersion.value++
    }

    /** Bytes still to download for these files (0 when all are there). */
    fun missingBytes(files: List<ModelFile>): Long =
        files.filter { !models.isDownloaded(it.file) }.sumOf { parseSize(it.size) }

    // ---- jobs --------------------------------------------------------------------------

    private fun launch(job: Job) {
        EngineService.start(getApplication(), job)
        jobs.refresh(job.id)
        jobs.watchLog(job.id)
    }

    fun cancel(id: String) = EngineService.cancel(getApplication(), id)

    fun deleteJob(id: String) {
        jobs.delete(id)
        if (openJob.value == id) openJob.value = null
    }

    /** Text the job saved for display (transcript paragraphs, corrected copy, translation). */
    fun jobText(job: Job, which: String): String = jobs.store.text(job.id, which)

    fun jobFiles(job: Job): List<File> = File(jobs.store.dir(job.id), "out").listFiles()?.toList() ?: emptyList()

    /** A file shared from another app (Share > Transcribe & Translate), copied into the app. */
    fun sharedFile(type: String, local: Uri, name: String) {
        if (type.startsWith("audio/") || type.startsWith("video/")) {
            audio.value = Picked(local, name)
            tab.value = Tab.TRANSCRIBE
        } else {
            document.value = Picked(local, name)
            pastedText.value = ""
            tab.value = Tab.TRANSLATE
        }
    }
}
