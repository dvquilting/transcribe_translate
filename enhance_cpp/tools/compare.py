"""compare.py - how close two audio files are (the C++ output against reference.py's).

    python compare.py A.wav B.wav

Prints the length, the peak and RMS difference relative to B's RMS, and the
signal-to-difference ratio in dB (higher is closer; 60 dB = differences 1000x quieter).
"""

import sys

import numpy as np
import soundfile as sf


def main():
    a, ra = sf.read(sys.argv[1], dtype="float64")
    b, rb = sf.read(sys.argv[2], dtype="float64")
    if a.ndim > 1:
        a = a.mean(1)
    if b.ndim > 1:
        b = b.mean(1)
    print(f"A: {len(a)} samples at {ra} Hz, B: {len(b)} samples at {rb} Hz")
    n = min(len(a), len(b))
    d = a[:n] - b[:n]
    rms_b = np.sqrt(np.mean(b[:n] ** 2)) + 1e-12
    rms_d = np.sqrt(np.mean(d**2)) + 1e-15
    print(f"peak |A-B| {np.abs(d).max():.3e}, rms(A-B)/rms(B) {rms_d / rms_b:.3e}, "
          f"signal-to-difference {20 * np.log10(rms_b / rms_d):.1f} dB")
    sec = 44100
    worst = max(range(0, n, sec), key=lambda s: np.sqrt(np.mean(d[s:s + sec] ** 2)))
    print(f"worst second starts at {worst / ra:.0f} s: rms difference {np.sqrt(np.mean(d[worst:worst + sec] ** 2)):.3e}")

    # Log-spectral distance: the spectrograms' difference in dB (what is heard; a
    # generative vocoder's waveform can differ in phase where it sounds the same).
    def spec_db(x):
        w = np.hanning(2048)
        frames = [np.abs(np.fft.rfft(x[i : i + 2048] * w)) for i in range(0, len(x) - 2048, 512)]
        return 20 * np.log10(np.maximum(np.array(frames), 1e-5))

    sa, sb = spec_db(a[:n]), spec_db(b[:n])
    loud = sb > sb.max() - 60  # bins within 60 dB of the loudest
    lsd = np.sqrt(np.mean((sa - sb) ** 2, axis=1)).mean()
    print(f"log-spectral distance {lsd:.2f} dB (all bins), {np.abs(sa - sb)[loud].mean():.2f} dB mean "
          f"over audible bins")
    return 0 if len(a) == len(b) else 1


if __name__ == "__main__":
    sys.exit(main())
