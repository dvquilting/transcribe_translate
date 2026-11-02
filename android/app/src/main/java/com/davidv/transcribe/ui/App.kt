package com.davidv.transcribe.ui

import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.History
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.Storage
import androidx.compose.material.icons.filled.Translate
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun App(vm: AppViewModel) {
    val context = LocalContext.current
    val dark = isSystemInDarkTheme()
    // The phone's own colours (Material You) from Android 12 on.
    val colors = when {
        Build.VERSION.SDK_INT >= 31 -> if (dark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        dark -> darkColorScheme()
        else -> lightColorScheme()
    }
    MaterialTheme(colorScheme = colors) {
        val tab by vm.tab.collectAsState()
        Scaffold(
            topBar = { TopAppBar(title = { Text(when (tab) {
                Tab.TRANSCRIBE -> "Transcribe"
                Tab.TRANSLATE -> "Translate"
                Tab.HISTORY -> "History"
                Tab.MODELS -> "Models and settings"
            }) }) },
            bottomBar = {
                NavigationBar {
                    listOf(
                        Triple(Tab.TRANSCRIBE, Icons.Default.Mic, "Transcribe"),
                        Triple(Tab.TRANSLATE, Icons.Default.Translate, "Translate"),
                        Triple(Tab.HISTORY, Icons.Default.History, "History"),
                        Triple(Tab.MODELS, Icons.Default.Storage, "Models"),
                    ).forEach { (t, icon, label) ->
                        NavigationBarItem(selected = tab == t, onClick = { vm.tab.value = t }, icon = { Icon(icon, null) },
                            label = { Text(label) })
                    }
                }
            },
        ) { padding ->
            Box(Modifier.padding(padding)) {
                when (tab) {
                    Tab.TRANSCRIBE -> TranscribeScreen(vm)
                    Tab.TRANSLATE -> TranslateScreen(vm)
                    Tab.HISTORY -> HistoryScreen(vm)
                    Tab.MODELS -> ModelsScreen(vm)
                }
            }
        }
    }
}
