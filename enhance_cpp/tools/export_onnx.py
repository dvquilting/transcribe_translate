"""export_onnx.py - converts Resemble Enhance's PyTorch weights into the ONNX models the
C++ program runs (build time only; the program itself uses no Python).

    python export_onnx.py OUT_DIR

writes into OUT_DIR:
    denoiser.onnx   magnitude/phase (1, 841, T) -> separated magnitude/phase (the UNet denoiser)
    encoder.onnx    mel (1, 128, T)  -> latent (1, 64, T)          (IRMAE encoder)
    velocity.onnx   latent, mel, t   -> velocity (1, 64, T)        (CFM network, one ODE evaluation)
    decoder.onnx    latent (1, 64, T) -> features (1, 160, T)      (IRMAE decoder)
    vocoder.onnx    features (1, 160, N), noise (1, 128, N) -> waveform (1, 420 * N)  (UnivNet, unpadded)
    filters.bin     the mel filterbanks and constants (see write_filters)
and checks each model against PyTorch with ONNX Runtime.
"""

import argparse
import struct
import sys
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
from torch import nn

import re_model

OPSET = 17


class DenoiserNet(nn.Module):
    """Denoiser._predict + _separate (the STFT and inverse STFT are done in C++)."""

    def __init__(self, denoiser):
        super().__init__()
        self.d = denoiser

    def forward(self, mag, cos, sin):
        mag_mask, sin_res, cos_res = self.d._predict(mag, cos, sin)
        return self.d._separate(mag, cos, sin, mag_mask, cos_res, sin_res)


class Encoder(nn.Module):
    def __init__(self, ae):
        super().__init__()
        self.encoder = ae.encoder  # the Normalizer "estimator" only gathers training statistics

    def forward(self, mel):
        return self.encoder(mel)


class Decoder(nn.Module):
    def __init__(self, ae):
        super().__init__()
        self.decoder = ae.decoder

    def forward(self, z):
        return self.decoder(z)


class Velocity(nn.Module):
    """CFM._to_v: the velocity field at time t (shape [1]) for latent psi and condition mel."""

    def __init__(self, cfm):
        super().__init__()
        self.emb = cfm.emb
        self.net = cfm.net

    def forward(self, psi, cond, t):
        t = t.clamp(0, 1)
        g = self.emb(t)  # (1, 128)
        return self.net(psi, l=cond, g=g)


class Vocoder(nn.Module):
    """UnivNet.forward without its end padding: features (1, 160, N) and noise (1, 128, N)
    -> (1, 420 N). The C++ program pads 10 zero frames at the end as UnivNet.forward does
    and runs the vocoder in overlapping windows of frames (its receptive field is
    under 16 frames), which keeps memory small (a whole 30 s chunk needs ~3 GB per
    block for the location-variable kernels)."""

    def __init__(self, univnet):
        super().__init__()
        self.v = univnet

    def forward(self, x, noise):
        z = self.v.conv_pre(noise)
        for block in self.v.blocks:
            z = block(z, x)
        return self.v.conv_post(z).squeeze(1)


def original_vocoder(univnet, x, noise):
    """UnivNet.forward as the original runs it (npad = 10 frames), with the noise given."""
    npad = 10
    z = univnet.conv_pre(noise)
    xp = F.pad(x, (0, npad), "constant", 0)
    for block in univnet.blocks:
        z = block(z, xp)
    z = univnet.conv_post(z)
    return z[..., : -univnet.scale_factor * npad].squeeze(1)


def export(model, args, names_in, names_out, dynamic, path):
    torch.onnx.export(
        model,
        args,
        str(path),
        input_names=names_in,
        output_names=names_out,
        dynamic_axes=dynamic,
        opset_version=OPSET,
        do_constant_folding=True,
        dynamo=False,
    )


def check(path, model, feeds, limit=1e-3):
    """The exported model against PyTorch on `feeds`: max |difference| / max |output|."""
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    got = sess.run(None, {k: v.numpy() for k, v in feeds.items()})
    with torch.inference_mode():
        want = model(*feeds.values())
    if isinstance(want, torch.Tensor):
        want = (want,)
    worst = 0.0
    for g, w in zip(got, want):
        w = w.numpy()
        assert g.shape == w.shape, f"{path.name}: shape {g.shape} != {w.shape}"
        err = np.abs(g - w).max() / (np.abs(w).max() + 1e-9)
        worst = max(worst, float(err))
    print(f"  {path.name}: max relative difference to PyTorch {worst:.2e}", flush=True)
    if worst > limit:
        raise SystemExit(f"{path.name} does not match the PyTorch model")


