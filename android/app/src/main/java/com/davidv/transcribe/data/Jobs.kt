package com.davidv.transcribe.data

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID

enum class JobKind { TRANSCRIBE, TRANSLATE, DOWNLOAD }
enum class JobStatus { RUNNING, DONE, FAILED, CANCELLED }

/** A saved result file: where the user can find it (a content URI) and its name. */
data class OutputFile(val name: String, val uri: String, val mime: String)

/**
 * One transcription, translation or model download. Written by the engine
 * service (its own process) to jobs/<id>/job.json; the app reads it, so a job
 * survives the app being closed. `spec` is what the user asked for.
 */
data class Job(
    val id: String,
    val kind: JobKind,
    val title: String,
    val created: Long,
    val spec: JSONObject,
    val status: JobStatus = JobStatus.RUNNING,
    val message: String = "",          // the current step, or the error
    val phase: String = "",            // progress: "decode", "download", "transcribe", ...
    val done: Long = 0,
    val total: Long = 0,
    val unit: String = "",
    val heartbeat: Long = 0,           // last sign of life from the engine (ms)
    val finished: Long = 0,
    val audioSeconds: Double = 0.0,
    val seconds: Double = 0.0,         // time the job took
    val files: List<OutputFile> = emptyList(),
    val warning: String = "",          // e.g. the corrected copy failed, but the transcript is saved
) {
    val active: Boolean get() = status == JobStatus.RUNNING

    fun toJson(): JSONObject = JSONObject().apply {
        put("id", id); put("kind", kind.name); put("title", title); put("created", created); put("spec", spec)
        put("status", status.name); put("message", message); put("phase", phase); put("done", done)
        put("total", total); put("unit", unit); put("heartbeat", heartbeat); put("finished", finished)
        put("audioSeconds", audioSeconds); put("seconds", seconds); put("warning", warning)
        put("files", JSONArray().apply {
            files.forEach { put(JSONObject().put("name", it.name).put("uri", it.uri).put("mime", it.mime)) }
        })
    }

    companion object {
        fun new(kind: JobKind, title: String, spec: JSONObject) =
            Job(UUID.randomUUID().toString(), kind, title, System.currentTimeMillis(), spec,
                heartbeat = System.currentTimeMillis(), message = "Starting...")

        fun fromJson(o: JSONObject) = Job(
            o.getString("id"), JobKind.valueOf(o.getString("kind")), o.getString("title"), o.getLong("created"),
            o.optJSONObject("spec") ?: JSONObject(), JobStatus.valueOf(o.getString("status")), o.optString("message"),
            o.optString("phase"), o.optLong("done"), o.optLong("total"), o.optString("unit"), o.optLong("heartbeat"),
            o.optLong("finished"), o.optDouble("audioSeconds", 0.0), o.optDouble("seconds", 0.0),
            (o.optJSONArray("files") ?: JSONArray()).objects()
                .map { OutputFile(it.getString("name"), it.getString("uri"), it.optString("mime")) },
            o.optString("warning"),
        )
    }
}

/** jobs/<id>/: job.json, log.txt, and the job's working files. */
class JobStore(context: Context) {
    val root: File = File(context.filesDir, "jobs").apply { mkdirs() }

    fun dir(id: String) = File(root, id).apply { mkdirs() }
    fun logFile(id: String) = File(dir(id), "log.txt")

    @Synchronized
    fun save(job: Job) {
        val d = dir(job.id)
        val tmp = File(d, "job.json.tmp")
        tmp.writeText(job.toJson().toString())
        tmp.renameTo(File(d, "job.json"))
    }

    fun load(id: String): Job? = try {
        File(File(root, id), "job.json").takeIf { it.isFile }?.let { Job.fromJson(JSONObject(it.readText())) }
    } catch (e: Exception) {
        null
    }

    /** Newest first. */
    fun list(): List<Job> =
        (root.listFiles() ?: emptyArray()).mapNotNull { load(it.name) }.sortedByDescending { it.created }

    fun delete(id: String) {
        File(root, id).deleteRecursively()
    }

    fun log(id: String): String = logFile(id).takeIf { it.isFile }?.readText() ?: ""

    /** The transcript or translation text the job saved in its folder (for display), or "". */
    fun text(id: String, name: String): String = File(File(dir(id), "out"), name).takeIf { it.isFile }?.readText() ?: ""
}
