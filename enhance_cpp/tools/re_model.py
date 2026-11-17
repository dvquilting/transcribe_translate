"""re_model.py - loads Resemble Enhance's original PyTorch model for the build-time tools.

Used only by export_onnx.py and reference.py (never at run time). The model code is
Resemble AI's own, from third_party/resemble-enhance (downloaded by the build script);
its training-only imports (DeepSpeed) are stubbed, and the run's hparams.yaml (which
holds POSIX paths that cannot be loaded on Windows) is replaced by its values below.
"""

import math
import sys
import types
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
UPSTREAM = ROOT / "third_party" / "resemble-enhance"
RUN_DIR = HERE / "model_repo" / "enhancer_stage2"
CHECKPOINT = RUN_DIR / "ds" / "G" / "default" / "mp_rank_00_model_states.pt"
CHECKPOINT_URL = (
    "https://huggingface.co/ResembleAI/resemble-enhance/resolve/main/"
    "enhancer_stage2/ds/G/default/mp_rank_00_model_states.pt"
)


def _stub_deepspeed():
    """DeepSpeed is only needed for training; inference code imports it at module level."""
    if "deepspeed" in sys.modules:
        return

    class _Any:
        def __init__(self, *a, **k):
            pass

        def __call__(self, *a, **k):
            return _Any()

        def __getattr__(self, name):
            return _Any()

    def mod(name, **attrs):
        m = types.ModuleType(name)
        for k, v in attrs.items():
            setattr(m, k, v)
        sys.modules[name] = m
        return m

    ds = mod("deepspeed", DeepSpeedConfig=_Any, init_distributed=lambda *a, **k: None)
    ds.accelerator = mod("deepspeed.accelerator", get_accelerator=lambda: _Any())
    ds.runtime = mod("deepspeed.runtime")
    ds.runtime.engine = mod("deepspeed.runtime.engine", DeepSpeedEngine=_Any)
    ds.runtime.utils = mod("deepspeed.runtime.utils", clip_grad_norm_=lambda *a, **k: None)


def ensure_checkpoint():
    if CHECKPOINT.exists() and CHECKPOINT.stat().st_size > 100_000_000:
        return
    import urllib.request

    CHECKPOINT.parent.mkdir(parents=True, exist_ok=True)
    tmp = CHECKPOINT.with_suffix(".part")
    print(f"Downloading the Resemble Enhance weights (713 MB) from {CHECKPOINT_URL}", flush=True)
    urllib.request.urlretrieve(CHECKPOINT_URL, tmp)
    tmp.replace(CHECKPOINT)


def load_enhancer():
    """The original Enhancer with the stage-2 weights, weight norm removed, in eval mode (CPU)."""
    import torch

    _stub_deepspeed()
    sys.path.insert(0, str(UPSTREAM))
    # The training data pipeline (librosa, sox augmentation) is imported by the training
    # modules that also define the model classes; inference never uses it.
    data = types.ModuleType("resemble_enhance.data")
    data.create_dataloaders = data.mix_fg_bg = lambda *a, **k: None
    sys.modules["resemble_enhance.data"] = data
    from resemble_enhance.enhancer.enhancer import Enhancer
    from resemble_enhance.enhancer.hparams import HParams
    from resemble_enhance.inference import remove_weight_norm_recursively

    # enhancer_stage2/hparams.yaml (the model-relevant values).
    hp = HParams(
        wav_rate=44100,
        n_fft=2048,
        win_size=2048,
        hop_size=420,
        num_mels=128,
        stft_magnitude_min=1e-4,
        preemphasis=0.97,
        training_seconds=3.0,
        batch_size_per_gpu=32,
        cfm_solver_method="midpoint",
        cfm_solver_nfe=64,
        cfm_time_mapping_divisor=4,
        univnet_nc=96,
        lcfm_latent_dim=64,
        lcfm_training_mode="cfm",
        lcfm_z_scale=6,
        vocoder_extra_dim=32,
        gan_training_start_step=None,
        praat_augment_prob=0.2,
    )
    # Solver.exponential_decay_mapping does float(fsolve(...)), a 1-element array,
    # which NumPy 2.x rejects; the same computation, taking the element.
    from resemble_enhance.enhancer.lcfm.cfm import Solver

    def exponential_decay_mapping(t, n=4):
        a = time_mapping_a(n)
        return (a**t - 1) / (a - 1)

    Solver.exponential_decay_mapping = staticmethod(exponential_decay_mapping)

    ensure_checkpoint()
    enhancer = Enhancer(hp)
    state = torch.load(CHECKPOINT, map_location="cpu", weights_only=False)["module"]
    enhancer.load_state_dict(state)
    enhancer.eval()
    remove_weight_norm_recursively(enhancer)
    explicit_same_padding(enhancer)
    return enhancer


