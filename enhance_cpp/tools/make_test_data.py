"""make_test_data.py - writes test_data/dsp/*.bin: inputs and the outputs PyTorch / torchaudio /
Resemble Enhance's own code give for them, for tests/test_dsp.cpp.

Each .bin file: int32 ndim, int32 dims[ndim], float32 data (row-major).
"""

import struct
import sys
from pathlib import Path

import numpy as np
import torch
from torchaudio.functional import resample

import re_model

OUT = Path(__file__).resolve().parent.parent / "test_data" / "dsp"


def save(name, a):
    a = np.ascontiguousarray(np.asarray(a, dtype=np.float32))
    with open(OUT / f"{name}.bin", "wb") as f:
        f.write(struct.pack("<i", a.ndim))
        f.write(struct.pack(f"<{a.ndim}i", *a.shape))
        f.write(a.tobytes())


def signal(n, sr, seed):
    rng = np.random.default_rng(seed)
    t = np.arange(n) / sr
    x = 0.4 * np.sin(2 * np.pi * 220 * t) + 0.2 * np.sin(2 * np.pi * 3100 * t * (1 + 0.1 * t))
    x += 0.05 * rng.standard_normal(n)
    return torch.from_numpy(x.astype(np.float32))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    enh = re_model.load_enhancer()
    from resemble_enhance.inference import compute_offset, merge_chunks

    rs = dict(lowpass_filter_width=64, rolloff=0.9475937167399596, resampling_method="sinc_interp_kaiser",
              beta=14.769656459379492)
    for sr in (48000, 22050, 16000):
        x = signal(int(sr * 0.7) + 3, sr, sr)
        save(f"resample_{sr}_in", x)
        save(f"resample_{sr}_out", resample(x, orig_freq=sr, new_freq=44100, **rs))

    x = signal(30011, 44100, 1)
    save("mel_in", x)
    with torch.inference_mode():
        save("mel_out", enh.normalizer(enh.to_mel(x[None]), update=False)[0])

        mag, cos, sin = enh.denoiser._stft(x[None])
        save("stft_mag", mag[0])
        save("stft_cos", cos[0])
        save("istft_out", enh.denoiser._istft(mag * 0.7, cos, sin)[0])

    a = signal(44100, 44100, 5)
    b = torch.roll(a, 1234)
    save("offset_a", a)
    save("offset_b", b)
    save("offset_out", np.array([compute_offset(a, b)]))

    chunks = [signal(n, 44100, 10 + i) for i, n in enumerate([3 * 44100, 3 * 44100, 2 * 44100 + 17])]
    for i, c in enumerate(chunks):
        save(f"merge_chunk{i}", c)
    save("merge_out", merge_chunks(chunks, 3 * 44100, 2 * 44100, sr=44100, length=6 * 44100 + 17))

    from resemble_enhance.enhancer.lcfm.cfm import Solver

    save("time_points_32", Solver.exponential_decay_mapping(np.linspace(0, 1, 33), n=4))

    from export_onnx import write_filters

    write_filters(enh, OUT / "filters.bin")

    g = re_model.NoiseGen(7)
    save("noise_7", np.concatenate([g.normal(11), g.normal(6)]))
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    sys.exit(main())
