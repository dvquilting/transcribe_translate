package com.davidv.transcribe.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Description
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import com.davidv.transcribe.data.JobKind
import com.davidv.transcribe.data.JobStatus
import com.davidv.transcribe.data.ModelFile
import com.davidv.transcribe.data.formatBytes
import com.davidv.transcribe.engine.EngineService

@Composable
fun TranslateScreen(vm: AppViewModel) {
    val context = LocalContext.current
    val cat = vm.catalog
    val doc by vm.document.collectAsState()
    val pasted by vm.pastedText.collectAsState()
    val jobs by vm.jobs.jobs.collectAsState()
    val active = jobs.firstOrNull { it.active }
    val last = jobs.firstOrNull { it.kind == JobKind.TRANSLATE }

    var model by remember { mutableStateOf(vm.settings.translationModel.takeIf { cat.translationModel(it) != null } ?: cat.translation.first().key) }
    var from by remember { mutableStateOf(vm.settings.translateFrom) }
    var to by remember { mutableStateOf(vm.settings.translateTo) }
    var context_ by remember { mutableStateOf(vm.settings.useContext) }
    var pdf by remember { mutableStateOf(false) }
    val m = cat.translationModel(model)!!

    val pick = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (_: SecurityException) {}
            vm.document.value = Picked(uri, EngineService.displayName(context, uri))
            vm.pastedText.value = ""
        }
    }

    // Languages the chosen model wasn't trained on.
    val notes = buildList {
        if (!cat.supports(m, to)) add("${m.display} was not trained on ${cat.language(to)?.name ?: to}; Qwen 3.5 handles more languages.")
        if (from != "auto" && !cat.supports(m, from)) add("${m.display} was not trained on ${cat.language(from)?.name ?: from}.")
        if (from == to) add("The two languages are the same.")
    }
    val missing = vm.missingBytes(listOf(ModelFile(m.file, m.url, m.display, m.size, "")))
    val isTxt = doc == null || doc!!.name.substringAfterLast('.', "txt").lowercase() == "txt"

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        // ---- input ----
        if (doc != null) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Description, null)
                Spacer(Modifier.width(8.dp))
                Text(doc!!.name, Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
                TextButton(onClick = { vm.document.value = null }) { Text("Remove") }
            }
        } else {
            OutlinedTextField(value = pasted, onValueChange = { vm.pastedText.value = it }, label = { Text("Text to translate") },
                modifier = Modifier.fillMaxWidth().heightIn(min = 140.dp, max = 320.dp))
        }
        OutlinedButton(onClick = {
            pick.launch(arrayOf("text/plain", "text/markdown", "application/x-subrip", "text/srt",
                "application/vnd.openxmlformats-officedocument.wordprocessingml.document", "application/octet-stream"))
        }) { Text("Choose a file (.txt, .srt, .docx, .md)") }

        // ---- options ----
        Choice("Translation model", model, cat.translation.map { it.key to "${it.display}  —  ${it.size}" }) { model = it }
        Hint(m.description)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Choice("From", from, listOf("auto" to "Detect automatically") + cat.languages.map { it.code to it.label }) { from = it }
            }
            IconButton(onClick = { if (from != "auto") { val t = from; from = to; to = t } }, enabled = from != "auto") {
                Icon(Icons.Default.SwapHoriz, "Swap")
            }
            Column(Modifier.weight(1f)) {
                Choice("Into", to, cat.languages.map { it.code to it.label }) { to = it }
            }
        }
        notes.forEach { Hint(it, warning = true) }
        CheckRow("Use the preceding text as context (Hy-MT2 and Qwen)", context_, enabled = !m.key.startsWith("translategemma")) { context_ = it }
        if (isTxt) CheckRow("Also save as PDF", pdf) { pdf = it }
        if (missing > 0) Hint("First use downloads ${formatBytes(missing)}.")

        Button(
            onClick = { vm.startTranslation(model, from, to, context_, pdf && isTxt) },
            enabled = (doc != null || pasted.isNotBlank()) && active == null && from != to,
            modifier = Modifier.fillMaxWidth(),
        ) { Text("Translate") }
        if (active != null && active.kind != JobKind.TRANSLATE) Hint("Another job is running (see below).")

        (active ?: last)?.let { j ->
            JobCard(j) { vm.cancel(j.id) }
            if (!j.active && j.kind == JobKind.TRANSLATE && j.status == JobStatus.DONE) {
                val text = vm.jobText(j, "shown.txt")
                FilesRow(context, j.files)
                if (text.isNotEmpty()) OutlinedButton(onClick = { shareText(context, text) }) { Text("Share text") }
                TextResult(if (j.spec.optString("format") == "srt") "Translated subtitles" else "Translation", text)
            }
        }
    }
}
