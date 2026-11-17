"""reference.py - the original Resemble Enhance pipeline (PyTorch, CPU), for testing the C++ port.

    python reference.py IN.wav OUT.wav [--denoise-only] [--nfe 64] [--solver midpoint]
                        [--lambd 0.5] [--tau 0.5] [--chunk-seconds 30] [--seed 0]

Runs resemble_enhance.inference.inference() unchanged, except that its random numbers
(the CFM prior and the vocoder's noise) come from the same generator as the C++
program's (re_model.NoiseGen, seeded with --seed), so the two outputs can be compared.
Writes OUT.wav as 32-bit float at 44.1 kHz.
"""

import argparse

import numpy as np
import soundfile as sf
import torch

import re_model


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--denoise-only", action="store_true")
    ap.add_argument("--nfe", type=int, default=64)
    ap.add_argument("--solver", default="midpoint")
    ap.add_argument("--lambd", type=float, default=0.5)
    ap.add_argument("--tau", type=float, default=0.5)
    ap.add_argument("--chunk-seconds", type=float, default=30.0)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    gen = re_model.NoiseGen(args.seed)

    def randn(*size, **kw):
        if len(size) == 1 and isinstance(size[0], (tuple, list, torch.Size)):
            size = tuple(size[0])
        n = int(np.prod(size))
        return torch.from_numpy(gen.normal(n).reshape(size))

    def randn_like(x, **kw):
        return randn(*x.shape).to(x.dtype)

    torch.randn = randn
    torch.randn_like = randn_like

    enhancer = re_model.load_enhancer()
    from resemble_enhance.inference import inference

    data, sr = sf.read(args.input, dtype="float32", always_2d=True)
    dwav = torch.from_numpy(data.T.copy()).mean(0)

    with torch.inference_mode():
        if args.denoise_only:
            hwav, sr = inference(enhancer.denoiser, dwav, sr, "cpu", chunk_seconds=args.chunk_seconds)
        else:
            enhancer.configurate_(nfe=args.nfe, solver=args.solver, lambd=args.lambd, tau=args.tau)
            hwav, sr = inference(enhancer, dwav, sr, "cpu", chunk_seconds=args.chunk_seconds)

    sf.write(args.output, hwav.numpy().astype(np.float32), sr, subtype="FLOAT")
    print(f"Wrote {args.output}: {hwav.shape[-1] / sr:.2f} s at {sr} Hz")


if __name__ == "__main__":
    main()
