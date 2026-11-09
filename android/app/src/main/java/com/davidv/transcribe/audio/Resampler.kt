package com.davidv.transcribe.audio

import kotlin.math.PI
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.sin

/**
 * Streaming band-limited resampler (windowed sinc, Blackman window, 8 zero
 * crossings), for turning decoded audio of any rate into the 16 kHz the speech
 * models take. The kernel is tabulated at 512 fractional positions, so the cost
 * is one table lookup and multiply per tap.
 */
class Resampler(private val inRate: Int, private val outRate: Int) {
    private val step = inRate.toDouble() / outRate          // input samples per output sample
    private val cutoff = min(1.0, outRate.toDouble() / inRate) * 0.95  // below the new Nyquist
    private val half = ceil(ZERO_CROSSINGS / cutoff).toInt()   // taps on each side
    private val table: Array<FloatArray> = Array(PHASES + 1) { p ->
        val frac = p.toDouble() / PHASES
        FloatArray(2 * half) { k ->
            val x = (k - half + 1) - frac                       // distance from the output position
            val w = x / (half + 1)                                 // -1..1 over the window
            val win = if (w <= -1 || w >= 1) 0.0 else 0.42 + 0.5 * cos(PI * w) + 0.08 * cos(2 * PI * w)
            val s = if (x == 0.0) 1.0 else sin(PI * cutoff * x) / (PI * cutoff * x)
            (cutoff * s * win).toFloat()
        }
    }
    private var buf = FloatArray(1 shl 16)
    private var len = 0          // samples in buf
    private var bufStart = 0L    // input index of buf[0]
    private var next = 0.0       // input position of the next output sample

    val passThrough = inRate == outRate

    /** Feed input samples; the output produced so far goes to [out]. */
    fun process(input: FloatArray, count: Int, out: (FloatArray, Int) -> Unit) {
        if (passThrough) { out(input, count); return }
        if (len + count > buf.size) buf = buf.copyOf(maxOf(buf.size * 2, len + count))
        System.arraycopy(input, 0, buf, len, count)
        len += count
        drain(out, final = false)
    }

    /** The last output samples (the input's end is padded with silence). */
    fun flush(out: (FloatArray, Int) -> Unit) {
        if (passThrough) return
        val pad = 2 * half + 1
        if (len + pad > buf.size) buf = buf.copyOf(len + pad)
        java.util.Arrays.fill(buf, len, len + pad, 0f)
        len += pad
        drain(out, final = true)
    }

    private val res = FloatArray(4096)

    private fun drain(out: (FloatArray, Int) -> Unit, final: Boolean) {
        val taps = 2 * half
        var n = 0
        val end = bufStart + len - (if (final) half + 1 else 2 * half)  // last position with all its taps
        while (next <= end) {
            val base = floor(next).toLong()
            val frac = next - base
            val row = table[(frac * PHASES + 0.5).toInt()]
            var i = (base - half + 1 - bufStart).toInt()
            var acc = 0f
            if (i >= 0 && i + taps <= len) {  // the usual case: all taps inside the buffer
                for (k in 0 until taps) acc += buf[i + k] * row[k]
            } else {
                for (k in 0 until taps) {
                    if (i in 0 until len) acc += buf[i] * row[k]
                    i++
                }
            }
            res[n++] = acc
            if (n == res.size) { out(res, n); n = 0 }
            next += step
        }
        if (n > 0) out(res, n)
        // Drop input no longer needed (keep the taps the next output needs).
        val keepFrom = (floor(next).toLong() - half - bufStart).toInt().coerceIn(0, len)
        if (keepFrom > 0) {
            System.arraycopy(buf, keepFrom, buf, 0, len - keepFrom)
            len -= keepFrom
            bufStart += keepFrom
        }
    }

    companion object {
        private const val ZERO_CROSSINGS = 8
        private const val PHASES = 512
    }
}
