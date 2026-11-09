package com.davidv.transcribe.engine

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.Uri
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.os.Process
import android.provider.OpenableColumns
import com.davidv.transcribe.MainActivity
import com.davidv.transcribe.R
import com.davidv.transcribe.audio.AudioDecoder
import com.davidv.transcribe.data.Job
import com.davidv.transcribe.data.JobKind
import com.davidv.transcribe.data.JobStatus
import com.davidv.transcribe.data.JobStore
import com.davidv.transcribe.data.ModelStore
import com.davidv.transcribe.data.OutputFile
import com.davidv.transcribe.data.Settings
import com.davidv.transcribe.data.formatBytes
import com.davidv.transcribe.util.Exporter
import com.davidv.transcribe.util.PdfMaker
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/**
 * Runs one job at a time - a transcription, a translation or a model download -
 * as a foreground service with a progress notification, in a process of its
 * own (":engine", see AndroidManifest.xml). Separate from the app so that:
 *  - a job goes on when the app is closed or the screen is off;
 *  - Cancel always works: when the engine doesn't stop by itself within a few
 *    seconds (a model in the middle of a long step), the process is ended;
 *  - running out of memory ends the job, not the app.
 * The job's state is written to jobs/<id>/job.json and announced with a
 * broadcast to the app (JobRepository).
 */
