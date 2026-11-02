package com.davidv.transcribe.audio

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.io.BufferedOutputStream
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Decode the sound of an audio or video file with Android's own decoders (the
 * formats the phone can play) into 16 kHz mono float32 - the raw file the
 * engine reads (engine_api.h: "pcm"). Streams: memory use doesn't grow with
 * the length of the file.
 */
object AudioDecoder {
    const val RATE = 16000

    /** Returns the length in seconds. [progress] gets 0..1. Throws IOException. */
    fun decode(context: Context, uri: Uri, dest: File, progress: (Float) -> Unit, cancelled: () -> Boolean): Double {
        val ex = MediaExtractor()
        try {
            ex.setDataSource(context, uri, null)
            val track = (0 until ex.trackCount).firstOrNull {
                ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
            } ?: throw IOException("the file has no sound track")
            ex.selectTrack(track)
            val format = ex.getTrackFormat(track)
            val mime = format.getString(MediaFormat.KEY_MIME)!!
            val durationUs = if (format.containsKey(MediaFormat.KEY_DURATION)) format.getLong(MediaFormat.KEY_DURATION) else 0L
            val codec = try {
                MediaCodec.createDecoderByType(mime)
            } catch (e: Exception) {
                throw IOException("this phone can't decode $mime audio")
            }
            var written = 0L
            try {
                codec.configure(format, null, null, 0)
                codec.start()
                var outRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                var channels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                var floatPcm = false
                var resampler: Resampler? = null
                val info = MediaCodec.BufferInfo()
                var inputDone = false
                var outputDone = false
                BufferedOutputStream(FileOutputStream(dest), 1 shl 20).use { out ->
                    val bytes = ByteBuffer.allocate(4 * 8192).order(ByteOrder.LITTLE_ENDIAN)
                    val write: (FloatArray, Int) -> Unit = { a, n ->
                        var i = 0
                        while (i < n) {
                            bytes.clear()
                            val m = minOf(n - i, 8192)
                            for (k in 0 until m) bytes.putFloat(a[i + k])
                            out.write(bytes.array(), 0, m * 4)
                            i += m
                        }
                        written += n
                    }
                    var mono = FloatArray(0)
                    while (!outputDone) {
                        if (cancelled()) throw IOException("cancelled")
                        if (!inputDone) {
                            val i = codec.dequeueInputBuffer(10_000)
                            if (i >= 0) {
                                val buf = codec.getInputBuffer(i)!!
                                val n = ex.readSampleData(buf, 0)
                                if (n < 0) {
                                    codec.queueInputBuffer(i, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                                    inputDone = true
                                } else {
                                    codec.queueInputBuffer(i, 0, n, ex.sampleTime, 0)
                                    if (durationUs > 0) progress((ex.sampleTime.toFloat() / durationUs).coerceIn(0f, 1f))
                                    ex.advance()
                                }
                            }
                        }
                        val o = codec.dequeueOutputBuffer(info, 10_000)
                        when {
                            o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                                val f = codec.outputFormat
                                outRate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                                channels = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                                floatPcm = f.containsKey(MediaFormat.KEY_PCM_ENCODING) &&
                                    f.getInteger(MediaFormat.KEY_PCM_ENCODING) == AudioFormat.ENCODING_PCM_FLOAT
                                resampler = null
                            }
                            o >= 0 -> {
                                val buf = codec.getOutputBuffer(o)!!.order(ByteOrder.LITTLE_ENDIAN)
                                buf.position(info.offset)
                                buf.limit(info.offset + info.size)
                                val frames = info.size / ((if (floatPcm) 4 else 2) * channels)
                                if (mono.size < frames) mono = FloatArray(frames)
                                if (floatPcm) {
                                    val fb = buf.asFloatBuffer()
                                    for (k in 0 until frames) {
                                        var s = 0f
                                        for (c in 0 until channels) s += fb.get()
                                        mono[k] = s / channels
                                    }
                                } else {
                                    val sb = buf.asShortBuffer()
                                    for (k in 0 until frames) {
                                        var s = 0
                                        for (c in 0 until channels) s += sb.get()
                                        mono[k] = s / (32768f * channels)
                                    }
                                }
                                codec.releaseOutputBuffer(o, false)
                                if (frames > 0) {
                                    val r = resampler ?: Resampler(outRate, RATE).also { resampler = it }
                                    r.process(mono, frames, write)
                                }
                                if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) {
                                    resampler?.flush(write)
                                    outputDone = true
                                }
                            }
                        }
                    }
                }
            } finally {
                try { codec.stop() } catch (_: Exception) {}
                codec.release()
            }
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
            ex.release()
        }
    }
}
