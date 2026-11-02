package com.davidv.transcribe

import com.davidv.transcribe.audio.Resampler
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin

class ResamplerTest {
    private fun run(inRate: Int, outRate: Int, freq: Double): FloatArray {
        val r = Resampler(inRate, outRate)
        val n = inRate / 2
        val x = FloatArray(n) { sin(2 * PI * freq * it / inRate).toFloat() }
        val out = ArrayList<Float>()
        val sink: (FloatArray, Int) -> Unit = { a, k -> for (i in 0 until k) out.add(a[i]) }
        var i = 0
        while (i < n) {  // in uneven pieces, as a decoder delivers them
            val k = minOf(1234, n - i)
            r.process(x.copyOfRange(i, i + k), k, sink)
            i += k
        }
        r.flush(sink)
        return out.toFloatArray()
    }

    private fun peak(a: FloatArray) = a.copyOfRange(a.size / 4, 3 * a.size / 4).maxOf { abs(it) }

    @Test fun keepsLengthAndLevel() {
        for ((rate, f) in listOf(48000 to 1000.0, 44100 to 3000.0, 22050 to 500.0, 8000 to 1000.0)) {
            val y = run(rate, 16000, f)
            assertEquals("length from $rate Hz", 8000.0, y.size.toDouble(), 30.0)
            assertEquals("level from $rate Hz", 1.0, peak(y).toDouble(), 0.03)
        }
    }

    @Test fun removesWhatCantBeRepresented() {
        // 12 kHz is above 16 kHz audio's 8 kHz limit: it must not fold back as a 4 kHz tone.
        assertTrue(peak(run(48000, 16000, 12000.0)) < 0.01f)
    }
}