class EngineService : Service() {
    private lateinit var store: JobStore
    private lateinit var models: ModelStore
    private lateinit var settings: Settings
    private var job: Job? = null
    private val cancel = AtomicBoolean(false)
    private var wake: PowerManager.WakeLock? = null
    private var lastSave = 0L
    private var lastNotify = 0L
    private val logLock = Any()

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        store = JobStore(this)
        models = ModelStore(this)
        settings = Settings(this)
        createChannels(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_CANCEL -> cancelJob(intent.getStringExtra(EXTRA_ID))
            ACTION_START -> {
                val j = intent.getStringExtra(EXTRA_JOB)?.let { Job.fromJson(JSONObject(it)) } ?: return START_NOT_STICKY
                if (job != null) {  // one at a time (the app doesn't offer a second)
                    store.save(j.copy(status = JobStatus.FAILED, message = "Another job is running.", finished = now()))
                    announce(j.id)
                    return START_NOT_STICKY
                }
                start(j)
            }
        }
        return START_NOT_STICKY
    }

    private fun start(j: Job) {
        job = j
        cancel.set(false)
        store.save(j)
        val type = when {
            j.kind == JobKind.DOWNLOAD || Build.VERSION.SDK_INT < 35 -> ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
            else -> ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PROCESSING
        }
        startForeground(NOTIFY_JOB, notification(j), type)
        wake = getSystemService(PowerManager::class.java).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "transcribe:job")
            .apply { acquire(12 * 60 * 60 * 1000L) }
        thread(name = "engine-job") {
            val result = try {
                when (j.kind) {
                    JobKind.TRANSCRIBE -> transcribe(j)
                    JobKind.TRANSLATE -> translate(j)
                    JobKind.DOWNLOAD -> download(j)
                }
            } catch (e: Throwable) {
                current().copy(status = if (cancel.get()) JobStatus.CANCELLED else JobStatus.FAILED,
                    message = if (cancel.get()) "Cancelled." else (e.message ?: e.javaClass.simpleName))
            }
            finish(result)
        }
    }

    private fun current(): Job = job!!

    // ---- the three kinds of job ----------------------------------------------------

    private fun transcribe(j: Job): Job {
        val s = j.spec
        val dir = store.dir(j.id)
        val out = File(dir, "out").apply { mkdirs() }
        val pcm = File(dir, "audio.raw")
        update(message = "Decoding the audio...", phase = "decode", done = 0, total = 1000, unit = "")
        val decodeStart = System.nanoTime()
        val seconds = try {
            AudioDecoder.decode(this, Uri.parse(s.getString("input")), pcm,
                { f -> update(phase = "decode", done = (f * 1000).toLong(), total = 1000) }, { cancel.get() })
        } catch (e: IOException) {
            pcm.delete()
            throw e
        }
        log("Audio : %d:%02d (%.1f s)".format((seconds / 60).toInt(), (seconds % 60).toInt(), seconds))
        val decodeSeconds = (System.nanoTime() - decodeStart) / 1e9
        log("Decoded in %.1f s (%.0fx real time)".format(decodeSeconds, seconds / maxOf(decodeSeconds, 1e-3)))
        set { it.copy(audioSeconds = seconds) }
        val formats = s.optJSONArray("formats")?.let { a -> (0 until a.length()).map { a.getString(it) } } ?: listOf("txt")
        val name = s.optString("name", "transcript")
        val request = JSONObject().apply {
            put("pcm", pcm.path); put("seconds", seconds); put("model", s.getString("model"))
            put("language", s.optString("language", "auto")); put("word_timing", s.optBoolean("word_timing"))
            put("polish", s.optBoolean("polish")); put("threads", settings.threads)
            put("out_dir", out.path); put("name", name); put("title", s.optString("title", name))
            put("formats", JSONArray(formats.filter { it != "pdf" }))
        }
        val r = JSONObject(NativeEngine.transcribe(request.toString(), listener))
        pcm.delete()
        if (!r.optBoolean("ok")) throw IOException(if (r.optBoolean("cancelled")) "Cancelled." else r.optString("error"))
        val files = (0 until r.getJSONArray("files").length()).map { File(r.getJSONArray("files").getString(it)) }.toMutableList()
        if ("pdf" in formats) {
            update(message = "Making the PDF...", phase = "pdf", done = 0, total = 0)
            for (txt in files.filter { it.name.endsWith(".txt") }.toList()) {
                val pdf = File(out, txt.name.removeSuffix(".txt") + ".pdf")
                PdfMaker.write(txt.readText(), s.optString("title", name), pdf)
                files += pdf
            }
        }
        // The .txt is always made (it's shown in the app); saved only when asked for.
        val keep = files.filter { f -> f.extension != "txt" || "txt" in formats }
        val saved = save(keep)
        return current().copy(status = JobStatus.DONE, message = "Done.", files = saved, seconds = r.optDouble("seconds"),
            warning = r.optString("polish_error").takeIf { it.isNotEmpty() }?.let { "The corrected copy failed: $it" } ?: "")
    }

    private fun translate(j: Job): Job {
        val s = j.spec
        val dir = store.dir(j.id)
        val out = File(dir, "out").apply { mkdirs() }
        // The input: text typed in the app, or a file (copied here: the engine reads paths).
        val format = s.optString("format", "txt")
        val input = File(dir, "input.$format")
        if (s.has("text")) input.writeText(s.getString("text"))
        else contentResolver.openInputStream(Uri.parse(s.getString("input")))!!.use { i ->
            FileOutputStream(input).use { i.copyTo(it) }
        }
        val name = s.optString("name", "translation")
        val result = File(out, "$name.$format")
        val request = JSONObject().apply {
            put("input", input.path); put("format", format); put("model", s.getString("model"))
            put("from", s.optString("from", "auto")); put("to", s.getString("to"))
            put("context", s.optBoolean("context", true)); put("threads", settings.threads); put("output", result.path)
        }
        val r = JSONObject(NativeEngine.translate(request.toString(), listener))
        if (!r.optBoolean("ok")) throw IOException(if (r.optBoolean("cancelled")) "Cancelled." else r.optString("error"))
        val files = mutableListOf(result)
        if (format != "docx") {
            // Shown in the app; for subtitles and Markdown the text is the file itself.
            File(out, "shown.txt").writeText(r.optString("text"))
            if (s.optBoolean("pdf") && format == "txt") {
                val pdf = File(out, "$name.pdf")
                PdfMaker.write(r.optString("text"), s.optString("title", name), pdf)
                files += pdf
            }
        }
        val problems = r.optInt("problems")
        return current().copy(status = JobStatus.DONE, message = "Done.", files = save(files), seconds = r.optDouble("seconds"),
            warning = if (problems > 0) "$problems part(s) couldn't be translated and were kept in the original language." else "")
    }

    private fun download(j: Job): Job {
        val files = j.spec.getJSONArray("files")
        for (i in 0 until files.length()) {
            val f = files.getJSONObject(i)
            listener.modelFile(f.getString("file"), f.getString("url"), f.getString("display"), f.optString("size"))
        }
        return current().copy(status = JobStatus.DONE, message = "Downloaded.")
    }

    /** Copy the results to Documents/Transcribe (or the chosen folder). */
    private fun save(files: List<File>): List<OutputFile> {
        update(message = "Saving...", phase = "save", done = 0, total = 0)
        return files.map { f ->
            try {
                Exporter.export(this, f, f.name, settings.outputTree)
            } catch (e: Exception) {
                log("Note: ${e.message}; kept in the app only")
                OutputFile(f.name, Uri.fromFile(f).toString(), Exporter.mimeOf(f.name))
            }
        }
    }

    // ---- what the engine reports ------------------------------------------------------

    private val listener = object : EngineListener {
        override fun log(line: String) = this@EngineService.log(line)

        override fun progress(phase: String, done: Long, total: Long, unit: String) =
            update(phase = phase, done = done, total = total, unit = unit)

        override fun modelFile(file: String, url: String, display: String, size: String): String {
            if (models.isDownloaded(file)) return models.fileOf(file).path
            update(message = "Downloading $display ($size)...", phase = "download", done = 0, total = 0, unit = "MB")
            log("Downloading $display ($size)...")
            val f = models.download(file, url, display, settings.allowMobileData,
                { d, t -> update(phase = "download", done = d / 1_000_000, total = t / 1_000_000, unit = "MB") },
                { cancel.get() })
            log("Downloaded $display (${formatBytes(f.length())})")
            return f.path
        }
    }

    private fun log(line: String) {
        val j = job ?: return
        synchronized(logLock) { store.logFile(j.id).appendText(line + "\n") }
        // Status lines worth showing (the same as the desktop window shows).
        val t = line.trim()
        if (STATUS_PREFIXES.any { t.startsWith(it) }) update(message = t)
        sendBroadcast(Intent(ACTION_EVENT).setPackage(packageName).putExtra(EXTRA_ID, j.id).putExtra(EXTRA_LOG, line))
    }

    private fun update(message: String? = null, phase: String? = null, done: Long? = null, total: Long? = null, unit: String? = null) =
        set { it.copy(message = message ?: it.message, phase = phase ?: it.phase, done = done ?: it.done,
            total = total ?: it.total, unit = unit ?: it.unit) }

    @Synchronized
    private fun set(change: (Job) -> Job) {
        val j = job ?: return
        val n = change(j).copy(heartbeat = now())
        job = n
        val t = now()
        if (t - lastSave > 300 || n.message != j.message) {  // at most ~3 writes a second
            lastSave = t
            store.save(n)
            announce(n.id)
        }
        if (t - lastNotify > 1000) {
            lastNotify = t
            getSystemService(NotificationManager::class.java).notify(NOTIFY_JOB, notification(n))
        }
    }

    private fun announce(id: String) = sendBroadcast(Intent(ACTION_EVENT).setPackage(packageName).putExtra(EXTRA_ID, id))

    private fun finish(result: Job) {
        val done = result.copy(finished = now(), heartbeat = now(),
            message = if (result.status == JobStatus.DONE && result.warning.isNotEmpty()) "Done, with a problem." else result.message)
        job = done
        store.save(done)
        File(store.dir(done.id), "audio.raw").delete()
        if (done.status == JobStatus.CANCELLED) File(store.dir(done.id), "out").deleteRecursively()
        log(when (done.status) {
            JobStatus.DONE -> "Done."
            JobStatus.CANCELLED -> "[cancelled]"
            else -> "Error: ${done.message}"
        })
        announce(done.id)
        wake?.release()
        stopForeground(STOP_FOREGROUND_REMOVE)
        if (done.status != JobStatus.CANCELLED)
            getSystemService(NotificationManager::class.java).notify(NOTIFY_DONE, finishedNotification(done))
        job = null
        stopSelf()
    }

    private fun cancelJob(id: String?) {
        val j = job ?: return
        if (id != null && id != j.id) return
        cancel.set(true)
        NativeEngine.cancel()
        update(message = "Cancelling...")
        // A model in the middle of a long step can't stop at once: end the process.
        thread {
            Thread.sleep(4000)
            val still = job
            if (still != null && still.id == j.id) {
                store.save(still.copy(status = JobStatus.CANCELLED, message = "Cancelled.", finished = now()))
                File(store.dir(j.id), "out").deleteRecursively()
                File(store.dir(j.id), "audio.raw").delete()
                announce(j.id)
                getSystemService(NotificationManager::class.java).cancel(NOTIFY_JOB)
                Process.killProcess(Process.myPid())
            }
        }
    }

    // ---- notifications ------------------------------------------------------------------

    private fun notification(j: Job): Notification {
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java).putExtra(EXTRA_ID, j.id)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val stop = PendingIntent.getService(this, 1, Intent(this, EngineService::class.java).setAction(ACTION_CANCEL)
            .putExtra(EXTRA_ID, j.id), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        val b = Notification.Builder(this, CHANNEL_JOB)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(when (j.kind) {
                JobKind.TRANSCRIBE -> "Transcribing ${j.title}"
                JobKind.TRANSLATE -> "Translating ${j.title}"
                JobKind.DOWNLOAD -> "Downloading models"
            })
            .setContentText(j.message)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Cancel", stop).build())
        if (j.total > 0) b.setProgress(100, ((j.done * 100) / j.total).toInt().coerceIn(0, 100), false)
        else b.setProgress(0, 0, true)
        return b.build()
    }

    private fun finishedNotification(j: Job): Notification {
        val open = PendingIntent.getActivity(this, 2, Intent(this, MainActivity::class.java).putExtra(EXTRA_ID, j.id)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP), PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT)
        return Notification.Builder(this, CHANNEL_DONE)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(when {
                j.status != JobStatus.DONE -> "Failed: ${j.title}"
                j.kind == JobKind.TRANSCRIBE -> "Transcribed: ${j.title}"
                j.kind == JobKind.TRANSLATE -> "Translated: ${j.title}"
                else -> "Models downloaded"
            })
            .setContentText(if (j.status == JobStatus.DONE) j.warning.ifEmpty { "Tap to open." } else j.message)
            .setContentIntent(open)
            .setAutoCancel(true)
            .build()
    }

    companion object {
        const val ACTION_START = "com.davidv.transcribe.START"
        const val ACTION_CANCEL = "com.davidv.transcribe.CANCEL"
        const val ACTION_EVENT = "com.davidv.transcribe.JOB_EVENT"
        const val EXTRA_JOB = "job"
        const val EXTRA_ID = "id"
        const val EXTRA_LOG = "log"
        private const val CHANNEL_JOB = "jobs"
        private const val CHANNEL_DONE = "done"
        private const val NOTIFY_JOB = 1
        private const val NOTIFY_DONE = 2
        private val STATUS_PREFIXES = listOf("Loading", "Downloading", "Transcribing", "Translating", "Correcting",
            "Chunks:", "Punctuation:", "Detected:", "Note:", "Warning:", "Decoding", "Languages:")

        private fun now() = System.currentTimeMillis()

        fun createChannels(context: Context) {
            val nm = context.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(NotificationChannel(CHANNEL_JOB, "Running jobs", NotificationManager.IMPORTANCE_LOW))
            nm.createNotificationChannel(NotificationChannel(CHANNEL_DONE, "Finished jobs", NotificationManager.IMPORTANCE_DEFAULT))
        }

        fun start(context: Context, job: Job) {
            JobStore(context).save(job)
            context.startForegroundService(Intent(context, EngineService::class.java).setAction(ACTION_START)
                .putExtra(EXTRA_JOB, job.toJson().toString()))
        }

        fun cancel(context: Context, id: String) {
            context.startService(Intent(context, EngineService::class.java).setAction(ACTION_CANCEL).putExtra(EXTRA_ID, id))
        }

        /** A file's name as shown by its app (for titles and output names). */
        fun displayName(context: Context, uri: Uri): String {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
                if (c.moveToFirst()) c.getString(0)?.let { return it }
            }
            return uri.lastPathSegment?.substringAfterLast('/') ?: "audio"
        }
    }
}
