package com.davidv.transcribe.audio

import android.annotation.SuppressLint
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.IBinder
import com.davidv.transcribe.MainActivity
import com.davidv.transcribe.R
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import java.io.File
import java.io.RandomAccessFile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.concurrent.thread
import kotlin.math.abs

/**
 * Records from the microphone into a 16 kHz mono WAV (what the speech models
 * use), as a foreground service so a long meeting can be recorded with the
 * screen off. Runs in the app's process; the screen shows [state].
 */
class RecorderService : Service() {
    data class State(val recording: Boolean = false, val seconds: Double = 0.0, val level: Float = 0f, val file: File? = null)

    @Volatile private var running = false

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> running = false
            else -> if (!running) startRecording()
        }
        return START_NOT_STICKY
    }

    @SuppressLint("MissingPermission")  // the screen asks for RECORD_AUDIO before starting this
    private fun startRecording() {
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Recording", NotificationManager.IMPORTANCE_LOW))
        val open = PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val stop = PendingIntent.getService(this, 1, Intent(this, RecorderService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE)
        val n = Notification.Builder(this, CHANNEL).setSmallIcon(R.drawable.ic_notification)
            .setContentTitle("Recording").setContentText("Tap to open; Stop ends the recording.")
            .setOngoing(true).setContentIntent(open)
            .addAction(Notification.Action.Builder(null, "Stop", stop).build()).build()
        startForeground(NOTIFY, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE)

        val dir = File(filesDir, "recordings").apply { mkdirs() }
        val name = "Recording " + SimpleDateFormat("yyyy-MM-dd HH.mm", Locale.getDefault()).format(Date()) + ".wav"
        val file = File(dir, name)
        val min = AudioRecord.getMinBufferSize(RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT)
        // VOICE_RECOGNITION: tuned for speech recognition (no aggressive noise suppression).
        val rec = AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, RATE, AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT, maxOf(min, RATE))
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            _state.value = State()
            stopSelf()
            return
        }
        running = true
        thread(name = "recorder") {
            var bytes = 0L
            RandomAccessFile(file, "rw").use { out ->
                out.setLength(0)
                out.write(ByteArray(44))  // the header, filled in at the end
                val buf = ShortArray(RATE / 10)
                val raw = ByteArray(buf.size * 2)
                rec.startRecording()
                _state.value = State(true, 0.0, 0f, file)
                while (running) {
                    val n = rec.read(buf, 0, buf.size)
                    if (n <= 0) continue
                    var peak = 0
                    for (i in 0 until n) {
                        val v = buf[i].toInt()
                        peak = maxOf(peak, abs(v))
                        raw[2 * i] = (v and 0xff).toByte()
                        raw[2 * i + 1] = (v shr 8 and 0xff).toByte()
                    }
                    out.write(raw, 0, 2 * n)
                    bytes += 2 * n
                    _state.value = State(true, bytes / (2.0 * RATE), peak / 32768f, file)
                }
                rec.stop()
                rec.release()
                writeHeader(out, bytes)
            }
            _state.value = State(false, bytes / (2.0 * RATE), 0f, file)
            stopForeground(STOP_FOREGROUND_REMOVE)
            stopSelf()
        }
    }

    private fun writeHeader(f: RandomAccessFile, data: Long) {
        fun le32(v: Long) = byteArrayOf(v.toByte(), (v shr 8).toByte(), (v shr 16).toByte(), (v shr 24).toByte())
        fun le16(v: Int) = byteArrayOf(v.toByte(), (v shr 8).toByte())
        f.seek(0)
        f.write("RIFF".toByteArray()); f.write(le32(36 + data)); f.write("WAVE".toByteArray())
        f.write("fmt ".toByteArray()); f.write(le32(16)); f.write(le16(1)); f.write(le16(1))
        f.write(le32(RATE.toLong())); f.write(le32(RATE * 2L)); f.write(le16(2)); f.write(le16(16))
        f.write("data".toByteArray()); f.write(le32(data))
    }

    companion object {
        const val RATE = 16000
        private const val CHANNEL = "recording"
        private const val NOTIFY = 3
        private const val ACTION_STOP = "com.davidv.transcribe.STOP_RECORDING"
        private val _state = MutableStateFlow(State())
        val state: StateFlow<State> = _state

        fun start(context: Context) = context.startForegroundService(Intent(context, RecorderService::class.java))
        fun stop(context: Context) =
            context.startService(Intent(context, RecorderService::class.java).setAction(ACTION_STOP))
    }
}
