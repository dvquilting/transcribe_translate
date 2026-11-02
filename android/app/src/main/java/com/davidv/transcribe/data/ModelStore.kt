package com.davidv.transcribe.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.NetworkCapabilities
import android.os.StatFs
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL

/**
 * The downloaded model files, in the app's private storage (removed when the
 * app is uninstalled). Downloads resume where they stopped (a .part file) and
 * only become the model file when complete.
 */
class ModelStore(private val context: Context) {
    val dir: File = File(context.filesDir, "models").apply { mkdirs() }

    fun fileOf(name: String) = File(dir, name)
    fun isDownloaded(name: String) = fileOf(name).isFile
    fun bytesOnDisk(name: String): Long = fileOf(name).takeIf { it.isFile }?.length() ?: 0L
    fun partialBytes(name: String): Long = File(dir, "$name.part").takeIf { it.isFile }?.length() ?: 0L
    fun freeBytes(): Long = StatFs(dir.path).availableBytes

    fun delete(name: String) {
        fileOf(name).delete()
        File(dir, "$name.part").delete()
    }

    fun totalBytes(): Long = dir.listFiles()?.sumOf { it.length() } ?: 0L

    /** True on Wi-Fi, Ethernet or another network that isn't charged by the amount of data. */
    fun unmetered(): Boolean {
        val cm = context.getSystemService(ConnectivityManager::class.java) ?: return false
        val caps = cm.getNetworkCapabilities(cm.activeNetwork) ?: return false
        return caps.hasCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED)
    }

    /**
     * Download [url] to the model file [name] (if it isn't there yet) and return it.
     * [progress] gets bytes done and the total (0 = unknown). Throws IOException.
     */
    fun download(
        name: String, url: String, display: String, allowMobileData: Boolean,
        progress: (Long, Long) -> Unit, cancelled: () -> Boolean,
    ): File {
        val target = fileOf(name)
        if (target.isFile) return target
        if (!allowMobileData && !unmetered())
            throw IOException("$display needs to be downloaded first. Connect to Wi-Fi, or allow downloads over mobile data in Settings.")
        val part = File(dir, "$name.part")
        var redirects = 0
        var address = URL(url)
        while (true) {
            val have = if (part.isFile) part.length() else 0L
            val c = address.openConnection() as HttpURLConnection
            c.instanceFollowRedirects = false  // followed below, so the Range header goes along
            c.connectTimeout = 20_000
            c.readTimeout = 60_000
            c.setRequestProperty("User-Agent", "TranscribeTranslate-Android")
            if (have > 0) c.setRequestProperty("Range", "bytes=$have-")
            val code = c.responseCode
            if (code in 300..399) {
                val loc = c.getHeaderField("Location") ?: throw IOException("download of $display: redirect without a target")
                c.disconnect()
                if (++redirects > 10) throw IOException("download of $display: too many redirects")
                address = URL(address, loc)
                continue
            }
            if (code == 416 && have > 0) {  // the .part is already complete (or the server disagrees): start over
                c.disconnect()
                part.delete()
                continue
            }
            if (code != 200 && code != 206) {
                c.disconnect()
                throw IOException("download of $display failed (HTTP $code)")
            }
            val resume = code == 206
            val length = c.getHeaderFieldLong("Content-Length", -1L)
            val total = if (length < 0) 0L else if (resume) have + length else length
            if (total > 0 && freeBytes() < total - (if (resume) have else 0) + 200L * 1024 * 1024)
                throw IOException("not enough free space for $display (${total / 1_000_000} MB)")
            RandomAccessFile(part, "rw").use { out ->
                if (resume) out.seek(have) else out.setLength(0)
                var done = if (resume) have else 0L
                c.inputStream.use { input ->
                    val buf = ByteArray(1 shl 16)
                    var last = 0L
                    while (true) {
                        if (cancelled()) throw IOException("cancelled")
                        val n = input.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        done += n
                        if (done - last > 2_000_000) {
                            last = done
                            progress(done, total)
                        }
                    }
                }
                progress(done, total)
                if (total > 0 && done != total) throw IOException("download of $display was cut off")
            }
            c.disconnect()
            if (!part.renameTo(target)) throw IOException("could not save $display")
            return target
        }
    }
}

/** "1.1 GB" -> bytes (for space estimates). */
fun parseSize(s: String): Long {
    val m = Regex("""([\d.]+)\s*(KB|MB|GB)""", RegexOption.IGNORE_CASE).find(s) ?: return 0
    val v = m.groupValues[1].toDoubleOrNull() ?: return 0
    return (v * when (m.groupValues[2].uppercase()) { "GB" -> 1e9; "MB" -> 1e6; else -> 1e3 }).toLong()
}

fun formatBytes(b: Long): String = when {
    b >= 1_000_000_000 -> "%.1f GB".format(b / 1e9)
    b >= 1_000_000 -> "%.0f MB".format(b / 1e6)
    else -> "%.0f KB".format(b / 1e3)
}
