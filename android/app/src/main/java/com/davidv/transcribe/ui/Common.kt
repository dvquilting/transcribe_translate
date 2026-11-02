package com.davidv.transcribe.ui

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuAnchorType
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import com.davidv.transcribe.data.Job
import com.davidv.transcribe.data.JobStatus
import com.davidv.transcribe.data.OutputFile
import java.io.File

/** A labelled drop-down menu of (value, text) choices. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun <T> Choice(label: String, value: T, options: List<Pair<T, String>>, enabled: Boolean = true, onChange: (T) -> Unit) {
    var open by remember { mutableStateOf(false) }
    ExposedDropdownMenuBox(expanded = open, onExpandedChange = { if (enabled) open = it }) {
        OutlinedTextField(
            value = options.firstOrNull { it.first == value }?.second ?: "",
            onValueChange = {}, readOnly = true, enabled = enabled, label = { Text(label) },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded = open) },
            modifier = Modifier.menuAnchor(MenuAnchorType.PrimaryNotEditable, enabled).fillMaxWidth(),
        )
        ExposedDropdownMenu(expanded = open, onDismissRequest = { open = false }) {
            options.forEach { (v, text) ->
                DropdownMenuItem(text = { Text(text) }, onClick = { onChange(v); open = false })
            }
        }
    }
}

@Composable
fun CheckRow(text: String, checked: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Checkbox(checked = checked, onCheckedChange = onChange, enabled = enabled)
        Text(text, style = MaterialTheme.typography.bodyMedium,
            color = if (enabled) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.onSurface.copy(alpha = 0.4f))
    }
}

@Composable
fun Hint(text: String, warning: Boolean = false) {
    if (text.isEmpty()) return
    Text(text, style = MaterialTheme.typography.bodySmall,
        color = if (warning) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
}

/** The running (or last) job: its status, progress bar and Cancel. */
@Composable
fun JobCard(job: Job, onCancel: () -> Unit) {
    Card(Modifier.fillMaxWidth(), colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(job.title, style = MaterialTheme.typography.titleSmall, modifier = Modifier.weight(1f))
                if (job.active) TextButton(onClick = onCancel) { Icon(Icons.Default.Close, null); Text("Cancel") }
            }
            Text(job.message, style = MaterialTheme.typography.bodyMedium)
            if (job.active) {
                if (job.total > 0) {
                    LinearProgressIndicator(progress = { (job.done.toFloat() / job.total).coerceIn(0f, 1f) }, Modifier.fillMaxWidth())
                    Text(progressText(job), style = MaterialTheme.typography.bodySmall)
                } else {
                    LinearProgressIndicator(Modifier.fillMaxWidth())
                }
            } else if (job.status == JobStatus.DONE && job.seconds > 0) {
                val speed = if (job.audioSeconds > 0) " (%.1f× real time)".format(job.audioSeconds / job.seconds) else ""
                Text("Took ${duration(job.seconds)}$speed", style = MaterialTheme.typography.bodySmall)
            }
            if (job.warning.isNotEmpty()) Hint(job.warning, warning = true)
        }
    }
}

fun progressText(j: Job): String = when (j.phase) {
    "decode" -> "Decoding: ${j.done / 10}%"
    "download" -> if (j.total > 0) "Downloading: ${j.done} of ${j.total} MB" else "Downloading: ${j.done} MB"
    "transcribe" -> if (j.unit == "s") "Transcribing: ${duration(j.done.toDouble())} of ${duration(j.total.toDouble())}"
                    else "Transcribing: part ${j.done} of ${j.total}"
    "align" -> "Word timing: part ${j.done} of ${j.total}"
    "polish" -> "Correcting: section ${j.done} of ${j.total}"
    "translate" -> "Translating: part ${j.done} of ${j.total}"
    else -> "${j.done} of ${j.total} ${j.unit}"
}

fun duration(s: Double): String {
    val t = s.toLong()
    return if (t >= 3600) "%d:%02d:%02d".format(t / 3600, t / 60 % 60, t % 60) else "%d:%02d".format(t / 60, t % 60)
}

/** A long text the user can select and copy. */
@Composable
fun TextResult(title: String, text: String) {
    if (text.isEmpty()) return
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(8.dp))
            SelectionContainer {
                Text(text, style = MaterialTheme.typography.bodyMedium.copy(fontFamily = if (title.contains("subtitles", true)) FontFamily.Monospace else FontFamily.Default))
            }
        }
    }
}

/** Open / share buttons for a job's saved files. */
@Composable
fun FilesRow(context: Context, files: List<OutputFile>) {
    if (files.isEmpty()) return
    Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text("Saved files:", style = MaterialTheme.typography.bodySmall)
        files.forEach { f ->
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(f.name, Modifier.weight(1f), style = MaterialTheme.typography.bodyMedium)
                TextButton(onClick = { openFile(context, f) }) { Text("Open") }
            }
        }
        Row {
            OutlinedButton(onClick = { shareFiles(context, files) }) { Text(if (files.size == 1) "Share" else "Share all") }
            Spacer(Modifier.width(8.dp))
        }
    }
}

/** A URI other apps may read (files kept only inside the app go through the FileProvider). */
fun shareableUri(context: Context, f: OutputFile): Uri {
    val u = Uri.parse(f.uri)
    return if (u.scheme == "file") FileProvider.getUriForFile(context, context.packageName + ".files", File(u.path!!)) else u
}

fun openFile(context: Context, f: OutputFile) {
    val i = Intent(Intent.ACTION_VIEW).setDataAndType(shareableUri(context, f), f.mime)
        .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(i)
    } catch (e: ActivityNotFoundException) {
        Toast.makeText(context, "No app on this phone opens ${f.name.substringAfterLast('.')} files.", Toast.LENGTH_LONG).show()
    }
}

fun shareFiles(context: Context, files: List<OutputFile>) {
    val uris = files.map { shareableUri(context, it) }
    val i = if (uris.size == 1) Intent(Intent.ACTION_SEND).setType(files[0].mime).putExtra(Intent.EXTRA_STREAM, uris[0])
    else Intent(Intent.ACTION_SEND_MULTIPLE).setType("*/*").putParcelableArrayListExtra(Intent.EXTRA_STREAM, ArrayList(uris))
    i.clipData = ClipData.newRawUri("", uris[0]).apply { uris.drop(1).forEach { addItem(ClipData.Item(it)) } }
    i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    context.startActivity(Intent.createChooser(i, "Share").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}

fun shareText(context: Context, text: String) {
    val i = Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, text)
    context.startActivity(Intent.createChooser(i, "Share").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
}
