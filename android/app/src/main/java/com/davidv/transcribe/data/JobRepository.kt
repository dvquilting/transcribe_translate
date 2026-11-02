package com.davidv.transcribe.data

import android.app.ActivityManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import androidx.core.content.ContextCompat
import com.davidv.transcribe.engine.EngineService
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

/**
 * The app's view of the jobs: the list (newest first), the running one and its
 * log, kept up to date from the engine service's broadcasts.
 */
class JobRepository(private val context: Context) {
    val store = JobStore(context)
    private val _jobs = MutableStateFlow(store.list())
    val jobs: StateFlow<List<Job>> = _jobs
    private val _log = MutableStateFlow("")
    /** The log of the newest job (the one being watched). */
    val log: StateFlow<String> = _log
    private var logId: String? = null

    private val receiver = object : BroadcastReceiver() {
        override fun onReceive(c: Context, intent: Intent) {
            val id = intent.getStringExtra(EngineService.EXTRA_ID) ?: return
            val line = intent.getStringExtra(EngineService.EXTRA_LOG)
            if (line != null) {
                if (id != logId) { logId = id; _log.value = store.log(id) } else _log.value = _log.value + line + "\n"
            } else {
                refresh(id)
            }
        }
    }

    init {
        ContextCompat.registerReceiver(context, receiver, IntentFilter(EngineService.ACTION_EVENT), ContextCompat.RECEIVER_NOT_EXPORTED)
        checkStale()
    }

    fun active(): Job? = _jobs.value.firstOrNull { it.active }

    fun watchLog(id: String) {
        logId = id
        _log.value = store.log(id)
    }

    fun refresh(id: String? = null) {
        if (id == null) {
            _jobs.value = store.list()
            return
        }
        val j = store.load(id) ?: return
        val list = _jobs.value.toMutableList()
        val i = list.indexOfFirst { it.id == id }
        if (i >= 0) list[i] = j else list.add(0, j)
        _jobs.value = list
        if (!j.active) checkStale()
    }

    fun delete(id: String) {
        store.delete(id)
        _jobs.value = _jobs.value.filter { it.id != id }
    }

    /**
     * A job still "running" when the engine's process is gone (the phone ran out
     * of memory, or the app was force-stopped): it failed.
     */
    fun checkStale() {
        val am = context.getSystemService(ActivityManager::class.java)
        val engineAlive = am.runningAppProcesses?.any { it.processName.endsWith(":engine") } == true
        var changed = false
        for (j in store.list().filter { it.active }) {
            if (engineAlive && System.currentTimeMillis() - j.heartbeat < 5 * 60_000) continue
            store.save(j.copy(status = JobStatus.FAILED, finished = System.currentTimeMillis(),
                message = "The job stopped unexpectedly (the phone may have run out of memory; try a smaller model)."))
            changed = true
        }
        if (changed) _jobs.value = store.list()
    }
}
