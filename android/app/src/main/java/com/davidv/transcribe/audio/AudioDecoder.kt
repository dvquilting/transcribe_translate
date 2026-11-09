package com.davidv.transcribe.audio

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * Decode the sound of an audio or video file with Android's own decoders (the
 * formats the phone can play) into 16 kHz mono float32 - the raw file the
 * engine reads (engine_api.h: "pcm"). Streams: memory use doesn't grow with
 * the length of the file.
 *
 * The decoder runs in MediaCodec's asynchronous mode: it asks for input and
 * hands over output on its own thread as soon as it can, so it is never kept
 * waiting by a polling loop. (Decoders hold several buffers before they give
 * any back; a loop that feeds one buffer and then waits for output loses most
 * of its time in the wait.) The conversion to mono floats and the writing use
 * bulk copies rather than one value at a time.
 */
object AudioDecoder {
    const val RATE = 16000

    /** Returns the length in seconds. [progress] gets 0..1. Throws IOException. */
    fun decode(context: Context, uri: Uri, dest: File, progress: (Float) -> Unit, cancelled: () -> Boolean): Double {
        val ex = MediaExtractor()
        val thread = HandlerThread("audio-decoder").apply { start() }
        var codec: MediaCodec? = null
        try {
            ex.setDataSource(context, uri, null)
            val track = (0 until ex.trackCount).firstOrNull {
                ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: throw IOException("the file has no sound track")
            ex.selectTrack(track)
            val format = ex.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME)!!
            val durationUs = if (format.containsKey(MediaFormat.KEY_DURATION)) format.getLong(MediaFormat.KEY_DURATION) else 0L
            // Surround sound (a film's 5.1): the decoder's own stereo downmix keeps the
            // dialogue channel at full strength, and there is less to decode.
            format.setInteger("aac-max-output-channel_count", 2)
            if (Build.VERSION.SDK_INT >= 30) format.setInteger(MediaFormat.KEY_MAX_OUTPUT_CHANNEL_COUNT, 2)
            codec = try {
                MediaCodec.createDecoderByType(mime)
            } catch (e: Exception) {
                throw IOException("this phone can't decode $mime audio")
            }
            val sink = Sink(dest, format, durationUs, progress)
            val done = CountDownLatch(1)
            codec.setCallback(object : MediaCodec.Callback() {
                private var inputDone = false

                override fun onInputBufferAvailable(mc: MediaCodec, index: Int) {
                    if (inputDone || sink.stopped) return
                    try {
                        val buf = mc.getInputBuffer(index) ?: return
                        val n = ex.readSampleData(buf, 0)
                        if (n < 0) {
                            mc.queueInputBuffer(index, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputDone = true
                        } else {
                            val t = ex.sampleTime
                            mc.queueInputBuffer(index, 0, n, t, 0)
                            sink.inputAt(t)
                            ex.advance()
                        }
                    } catch (e: Exception) {
                        sink.fail(e); done.countDown()
                    }
                }

                override fun onOutputBufferAvailable(mc: MediaCodec, index: Int, info: MediaCodec.BufferInfo) {
                    if (sink.stopped) return
                    try {
                        val buf = mc.getOutputBuffer(index)
                        if (buf != null && info.size > 0) {
                            buf.position(info.offset)
                            buf.limit(info.offset + info.size)
                            sink.pcm(buf.slice().order(ByteOrder.nativeOrder()))
                        }
                        mc.releaseOutputBuffer(index, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                            sink.finish(); done.countDown()
                        }
                    } catch (e: Exception) {
                        sink.fail(e); done.countDown()
                    }
                }

                override fun onOutputFormatChanged(mc: MediaCodec, f: MediaFormat) = sink.format(f)

                override fun onError(mc: MediaCodec, e: MediaCodec.CodecException) {
                    sink.fail(e); done.countDown()
                }
            }, Handler(thread.looper))
            codec.configure(format, null, null, 0)
            codec.start()
            while (!done.await(100, TimeUnit.MILLISECONDS)) {
                if (cancelled()) {
                    sink.fail(IOException("cancelled"))
                    break
                }
            }
            try { codec.stop() } catch (_: Exception) {}
            // No callback runs after this: the file can be closed.
            thread.quitSafely()
            thread.join(2000)
            val written = sink.close()
            sink.error?.let { throw it }
            progress(1f)
            if (written == 0L) throw IOException("the sound track is empty")
            return written.toDouble() / RATE
        } catch (e: IOException) {
            dest.delete()
            throw e
        } catch (e: Exception) {
            dest.delete()
            throw IOException("could not decode the audio (${e.message ?: e.javaClass.simpleName})", e)
        } finally {
            codec?.release()
            thread.quitSafely()
            ex.release()
        }
    }

    /** Decoded PCM -> mono -> 16 kHz -> little-endian float32 file. Used on the decoder's thread. */
    private class Sink(dest: File, input: MediaFormat, private val durationUs: Long, private val progress: (Float) -> Unit) {
        @Volatile var stopped = false
        @Volatile var error: IOException? = null
        private val out = BufferedOutputStream(FileOutputStream(dest), 1 shl 20)
        private var rate = input.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        private var channels = input.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        private var floatPcm = false
        private var resampler: Resampler? = null
        private var mono = FloatArray(0)
        private var shorts = ShortArray(0)
        private var floats = FloatArray(0)
        private val bytes = ByteBuffer.allocate(4 * CHUNK).order(ByteOrder.LITTLE_ENDIAN)
        private val bytesAsFloats = bytes.asFloatBuffer()
        private var written = 0L
        private var lastPermille = -1
        private val write: (FloatArray, Int) -> Unit = { a, n ->
            var i = 0
            while (i < n) {
                val m = minOf(n - i, CHUNK)
                bytesAsFloats.clear()
                bytesAsFloats.put(a, i, m)
                out.write(bytes.array(), 0, m * 4)
                i += m
            }
            written += n
        }

        fun format(f: MediaFormat) {
            rate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            channels = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            floatPcm = f.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                f.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
            resampler = null
        }

        /** Report progress by the position of the input fed to the decoder (only when it moves 0.1 %). */
        fun inputAt(timeUs: Long) {
            if (durationUs <= 0) return
            val p = ((timeUs * 1000) / durationUs).toInt().coerceIn(0, 1000)
            if (p != lastPermille) {
                lastPermille = p
                progress(p / 1000f)
            }
        }

        fun pcm(buf: ByteBuffer) {
            val ch = maxOf(1, channels)
            val samples = buf.remaining() / (if (floatPcm) 4 else 2)
            val frames = samples / ch
            if (frames == 0) return
            if (mono.size < frames) mono = FloatArray(frames)
            if (floatPcm) {
                val fb = buf.asFloatBuffer()
                if (ch == 1) {
                    fb.get(mono, 0, frames)
                } else {
                    if (floats.size < frames * ch) floats = FloatArray(frames * ch)
                    fb.get(floats, 0, frames * ch)
                    downmix(floats, ch, frames)
                }
            } else {
                if (shorts.size < frames * ch) shorts = ShortArray(frames * ch)
                buf.asShortBuffer().get(shorts, 0, frames * ch)
                val scale = 1f / (32768f * ch)
                if (ch == 1) {
                    for (k in 0 until frames) mono[k] = shorts[k] * scale
                } else {
                    var j = 0
                    for (k in 0 until frames) {
                        var s = 0
                        for (c in 0 until ch) s += shorts[j++]
                        mono[k] = s * scale
                    }
                }
            }
            val r = resampler ?: Resampler(rate, RATE).also { resampler = it }
            r.process(mono, frames, write)
        }

        private fun downmix(src: FloatArray, ch: Int, frames: Int) {
            val scale = 1f / ch
            var j = 0
            for (k in 0 until frames) {
                var s = 0f
                for (c in 0 until ch) s += src[j++]
                mono[k] = s * scale
            }
        }

        fun finish() {
            resampler?.flush(write)
            stopped = true
        }

        fun fail(e: Exception) {
            if (error == null) error = e as? IOException ?: IOException(e.message ?: e.javaClass.simpleName, e)
            stopped = true
        }

        /** Close the file; the number of 16 kHz samples written. */
        fun close(): Long {
            try {
                out.close()
            } catch (e: IOException) {
                if (error == null) error = e
            }
            return written
        }
    }

    private const val CHUNK = 8192
}
