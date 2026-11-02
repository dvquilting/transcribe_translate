package com.davidv.transcribe

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.lifecycle.lifecycleScope
import com.davidv.transcribe.engine.EngineService
import com.davidv.transcribe.ui.App
import com.davidv.transcribe.ui.AppViewModel
import com.davidv.transcribe.ui.Tab
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class MainActivity : ComponentActivity() {
    private val vm: AppViewModel by viewModels()
    private val askNotifications = registerForActivityResult(ActivityResultContracts.RequestPermission()) {}

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        EngineService.createChannels(this)
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            askNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
        setContent { App(vm) }
        if (savedInstanceState == null) handle(intent)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handle(intent)
    }

    override fun onResume() {
        super.onResume()
        vm.jobs.refresh()
        vm.jobs.checkStale()
    }

    private fun handle(intent: Intent?) {
        intent ?: return
        intent.getStringExtra(EngineService.EXTRA_ID)?.let { id ->  // from a notification
            vm.openJob.value = id
            vm.jobs.watchLog(id)
            vm.tab.value = Tab.HISTORY
            return
        }
        if (intent.action != Intent.ACTION_SEND) return
        val type = intent.type ?: return
        if (type == "text/plain" && intent.hasExtra(Intent.EXTRA_TEXT)) {
            vm.translateText(intent.getStringExtra(Intent.EXTRA_TEXT) ?: "")
            return
        }
        @Suppress("DEPRECATION")
        val uri = intent.getParcelableExtra<Uri>(Intent.EXTRA_STREAM) ?: return
        val name = EngineService.displayName(this, uri)
        // The sharing app's permission ends with this screen: keep a copy.
        lifecycleScope.launch {
            val local = withContext(Dispatchers.IO) {
                try {
                    val dir = File(cacheDir, "shared").apply { mkdirs() }
                    dir.listFiles()?.filter { System.currentTimeMillis() - it.lastModified() > 7 * 86_400_000L }?.forEach { it.delete() }
                    val f = File(dir, name.replace('/', '_'))
                    contentResolver.openInputStream(uri)!!.use { i -> f.outputStream().use { i.copyTo(it) } }
                    Uri.fromFile(f)
                } catch (e: Exception) {
                    null
                }
            }
            if (local == null) Toast.makeText(this@MainActivity, "Could not read the shared file.", Toast.LENGTH_LONG).show()
            else vm.sharedFile(type, local, name)
        }
    }
}
