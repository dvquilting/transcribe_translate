package com.davidv.transcribe.ui

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AudioFile
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import com.davidv.transcribe.audio.RecorderService
import com.davidv.transcribe.data.JobKind
import com.davidv.transcribe.data.JobStatus
import com.davidv.transcribe.data.ModelFile
import com.davidv.transcribe.data.formatBytes
import com.davidv.transcribe.engine.EngineService

@Composable
fun TranscribeScreen(vm: AppViewModel) {
    val context = LocalContext.current
    val cat = vm.catalog
    val audio by vm.audio.collectAsState()
    val jobs by vm.jobs.jobs.collectAsState()
    val rec by RecorderService.state.collectAsState()
    val active = jobs.firstOrNull { it.active }
    val last = jobs.firstOrNull { it.kind == JobKind.TRANSCRIBE }

    var model by remember { mutableStateOf(vm.settings.asrModel.takeIf { cat.asrModel(it) != null } ?: cat.asr.first().key) }
    var language by remember { mutableStateOf(vm.settings.spokenLanguage) }
    var formats by remember { mutableStateOf(vm.settings.formats) }
    var polish by remember { mutableStateOf(vm.settings.polish) }
    var wordTiming by remember { mutableStateOf(vm.settings.wordTiming) }
    val m = cat.asrModel(model)!!
    if (m.needsLanguage && language == "auto") language = "en"

    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            try {  // keep access after the app restarts
                context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) {}
            vm.audio.value = Picked(uri, EngineService.displayName(context, uri))
        }
    }
    val askMic = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { ok ->
        if (ok) RecorderService.start(context)
    }
    // A finished recording becomes the input.
    LaunchedEffect(rec.recording, rec.file) {
        val f = rec.file
        if (!rec.recording && f != null && rec.seconds > 0.5) vm.audio.value = Picked(Uri.fromFile(f), f.name)
    }

    // The models this run needs, and what is still to download.
    val needed = buildList {
        add(ModelFile(m.file, m.url, m.display, m.size, ""))
        if (wordTiming && m.chunked && "srt" in formats) add(ModelFile(cat.aligner.file, cat.aligner.url, cat.aligner.display, cat.aligner.size, ""))
        if (polish) add(ModelFile(cat.polish.file, cat.polish.url, cat.polish.display, cat.polish.size, ""))
    }
    val missing = vm.missingBytes(needed)

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        // ---- input ----
        Card(Modifier.fillMaxWidth()) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(audio?.name ?: "No audio chosen", style = MaterialTheme.typography.titleMedium)
                if (rec.recording) {
                    Text("Recording ${duration(rec.seconds)}", color = MaterialTheme.colorScheme.error)
                    LinearProgressIndicator(progress = { rec.level }, Modifier.fillMaxWidth())
                }
                Row {
                    OutlinedButton(onClick = { pick.launch(arrayOf("audio/*", "video/*")) }, enabled = !rec.recording) {
                        Icon(Icons.Default.AudioFile, null); Spacer(Modifier.width(6.dp)); Text("Choose file")
                    }
                    Spacer(Modifier.width(8.dp))
                    if (rec.recording) {
                        Button(onClick = { RecorderService.stop(context) }) { Icon(Icons.Default.Stop, null); Text("Stop") }
                    } else {
                        OutlinedButton(onClick = {
                            if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED)
                                RecorderService.start(context)
                            else askMic.launch(Manifest.permission.RECORD_AUDIO)
                        }) { Icon(Icons.Default.Mic, null); Spacer(Modifier.width(6.dp)); Text("Record") }
                    }
                }
            }
        }

        // ---- options ----
        Choice("Speech model", model, cat.asr.map { it.key to "${it.display}  —  ${it.size}" }) { model = it }
        Hint(m.description)
        val langs = (if (m.needsLanguage) emptyList() else listOf("auto" to "Detect automatically")) +
            cat.languages.map { it.code to it.label }
        Choice("Spoken language", language, langs) { language = it }
        if (!m.chunked) Hint("Parakeet detects the language itself (25 European languages).")

        Text("Save as", style = MaterialTheme.typography.titleSmall)
        Row {
            Column(Modifier.weight(1f)) {
                CheckRow("Text (.txt)", "txt" in formats) { formats = toggle(formats, "txt", it) }
                CheckRow("Subtitles (.srt)", "srt" in formats) { formats = toggle(formats, "srt", it) }
            }
            Column(Modifier.weight(1f)) {
                CheckRow("Word (.docx)", "docx" in formats) { formats = toggle(formats, "docx", it) }
                CheckRow("PDF (.pdf)", "pdf" in formats) { formats = toggle(formats, "pdf", it) }
            }
        }
        if (m.chunked) CheckRow("Word-level subtitle timing (+${cat.aligner.size} model)", wordTiming, enabled = "srt" in formats) { wordTiming = it }
        CheckRow("Corrected copy (AI spelling and grammar, ${cat.polish.display})", polish) { polish = it }
        if (polish) Hint("Takes about as long again as the transcription; the original transcript is always saved too.")

        if (missing > 0) Hint("First use downloads ${formatBytes(missing)}" +
            if (!vm.settings.allowMobileData && !vm.models.unmetered()) " - connect to Wi-Fi, or allow mobile data in Models > Settings." else ".",
            warning = !vm.settings.allowMobileData && !vm.models.unmetered())

        Button(
            onClick = { vm.startTranscription(model, language, formats, polish, wordTiming) },
            enabled = audio != null && active == null && !rec.recording && formats.isNotEmpty(),
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Transcribe") }
        if (active != null && active.kind != JobKind.TRANSCRIBE) Hint("Another job is running (see below).")

        // ---- progress and result ----
        (active ?: last)?.let { j ->
            JobCard(j) { vm.cancel(j.id) }
            if (!j.active && j.kind == JobKind.TRANSCRIBE && j.status == JobStatus.DONE) {
                val base = j.spec.optString("name", "transcript")
                val text = vm.jobText(j, "$base.txt")
                FilesRow(context, j.files)
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Button(onClick = { vm.translateText(stripNote(vm.jobText(j, "$base.clean.txt").ifEmpty { text })) }) { Text("Translate…") }
                    Spacer(Modifier.width(8.dp))
                    OutlinedButton(onClick = { shareText(context, text) }) { Text("Share text") }
                }
                TextResult("Corrected copy", stripNote(vm.jobText(j, "$base.clean.txt")))
                TextResult("Transcript", text)
            }
        }
    }
}

fun toggle(set: Set<String>, item: String, on: Boolean): Set<String> = if (on) set + item else set - item

/** Without the "[... corrected by a local AI model ...]" line at the top. */
fun stripNote(text: String): String =
    if (text.startsWith("[")) text.substringAfter("]\n", text).trimStart() else text
