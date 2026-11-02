package com.davidv.transcribe.ui

import android.content.Intent
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Switch
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
import com.davidv.transcribe.data.ModelFile
import com.davidv.transcribe.data.formatBytes
import com.davidv.transcribe.util.Exporter

@Composable
fun ModelsScreen(vm: AppViewModel) {
    val context = LocalContext.current
    val jobs by vm.jobs.jobs.collectAsState()
    val version by vm.modelsVersion.collectAsState()
    val active = jobs.firstOrNull { it.active }
    // Re-read the disk when a download finishes or a file is deleted.
    val finishedDownloads = jobs.count { it.kind == JobKind.DOWNLOAD && !it.active }
    val files = remember(version, finishedDownloads, active?.id) { vm.catalog.allFiles }
    var deleting by remember { mutableStateOf<ModelFile?>(null) }
    var mobileData by remember { mutableStateOf(vm.settings.allowMobileData) }
    var threads by remember { mutableStateOf(vm.settings.threads) }
    var tree by remember { mutableStateOf(vm.settings.outputTree) }
    val chooseFolder = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri: Uri? ->
        if (uri != null) {
            context.contentResolver.takePersistableUriPermission(uri,
                Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION)
            vm.settings.outputTree = uri.toString()
            tree = uri.toString()
        }
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Models", style = MaterialTheme.typography.titleLarge)
        Text("${formatBytes(vm.models.totalBytes())} used · ${formatBytes(vm.models.freeBytes())} free on the phone",
            style = MaterialTheme.typography.bodySmall)
        if (active?.kind == JobKind.DOWNLOAD) JobCard(active) { vm.cancel(active.id) }
        files.forEach { f ->
            val have = vm.models.isDownloaded(f.file)
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(12.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("${f.display}  —  ${f.size}", Modifier.weight(1f), style = MaterialTheme.typography.titleSmall)
                        if (have) TextButton(onClick = { deleting = f }, enabled = active == null) { Text("Delete") }
                        else TextButton(onClick = { vm.download(listOf(f)) }, enabled = active == null) {
                            Text(if (vm.models.partialBytes(f.file) > 0) "Resume" else "Download")
                        }
                    }
                    Text(f.description, style = MaterialTheme.typography.bodySmall)
                    if (have) Text("Downloaded (${formatBytes(vm.models.bytesOnDisk(f.file))})", style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary)
                }
            }
        }
        Text("Models download by themselves the first time they are needed. Downloading them here first lets you " +
            "choose when (on Wi-Fi).", style = MaterialTheme.typography.bodySmall)

        HorizontalDivider()
        Text("Settings", style = MaterialTheme.typography.titleLarge)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Download over mobile data")
                Hint("Off: models download on Wi-Fi only (they are 0.4–2.5 GB each).")
            }
            Switch(checked = mobileData, onCheckedChange = { mobileData = it; vm.settings.allowMobileData = it })
        }
        Choice("Processor threads", threads, listOf(0 to "Automatic (the fast cores)") + (2..8).map { it to "$it" }) {
            threads = it; vm.settings.threads = it
        }
        Hint("More threads use the slower cores too, which can make the models slower.")
        Column {
            Text("Save results in")
            Hint(if (tree == null) Exporter.FOLDER else Uri.parse(tree).lastPathSegment?.substringAfter(':') ?: "chosen folder")
            Row {
                OutlinedButton(onClick = { chooseFolder.launch(null) }) { Text("Choose folder") }
                if (tree != null) TextButton(onClick = { vm.settings.outputTree = null; tree = null }) { Text("Use Documents/Transcribe") }
            }
        }
        HorizontalDivider()
        Text("About", style = MaterialTheme.typography.titleLarge)
        Text("Everything runs on this phone; nothing is uploaded. Speech recognition by CrispASR (MIT), translation " +
            "and corrections by llama.cpp (MIT). Models: Parakeet (CC-BY-4.0, NVIDIA / moondream), Qwen3-ASR and " +
            "Qwen 3.5 (Apache-2.0, Alibaba), Hy-MT2 (Apache-2.0, Tencent), TranslateGemma (Gemma Terms of Use, Google).",
            style = MaterialTheme.typography.bodySmall)
    }
    deleting?.let { f ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text("Delete ${f.display}?") },
            text = { Text("It frees ${formatBytes(vm.models.bytesOnDisk(f.file))}; it downloads again when it is next needed.") },
            confirmButton = { TextButton(onClick = { vm.deleteModel(f.file); deleting = null }) { Text("Delete") } },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text("Cancel") } },
        )
    }
}
