package com.davidv.transcribe.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Translate
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
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
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import com.davidv.transcribe.data.Job
import com.davidv.transcribe.data.JobKind
import com.davidv.transcribe.data.JobStatus
import java.text.DateFormat
import java.util.Date

@Composable
fun HistoryScreen(vm: AppViewModel) {
    val jobs by vm.jobs.jobs.collectAsState()
    val openId by vm.openJob.collectAsState()
    val open = jobs.firstOrNull { it.id == openId }
    if (open != null) {
        JobDetail(vm, open) { vm.openJob.value = null }
        return
    }
    if (jobs.isEmpty()) {
        Text("Transcriptions and translations appear here.", Modifier.padding(24.dp), style = MaterialTheme.typography.bodyLarge)
        return
    }
    LazyColumn(Modifier.fillMaxSize().padding(horizontal = 16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        items(jobs, key = { it.id }) { j ->
            Card(Modifier.fillMaxWidth().clickable { vm.openJob.value = j.id; vm.jobs.watchLog(j.id) }) {
                Row(Modifier.padding(12.dp), verticalAlignment = Alignment.CenterVertically) {
                    Icon(when (j.kind) {
                        JobKind.TRANSCRIBE -> Icons.Default.Mic
                        JobKind.TRANSLATE -> Icons.Default.Translate
                        JobKind.DOWNLOAD -> Icons.Default.Download
                    }, null)
                    Column(Modifier.weight(1f).padding(start = 12.dp)) {
                        Text(j.title, style = MaterialTheme.typography.titleSmall, maxLines = 1)
                        Text(DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(j.created)) +
                            "  ·  " + statusText(j), style = MaterialTheme.typography.bodySmall,
                            color = if (j.status == JobStatus.FAILED) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant)
                    }
                }
            }
        }
    }
}

fun statusText(j: Job) = when (j.status) {
    JobStatus.RUNNING -> "running"
    JobStatus.DONE -> if (j.warning.isNotEmpty()) "done, with a problem" else "done"
    JobStatus.FAILED -> "failed"
    JobStatus.CANCELLED -> "cancelled"
}

@Composable
fun JobDetail(vm: AppViewModel, j: Job, onBack: () -> Unit) {
    val context = LocalContext.current
    val log by vm.jobs.log.collectAsState()
    var confirm by remember { mutableStateOf(false) }
    var showLog by remember { mutableStateOf(j.status == JobStatus.FAILED) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") }
            Text(j.title, Modifier.weight(1f), style = MaterialTheme.typography.titleMedium)
            IconButton(onClick = { confirm = true }, enabled = !j.active) { Icon(Icons.Default.Delete, "Delete") }
        }
        JobCard(j) { vm.cancel(j.id) }
        if (j.status == JobStatus.DONE) {
            FilesRow(context, j.files)
            when (j.kind) {
                JobKind.TRANSCRIBE -> {
                    val base = j.spec.optString("name", "transcript")
                    val text = vm.jobText(j, "$base.txt")
                    OutlinedButton(onClick = { vm.translateText(stripNote(vm.jobText(j, "$base.clean.txt").ifEmpty { text })) }) { Text("Translate…") }
                    TextResult("Corrected copy", stripNote(vm.jobText(j, "$base.clean.txt")))
                    TextResult("Transcript", text)
                }
                JobKind.TRANSLATE -> TextResult("Translation", vm.jobText(j, "shown.txt"))
                JobKind.DOWNLOAD -> {}
            }
        }
        TextButton(onClick = { showLog = !showLog }) { Text(if (showLog) "Hide the log" else "Show the log") }
        if (showLog) Text(log.ifEmpty { vm.jobs.store.log(j.id) }, style = MaterialTheme.typography.bodySmall.copy(fontFamily = FontFamily.Monospace))
    }
    if (confirm) AlertDialog(
        onDismissRequest = { confirm = false },
        title = { Text("Delete from the history?") },
        text = { Text("The files saved in Documents/Transcribe are kept.") },
        confirmButton = { TextButton(onClick = { confirm = false; vm.deleteJob(j.id) }) { Text("Delete") } },
        dismissButton = { TextButton(onClick = { confirm = false }) { Text("Cancel") } },
    )
}