def write_filters(enhancer, path):
    """filters.bin: little-endian; a header of int32/float32 constants, then the matrices.

    magic "REF1", version 1
    float32: normalizer mean, normalizer std, z_scale, time-mapping a
    int32 rows, cols + float32 data: mel filterbank (n_freqs=1025 x 128), torchaudio's
        (slaney scale and norm, 0..22050 Hz), for the enhancer's mel spectrogram
    int32 rows, cols + float32 data: mel filterbank (513 x 80, htk scale, no norm)
        for the chunk-alignment spectrogram (merge_chunks / compute_offset)
    """
    from torchaudio.transforms import MelSpectrogram

    norm = enhancer.normalizer
    mean = float(norm.running_mean.item())
    std = float(norm.running_std.item())
    z_scale = float(enhancer.lcfm.z_scale)
    a = re_model.time_mapping_a(enhancer.hp.cfm_time_mapping_divisor)
    fb_main = enhancer.mel_fn.melspec.mel_scale.fb.numpy().astype(np.float32)  # (1025, 128)
    sr = 44100
    hop = sr // 200
    win = hop * 4
    n_fft = 2 ** (win - 1).bit_length()
    align = MelSpectrogram(sample_rate=sr, n_fft=n_fft, win_length=win, hop_length=hop, n_mels=80,
                           f_min=0.0, f_max=sr // 2)
    fb_align = align.mel_scale.fb.numpy().astype(np.float32)  # (513, 80)
    with open(path, "wb") as f:
        f.write(b"REF1")
        f.write(struct.pack("<i", 1))
        f.write(struct.pack("<4f", mean, std, z_scale, a))
        for fb in (fb_main, fb_align):
            f.write(struct.pack("<2i", *fb.shape))
            f.write(np.ascontiguousarray(fb).tobytes())
    print(f"  filters.bin: mean {mean:.6f}, std {std:.6f}, z_scale {z_scale}, a {a:.9f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out_dir", type=Path)
    ap.add_argument("--only", nargs="*", default=None,
                    help="export only these models (denoiser encoder decoder velocity vocoder filters)")
    args = ap.parse_args()
    want = lambda name: args.only is None or name in args.only
    out = args.out_dir
    out.mkdir(parents=True, exist_ok=True)

    torch.manual_seed(0)
    print("Loading the Resemble Enhance weights ...", flush=True)
    enh = re_model.load_enhancer()

    # The vocoder, rewritten with the same results in operations ONNX Runtime and
    # DirectML support: the location-variable convolution as plain MatMuls, the
    # anti-aliasing filters with a fixed channel count, and the transposed
    # convolutions as zero insertion + convolution. Checked against the original
    # code: the vocoder's output for one input before the changes.
    from resemble_enhance.enhancer.univnet.lvcnet import LVCBlock

    probe = (torch.randn(1, 160, 23), torch.randn(1, 128, 33))
    with torch.inference_mode():
        original_wav = original_vocoder(enh.vocoder.eval(), *probe).numpy()
    LVCBlock.location_variable_convolution = re_model.lvc_matmul
    re_model.fixed_channel_filters(enh)
    re_model.replace_conv_transpose(enh.vocoder)

    print("Exporting ...", flush=True)
    T = 37  # an odd frame count: the UNet pads to a multiple of 16 itself
    F_bins = 841
    mel = torch.randn(1, 128, T)
    z = torch.randn(1, 64, T)
    if want("denoiser"):
        mag = torch.rand(1, F_bins, T) * 3
        phi = torch.rand(1, F_bins, T) * 6.28
        m = DenoiserNet(enh.denoiser).eval()
        p = out / "denoiser.onnx"
        export(m, (mag, phi.cos(), phi.sin()), ["mag", "cos", "sin"], ["sep_mag", "sep_cos", "sep_sin"],
               {n: {2: "frames"} for n in ["mag", "cos", "sin", "sep_mag", "sep_cos", "sep_sin"]}, p)
        # The phase output is normalized by sqrt(re^2 + im^2 + 1e-7) (Denoiser._magphase),
        # which magnifies float rounding where both are near zero: a looser limit.
        check(p, m, {"mag": torch.rand(1, F_bins, 53) * 3, "cos": torch.rand(1, F_bins, 53) * 2 - 1,
                     "sin": torch.rand(1, F_bins, 53) * 2 - 1}, limit=5e-3)

    if want("encoder"):
        m = Encoder(enh.lcfm.ae).eval()
        p = out / "encoder.onnx"
        export(m, (mel,), ["mel"], ["latent"], {"mel": {2: "frames"}, "latent": {2: "frames"}}, p)
        check(p, m, {"mel": torch.randn(1, 128, 61)})

    if want("decoder"):
        m = Decoder(enh.lcfm.ae).eval()
        p = out / "decoder.onnx"
        export(m, (z,), ["latent"], ["features"], {"latent": {2: "frames"}, "features": {2: "frames"}}, p)
        check(p, m, {"latent": torch.randn(1, 64, 61)})

    if want("velocity"):
        m = Velocity(enh.lcfm.cfm).eval()
        p = out / "velocity.onnx"
        export(m, (z, mel, torch.tensor([0.3])), ["psi", "cond", "t"], ["velocity"],
               {"psi": {2: "frames"}, "cond": {2: "frames"}, "velocity": {2: "frames"}}, p)
        check(p, m, {"psi": torch.randn(1, 64, 61), "cond": torch.randn(1, 128, 61), "t": torch.tensor([0.71])})

    if want("vocoder"):
        feats = torch.randn(1, 160, T)
        noise = torch.randn(1, 128, T)
        m = Vocoder(enh.vocoder).eval()
        p = out / "vocoder.onnx"
        export(m, (feats, noise), ["features", "noise"], ["wav"],
               {"features": {2: "frames"}, "noise": {2: "frames"}, "wav": {1: "samples"}}, p)
        padded = (F.pad(probe[0], (0, 10)), probe[1])
        check(p, m, {"features": padded[0], "noise": padded[1]})
        import onnxruntime as ort

        got = ort.InferenceSession(str(p), providers=["CPUExecutionProvider"]).run(
            None, {"features": padded[0].numpy(), "noise": padded[1].numpy()})[0][:, : 23 * 420]
        err = np.abs(got - original_wav).max() / (np.abs(original_wav).max() + 1e-9)
        print(f"  vocoder.onnx: max relative difference to the original vocoder code {err:.2e}", flush=True)
        if err > 1e-3:
            raise SystemExit("vocoder.onnx does not match the original vocoder")

    if want("filters"):
        write_filters(enh, out / "filters.bin")

    print(f"Done: {out}")


if __name__ == "__main__":
    sys.exit(main())