def explicit_same_padding(model):
    """padding="same" -> the equal numeric padding (all such kernels here are odd-sized).

    ONNX exports "same" as auto_pad SAME_UPPER, which ONNX Runtime rejects for dilated
    convolutions; explicit pads give the identical result."""
    from torch import nn

    for m in model.modules():
        if isinstance(m, nn.Conv1d) and m.padding == "same":
            k, d = m.kernel_size[0], m.dilation[0]
            assert k % 2 == 1, "even kernel with same padding"
            m.padding = (d * (k - 1) // 2,)


def lvc_matmul(self, x, kernel, bias, dilation=1, hop_size=256):
    """LVCBlock.location_variable_convolution without unfold/einsum (exports to plain MatMul).

    Same result as the original (which is always called with dilation=1):
      o[b, o, l*hop + s] = bias[b, o, l] + sum_{i,k} xpad[b, i, l*hop + s + k] * kernel[b, i, o, k, l]
    """
    import torch.nn.functional as F

    assert dilation == 1
    b, cin, length = x.shape
    _, _, cout, ksize, _ = kernel.shape
    pad = (ksize - 1) // 2
    xp = F.pad(x, (pad, pad), "constant", 0)
    out = None
    for k in range(ksize):
        xk = xp[:, :, k : k + length].reshape(b, cin, -1, hop_size)  # b i l s
        xk = xk.permute(0, 2, 3, 1)  # b l s i
        kk = kernel[:, :, :, k, :].permute(0, 3, 1, 2)  # b l i o
        o = xk @ kk  # b l s o
        out = o if out is None else out + o
    out = out + bias.permute(0, 2, 1).unsqueeze(2)  # + (b l 1 o)
    return out.permute(0, 3, 1, 2).reshape(b, cout, -1)  # b o (l s)


def fixed_channel_filters(model):
    """The vocoder's anti-aliasing filters expand their kernel to the input's channel
    count, read from the input's shape; the ONNX exporter needs it as a constant."""
    import torch.nn.functional as F
    from resemble_enhance.enhancer.univnet.alias_free_torch.filter import LowPassFilter1d
    from resemble_enhance.enhancer.univnet.alias_free_torch.resample import UpSample1d
    from resemble_enhance.enhancer.univnet.amp import UpActDown

    for m in model.modules():
        if isinstance(m, UpActDown):
            m.upsample.channels = m.act.in_features
            m.downsample.lowpass.channels = m.act.in_features

    def up_forward(self, x):
        x = F.pad(x, (self.pad, self.pad), mode="replicate")
        x = self.ratio * F.conv_transpose1d(x, self.filter.expand(self.channels, -1, -1),
                                            stride=self.stride, groups=self.channels)
        return x[..., self.pad_left : -self.pad_right]

    def lowpass_forward(self, x):
        if self.padding:
            x = F.pad(x, (self.pad_left, self.pad_right), mode=self.padding_mode)
        return F.conv1d(x, self.filter.expand(self.channels, -1, -1), stride=self.stride, groups=self.channels)

    UpSample1d.forward = up_forward
    LowPassFilter1d.forward = lowpass_forward


class ConvTransposeAsConv:
    """ConvTranspose1d(stride s, padding p, output_padding op) as zero insertion and a
    plain Conv1d with the flipped kernel: the same result, but DirectML rejects the
    vocoder's transposed convolutions (stride 7 with output padding)."""

    @staticmethod
    def make(ct):
        import torch
        from torch import nn

        s, p, op, k = ct.stride[0], ct.padding[0], ct.output_padding[0], ct.kernel_size[0]
        assert ct.groups == 1 and ct.dilation[0] == 1
        conv = nn.Conv1d(ct.in_channels, ct.out_channels, k, bias=ct.bias is not None)
        with torch.no_grad():
            conv.weight.copy_(ct.weight.flip(-1).transpose(0, 1))
            if ct.bias is not None:
                conv.bias.copy_(ct.bias)

        class Module(nn.Module):
            def __init__(self):
                super().__init__()
                self.conv = conv

            def forward(self, x):
                import torch.nn.functional as F

                b, c, n = x.shape
                if s > 1:
                    z = torch.zeros(b, c, n, s - 1, dtype=x.dtype, device=x.device)
                    x = torch.cat([x.unsqueeze(-1), z], dim=-1).reshape(b, c, n * s)
                    x = x[..., : (n - 1) * s + 1]
                x = F.pad(x, (k - 1 - p, k - 1 - p + op))
                return self.conv(x)

        return Module()


def replace_conv_transpose(model):
    import torch
    from torch import nn

    for name, child in list(model.named_children()):
        if isinstance(child, nn.ConvTranspose1d):
            new = ConvTransposeAsConv.make(child)
            x = torch.randn(1, child.in_channels, 13)
            with torch.no_grad():
                err = (new(x) - child(x)).abs().max() / child(x).abs().max()
            assert err < 1e-5, f"ConvTranspose rewrite differs by {err}"
            setattr(model, name, new)
        else:
            replace_conv_transpose(child)


def time_mapping_a(n=4):
    """Solve h(1/n, a) = 0.5 for h(t, a) = (a**t - 1) / (a - 1), as Solver.exponential_decay_mapping does."""
    from scipy.optimize import fsolve

    return float(fsolve(lambda a: (a ** (1 / n) - 1) / (a - 1) - 0.5, x0=0)[0])


# ---- deterministic noise shared with the C++ program (for the reference tests) ----


class NoiseGen:
    """splitmix64 + Box-Muller: the same standard-normal stream as enhance::NormalGen in C++."""

    MASK = (1 << 64) - 1

    def __init__(self, seed):
        self.state = seed & self.MASK

    def next_u64(self):
        self.state = (self.state + 0x9E3779B97F4A7C15) & self.MASK
        z = self.state
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & self.MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & self.MASK
        return z ^ (z >> 31)

    def normal(self, n):
        out = np.empty(n, dtype=np.float32)
        i = 0
        while i < n:
            u1 = ((self.next_u64() >> 11) + 1) * (1.0 / 9007199254740993.0)  # (0, 1]
            u2 = (self.next_u64() >> 11) * (1.0 / 9007199254740992.0)  # [0, 1)
            r = math.sqrt(-2.0 * math.log(u1))
            out[i] = r * math.cos(2 * math.pi * u2)
            if i + 1 < n:
                out[i + 1] = r * math.sin(2 * math.pi * u2)
            i += 2
        return out
