#!/usr/bin/env python3
"""Phase 4 / N1-N2: Distill ComfyUI HD teacher outputs into a real-time B+ student CNN.

Trains ECStudentESPCN4x (~82k params) on extracted+HD pairs from pipeline/manifest.json
using PyTorch CUDA, evaluates validation PSNR/SSIM/Alpha-IoU, benchmarks p50/p99
320x224->1280x896 GPU latency (FP16 & FP32), saves assets/models/ec_bplus_espcn4x.pt,
attempts ONNX export if onnx/onnxscript are present (without auto-installing), and
records neural_model metadata in pipeline/manifest.json.
"""

from __future__ import annotations

import argparse
import json
import random
import statistics
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = ROOT / "pipeline" / "manifest.json"
DEFAULT_OUT_PT = ROOT / "assets" / "models" / "ec_bplus_espcn4x.pt"
DEFAULT_OUT_ONNX = ROOT / "assets" / "models" / "ec_bplus_espcn4x.onnx"


class ResBlock(nn.Module):
    def __init__(self, channels: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, kernel_size=3, padding=1)
        self.act = nn.LeakyReLU(0.1, inplace=True)
        self.conv2 = nn.Conv2d(channels, channels, kernel_size=3, padding=1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return x + self.conv2(self.act(self.conv1(x)))


class ECStudentESPCN4x(nn.Module):
    """Compact ~82k-param RGBA 4x super-resolution CNN operating at native resolution."""

    def __init__(self, channels: int = 40, num_blocks: int = 2, scale: int = 4) -> None:
        super().__init__()
        self.scale = scale
        self.head = nn.Sequential(
            nn.Conv2d(4, channels, kernel_size=3, padding=1),
            nn.LeakyReLU(0.1, inplace=True),
        )
        self.body = nn.Sequential(*[ResBlock(channels) for _ in range(num_blocks)])
        self.tail = nn.Conv2d(channels, 4 * scale * scale, kernel_size=3, padding=1)
        self.shuffle = nn.PixelShuffle(scale)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        base = F.interpolate(x, scale_factor=self.scale, mode="bilinear", align_corners=False)
        feat = self.body(self.head(x))
        delta = self.shuffle(self.tail(feat))
        return torch.clamp(base + delta, 0.0, 1.0)


def sobel_edges(x: torch.Tensor) -> torch.Tensor:
    kx = torch.tensor([[-1.0, 0.0, 1.0], [-2.0, 0.0, 2.0], [-1.0, 0.0, 1.0]], device=x.device, dtype=x.dtype)
    ky = kx.t()
    kernel = torch.stack([kx, ky], dim=0).unsqueeze(1).repeat(x.shape[1], 1, 1, 1)
    # Depthwise gradient for each RGB channel
    gx_gy = F.conv2d(x, kernel, padding=1, groups=x.shape[1])
    return gx_gy


def compute_ssim_tensor(pred: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    """Differentiable mean SSIM across (N, C, H, W) tensor in [0, 1]."""
    c1 = 0.01 ** 2
    c2 = 0.03 ** 2
    mu_x = F.avg_pool2d(pred, 7, stride=1, padding=3)
    mu_y = F.avg_pool2d(target, 7, stride=1, padding=3)
    sigma_x = F.avg_pool2d(pred * pred, 7, stride=1, padding=3) - mu_x * mu_x
    sigma_y = F.avg_pool2d(target * target, 7, stride=1, padding=3) - mu_y * mu_y
    sigma_xy = F.avg_pool2d(pred * target, 7, stride=1, padding=3) - mu_x * mu_y
    num = (2 * mu_x * mu_y + c1) * (2 * sigma_xy + c2)
    den = (mu_x * mu_x + mu_y * mu_y + c1) * (sigma_x + sigma_y + c2)
    return (num / torch.clamp(den, min=1e-8)).mean()


def compute_ssim(pred: torch.Tensor, target: torch.Tensor) -> float:
    """Compute mean SSIM across (N, C, H, W) tensor in [0, 1]."""
    return float(compute_ssim_tensor(pred, target).item())


@torch.no_grad()
def pre_dedither_torch(x: torch.Tensor, mode: str) -> torch.Tensor:
    """Exact PyTorch GPU mirror of s_dedither_cs in neural/infer/ec_bplus_gl.c.

    x: (N, 4, H, W) float32 in [0, 1]. mode: 'off' | 'cbod' | 'both'.
    """
    if mode == "off":
        return x
    mode_id = 2 if mode == "both" else 1
    rgb = x[:, :3]
    alpha = x[:, 3:4]
    p = F.pad(rgb, (2, 2, 2, 2), mode="replicate")
    H, W = rgb.shape[2], rgb.shape[3]

    def at(dy: int, dx: int) -> torch.Tensor:
        return p[:, :, 2 + dy : 2 + dy + H, 2 + dx : 2 + dx + W]

    C = at(0, 0)
    L, R = at(0, -1), at(0, 1)
    U, D = at(-1, 0), at(1, 0)
    UL, UR = at(-1, -1), at(-1, 1)
    DL, DR = at(1, -1), at(1, 1)

    def ceq(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
        return (a - b).abs().amax(dim=1, keepdim=True) <= 0.035

    def cneq(a: torch.Tensor, b: torch.Tensor) -> torch.Tensor:
        return (a - b).abs().amax(dim=1, keepdim=True) > 0.045

    out_rgb = C.clone()
    blended = torch.zeros((x.shape[0], 1, H, W), dtype=torch.bool, device=x.device)

    # 1. Quadrant-based 2x2 checkerboard (CBOD)
    q_ul = ceq(C, UL) & ceq(L, U) & cneq(C, L)
    q_ur = ceq(C, UR) & ceq(R, U) & cneq(C, R)
    q_dl = ceq(C, DL) & ceq(L, D) & cneq(C, L)
    q_dr = ceq(C, DR) & ceq(R, D) & cneq(C, R)
    q_cnt = q_ul.int() + q_ur.int() + q_dl.int() + q_dr.int()
    m1 = q_cnt >= 2
    if m1.any():
        acc = (
            q_ul.float() * (0.5 * (L + U))
            + q_ur.float() * (0.5 * (R + U))
            + q_dl.float() * (0.5 * (L + D))
            + q_dr.float() * (0.5 * (R + D))
        )
        cand1 = 0.5 * C + 0.5 * (acc / torch.clamp(q_cnt.float(), min=1.0))
        out_rgb = torch.where(m1, cand1, out_rgb)
        blended = blended | m1

    # 2. Textured-background checkerboard
    eq_ul, eq_ur, eq_dl, eq_dr = ceq(C, UL), ceq(C, UR), ceq(C, DL), ceq(C, DR)
    pair_u = eq_ul & eq_ur
    pair_d = eq_dl & eq_dr
    pair_l = eq_ul & eq_dl
    pair_r = eq_ur & eq_dr
    adj_diag = pair_u | pair_d | pair_l | pair_r
    m2a = (~blended) & adj_diag & cneq(C, L) & cneq(C, R) & cneq(C, U) & cneq(C, D)
    if m2a.any():
        wsum = pair_u.float() + pair_d.float() + pair_l.float() + pair_r.float()
        acc = pair_u.float() * U + pair_d.float() * D + pair_l.float() * L + pair_r.float() * R
        cand2a = 0.5 * C + 0.5 * (acc / torch.clamp(wsum, min=1.0))
        out_rgb = torch.where(m2a, cand2a, out_rgb)
        blended = blended | m2a

    orth_eq = cneq(C, L) & ceq(L, R) & ceq(U, D) & ceq(L, U)
    d_not_b = cneq(UL, L).int() + cneq(UR, L).int() + cneq(DL, L).int() + cneq(DR, L).int()
    m2b = (~blended) & orth_eq & (d_not_b >= 2)
    if m2b.any():
        B = 0.25 * (L + R + U + D)
        cand2b = 0.5 * C + 0.5 * B
        out_rgb = torch.where(m2b, cand2b, out_rgb)
        blended = blended | m2b

    # 3. Vertical-line pseudo-transparency (SGENPT-MIX VL)
    if mode_id >= 2:
        L2, R2 = at(0, -2), at(0, 2)
        vc = ceq(U, C) | ceq(D, C)
        vl = ceq(UL, L) | ceq(DL, L)
        vr = ceq(UR, R) | ceq(DR, R)
        m3 = (~blended) & ceq(L, R) & cneq(C, L) & vc & vl & vr & (ceq(C, L2) | ceq(C, R2))
        if m3.any():
            cand3 = 0.5 * C + 0.25 * (L + R)
            out_rgb = torch.where(m3, cand3, out_rgb)

    return torch.cat([out_rgb, alpha], dim=1)


def load_pair_tensors(orig_path: Path, hd_path: Path, scale: int = 4) -> tuple[np.ndarray, np.ndarray]:
    lr = np.array(Image.open(orig_path).convert("RGBA"), dtype=np.float32) / 255.0
    hr = np.array(Image.open(hd_path).convert("RGBA"), dtype=np.float32) / 255.0
    if hr.shape[0] != lr.shape[0] * scale or hr.shape[1] != lr.shape[1] * scale:
        raise ValueError(f"Dimension mismatch: {orig_path} {lr.shape} vs {hd_path} {hr.shape}")
    return lr.transpose(2, 0, 1), hr.transpose(2, 0, 1)


def extract_patch(lr: np.ndarray, hr: np.ndarray, patch_lr: int, scale: int, rng: random.Random, deterministic: bool = False) -> tuple[np.ndarray, np.ndarray]:
    _, h, w = lr.shape
    patch_hr = patch_lr * scale
    if h <= patch_lr and w <= patch_lr:
        out_lr = np.zeros((4, patch_lr, patch_lr), dtype=np.float32)
        out_hr = np.zeros((4, patch_hr, patch_hr), dtype=np.float32)
        out_lr[:, :h, :w] = lr
        out_hr[:, : h * scale, : w * scale] = hr
        return out_lr, out_hr

    # For wide/tall sheets (planes), pad smaller axis if needed then crop aligned region
    pad_h = max(0, patch_lr - h)
    pad_w = max(0, patch_lr - w)
    if pad_h or pad_w:
        lr = np.pad(lr, ((0, 0), (0, pad_h), (0, pad_w)), mode="constant")
        hr = np.pad(hr, ((0, 0), (0, pad_h * scale), (0, pad_w * scale)), mode="constant")
        _, h, w = lr.shape

    if deterministic:
        y0 = (h - patch_lr) // 2
        x0 = (w - patch_lr) // 2
    else:
        y0 = rng.randint(0, h - patch_lr)
        x0 = rng.randint(0, w - patch_lr)

    return (
        lr[:, y0 : y0 + patch_lr, x0 : x0 + patch_lr].copy(),
        hr[:, y0 * scale : (y0 + patch_lr) * scale, x0 * scale : (x0 + patch_lr) * scale].copy(),
    )


def build_eval_batches(pairs: list[tuple[np.ndarray, np.ndarray]], patch_lr: int, scale: int) -> tuple[torch.Tensor, torch.Tensor]:
    rng = random.Random(42)
    lrs: list[np.ndarray] = []
    hrs: list[np.ndarray] = []
    for lr, hr in pairs:
        _, h, w = lr.shape
        if h >= patch_lr * 2 and w >= patch_lr * 2:
            # 3 deterministic crops covering center & sides of each plane sheet
            for fy, fx in ((0.2, 0.2), (0.5, 0.5), (0.2, 0.8)):
                y0 = int((h - patch_lr) * fy)
                x0 = int((w - patch_lr) * fx)
                lrs.append(lr[:, y0 : y0 + patch_lr, x0 : x0 + patch_lr])
                hrs.append(hr[:, y0 * scale : (y0 + patch_lr) * scale, x0 * scale : (x0 + patch_lr) * scale])
        else:
            lp, hp = extract_patch(lr, hr, patch_lr, scale, rng, deterministic=True)
            lrs.append(lp)
            hrs.append(hp)
    return torch.from_numpy(np.stack(lrs)), torch.from_numpy(np.stack(hrs))


def sample_train_batch(pairs: list[tuple[np.ndarray, np.ndarray]], batch_size: int, patch_lr: int, scale: int, rng: random.Random) -> tuple[torch.Tensor, torch.Tensor]:
    lrs: list[np.ndarray] = []
    hrs: list[np.ndarray] = []
    for _ in range(batch_size):
        lr, hr = rng.choice(pairs)
        lp, hp = extract_patch(lr, hr, patch_lr, scale, rng, deterministic=False)
        lrs.append(lp)
        hrs.append(hp)
    return torch.from_numpy(np.stack(lrs)), torch.from_numpy(np.stack(hrs))


def alpha_over(dst: np.ndarray, src: np.ndarray) -> np.ndarray:
    """Alpha-composite CHW RGBA src over opaque CHW RGBA dst in place."""
    a = src[3:4]
    dst[:3] = src[:3] * a + dst[:3] * (1.0 - a)
    dst[3:4] = 1.0
    return dst


def composite_scene_patch(
    arenas: list[dict[str, tuple[np.ndarray, np.ndarray]]],
    sprites: list[tuple[np.ndarray, np.ndarray]],
    patch_lr: int,
    scale: int,
    rng: random.Random,
    deterministic: bool = False,
) -> tuple[np.ndarray, np.ndarray]:
    """Synthesize a realistic opaque runtime patch: backdrop + plane_b + plane_a + 0..4 sprites."""
    patch_hr = patch_lr * scale
    lr = np.zeros((4, patch_lr, patch_lr), dtype=np.float32)
    hr = np.zeros((4, patch_hr, patch_hr), dtype=np.float32)
    lr[3] = 1.0
    hr[3] = 1.0

    # Genesis-like dark/mid backdrop color (black 50% of the time)
    if not deterministic and rng.random() < 0.5:
        bg_rgb = np.array([rng.randint(0, 4) / 7.0 for _ in range(3)], dtype=np.float32)[:, None, None]
        lr[:3] = bg_rgb
        hr[:3] = bg_rgb

    arena = arenas[0] if deterministic else rng.choice(arenas)
    hflip = (not deterministic) and (rng.random() < 0.5)

    for layer_key in ("plane_b", "plane_a"):
        if layer_key not in arena:
            continue
        p_lr, p_hr = arena[layer_key]
        _, ph, pw = p_lr.shape
        if deterministic:
            y0 = (ph - patch_lr) // 2
            x0 = (pw - patch_lr) // 2
        else:
            y0 = rng.randint(0, max(0, ph - patch_lr))
            x0 = rng.randint(0, max(0, pw - patch_lr))
        c_lr = p_lr[:, y0 : y0 + patch_lr, x0 : x0 + patch_lr]
        c_hr = p_hr[:, y0 * scale : (y0 + patch_lr) * scale, x0 * scale : (x0 + patch_lr) * scale]
        if hflip:
            c_lr = c_lr[:, :, ::-1]
            c_hr = c_hr[:, :, ::-1]
        alpha_over(lr, c_lr)
        alpha_over(hr, c_hr)

    # Composite 0..4 sprites (80% have 1..4 sprites so fighter detail gets heavy coverage)
    if sprites:
        n_spr = 2 if deterministic else (0 if rng.random() < 0.2 else rng.randint(1, 4))
        for _ in range(n_spr):
            s_lr, s_hr = rng.choice(sprites)
            _, sh, sw = s_lr.shape
            if sh > patch_lr or sw > patch_lr:
                continue
            if not deterministic and rng.random() < 0.5:
                s_lr = s_lr[:, :, ::-1]
                s_hr = s_hr[:, :, ::-1]
            sy = rng.randint(0, patch_lr - sh)
            sx = rng.randint(0, patch_lr - sw)
            alpha_over(lr[:, sy : sy + sh, sx : sx + sw], s_lr)
            alpha_over(
                hr[:, sy * scale : (sy + sh) * scale, sx * scale : (sx + sw) * scale],
                s_hr,
            )

    return lr, hr


def build_composite_eval_batches(
    arenas: list[dict[str, tuple[np.ndarray, np.ndarray]]],
    sprites: list[tuple[np.ndarray, np.ndarray]],
    patch_lr: int,
    scale: int,
    num_samples: int = 36,
) -> tuple[torch.Tensor, torch.Tensor]:
    rng = random.Random(42)
    lrs: list[np.ndarray] = []
    hrs: list[np.ndarray] = []
    for i in range(num_samples):
        arena = arenas[i % len(arenas)]
        lp, hp = composite_scene_patch([arena], sprites, patch_lr, scale, rng, deterministic=False)
        lrs.append(lp)
        hrs.append(hp)
    return torch.from_numpy(np.stack(lrs)), torch.from_numpy(np.stack(hrs))


def sample_composite_batch(
    arenas: list[dict[str, tuple[np.ndarray, np.ndarray]]],
    sprites: list[tuple[np.ndarray, np.ndarray]],
    batch_size: int,
    patch_lr: int,
    scale: int,
    rng: random.Random,
) -> tuple[torch.Tensor, torch.Tensor]:
    lrs: list[np.ndarray] = []
    hrs: list[np.ndarray] = []
    for _ in range(batch_size):
        lp, hp = composite_scene_patch(arenas, sprites, patch_lr, scale, rng, deterministic=False)
        lrs.append(lp)
        hrs.append(hp)
    return torch.from_numpy(np.stack(lrs)), torch.from_numpy(np.stack(hrs))


def benchmark_gpu_latency(
    model: nn.Module,
    device: torch.device,
    dtype: torch.dtype,
    channels: int = 40,
    num_blocks: int = 2,
    scale: int = 4,
    runs: int = 80,
) -> dict[str, float]:
    m = ECStudentESPCN4x(channels=channels, num_blocks=num_blocks, scale=scale).to(device=device, dtype=dtype)
    m.load_state_dict({k: v.to(dtype=dtype) for k, v in model.state_dict().items()})
    m.eval()
    x = torch.rand((1, 4, 224, 320), device=device, dtype=dtype)

    times_ms: list[float] = []
    with torch.no_grad():
        for _ in range(15):
            _ = m(x)
        torch.cuda.synchronize(device)
        for _ in range(runs):
            ev_s = torch.cuda.Event(enable_timing=True)
            ev_e = torch.cuda.Event(enable_timing=True)
            ev_s.record()
            _ = m(x)
            ev_e.record()
            torch.cuda.synchronize(device)
            times_ms.append(float(ev_s.elapsed_time(ev_e)))

    times_sorted = sorted(times_ms)
    p50 = statistics.median(times_sorted)
    p99_idx = min(len(times_sorted) - 1, int(0.99 * len(times_sorted)))
    p99 = times_sorted[p99_idx]
    return {"p50_ms": round(p50, 4), "p99_ms": round(p99, 4), "mean_ms": round(statistics.mean(times_sorted), 4)}


# ---------------------------------------------------------------------------
# G2.3: modo B+ v3 (full-frame distillation, perda perceptual)
# ---------------------------------------------------------------------------
def _load_rgb_u8(path: Path) -> np.ndarray:
    """PNG -> uint8 CHW RGB."""
    return np.ascontiguousarray(np.array(Image.open(path).convert("RGB"), dtype=np.uint8).transpose(2, 0, 1))


def select_v3_frames(manifest: dict[str, Any], max_frames: int, max_val_frames: int) -> tuple[list[dict], list[dict]]:
    """Subconjunto deterministico (espacado por id ordenado) respeitando o campo 'split'."""
    frames = sorted(manifest.get("frames", []), key=lambda f: str(f["id"]))
    train = [f for f in frames if f.get("split", "train") == "train"]
    val = [f for f in frames if f.get("split") == "val"]

    def spaced(items: list[dict], n: int) -> list[dict]:
        if n <= 0 or len(items) <= n:
            return items
        return [items[int(i * len(items) / n)] for i in range(n)]

    if max_frames > 0:
        n_val = min(len(val), max_val_frames, max(1, int(round(max_frames * 0.1)))) if val else 0
        train = spaced(train, max(1, max_frames - n_val))
        val = spaced(val, n_val)
    else:
        val = spaced(val, max_val_frames)
    return train, val


def load_v3_pairs(
    root: Path, teacher: str, frames: list[dict], device: torch.device
) -> tuple[list[torch.Tensor], list[torch.Tensor], list[str]]:
    lrs: list[torch.Tensor] = []
    hrs: list[torch.Tensor] = []
    ids: list[str] = []
    for f in frames:
        fid = str(f["id"])
        lp = root / "predither" / f"{fid}.png"
        hp = root / "targets" / teacher / f"{fid}.png"
        if not lp.exists() or not hp.exists():
            continue
        lr = _load_rgb_u8(lp)
        hr = _load_rgb_u8(hp)
        if hr.shape[1] != lr.shape[1] * 4 or hr.shape[2] != lr.shape[2] * 4:
            print(f"AVISO: dimensoes incompativeis em {fid}: {lr.shape} vs {hr.shape}; ignorado", file=sys.stderr)
            continue
        lrs.append(torch.from_numpy(lr).to(device))
        hrs.append(torch.from_numpy(hr).to(device))
        ids.append(fid)
    return lrs, hrs, ids


def sample_v3_batch(
    lrs: list[torch.Tensor], hrs: list[torch.Tensor], batch: int, patch: int, gen: random.Random
) -> tuple[torch.Tensor, torch.Tensor]:
    """Patches aleatorios de frames inteiros + flip horizontal (sem rot90/flip vertical)."""
    xs: list[torch.Tensor] = []
    ys: list[torch.Tensor] = []
    for _ in range(batch):
        i = gen.randrange(len(lrs))
        _, h, w = lrs[i].shape
        p = min(patch, h, w)
        y0 = gen.randint(0, h - p)
        x0 = gen.randint(0, w - p)
        lp = lrs[i][:, y0 : y0 + p, x0 : x0 + p]
        hp = hrs[i][:, y0 * 4 : (y0 + p) * 4, x0 * 4 : (x0 + p) * 4]
        if gen.random() < 0.5:
            lp = lp.flip(-1)
            hp = hp.flip(-1)
        xs.append(lp)
        ys.append(hp)
    return torch.stack(xs).float().div_(255.0), torch.stack(ys).float().div_(255.0)


def rgb_to_rgba(x: torch.Tensor) -> torch.Tensor:
    return torch.cat([x, torch.ones_like(x[:, :1])], dim=1)


def make_lpips(net: str, device: torch.device) -> nn.Module:
    try:
        import lpips  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "lpips ausente: instale com `python -m pip install --target neural\\.pydeps lpips --no-deps` "
            "e rode com PYTHONPATH=neural\\.pydeps"
        ) from exc
    m = lpips.LPIPS(net=net, verbose=False).to(device).eval()
    for p in m.parameters():
        p.requires_grad_(False)
    return m


class V3Loss(nn.Module):
    def __init__(self, spec: str, weights: dict[str, float], lpips_net: str, device: torch.device) -> None:
        super().__init__()
        self.terms = set(spec.lower().split("+"))
        unknown = self.terms - {"l1", "lpips", "grad", "ssim"}
        if unknown:
            raise SystemExit(f"--loss: termos desconhecidos {sorted(unknown)}")
        self.w = weights
        self.lpips = None
        if "lpips" in self.terms and weights["lpips"] > 0:
            self.lpips = make_lpips(lpips_net, device)

    def forward(self, pred: torch.Tensor, tgt: torch.Tensor) -> tuple[torch.Tensor, dict[str, float]]:
        """pred/tgt RGB fp32 em [0,1]."""
        parts: dict[str, torch.Tensor] = {}
        if "l1" in self.terms:
            parts["l1"] = F.l1_loss(pred, tgt)
        if "grad" in self.terms:
            parts["grad"] = F.l1_loss(sobel_edges(pred), sobel_edges(tgt))
        if "ssim" in self.terms:
            parts["ssim"] = 1.0 - compute_ssim_tensor(pred, tgt)
        if self.lpips is not None:
            parts["lpips"] = self.lpips(pred * 2 - 1, tgt * 2 - 1).mean()
        total = sum(self.w[k] * v for k, v in parts.items())
        return total, {k: float(v.detach()) for k, v in parts.items()}


def amp_ctx(device: torch.device, mode: str):
    if device.type != "cuda" or mode == "off":
        return torch.autocast(device_type="cpu", enabled=False)
    return torch.autocast(device_type="cuda", dtype=torch.bfloat16 if mode == "bf16" else torch.float16)


@torch.no_grad()
def eval_v3(
    model: nn.Module, lpips_model: nn.Module | None, vlr: list[torch.Tensor], vhr: list[torch.Tensor]
) -> dict[str, float]:
    model.eval()
    psnrs: list[float] = []
    ssims: list[float] = []
    lp: list[float] = []
    for lr, hr in zip(vlr, vhr):
        x = rgb_to_rgba(lr[None].float() / 255.0)
        t = hr[None].float() / 255.0
        p = model(x)[:, :3].float()  # fp32 na validacao (paridade com ONNX/GL)
        mse = float(F.mse_loss(p, t))
        psnrs.append(10.0 * float(np.log10(1.0 / max(mse, 1e-10))))
        ssims.append(compute_ssim(p, t))
        if lpips_model is not None:
            lp.append(float(lpips_model(p * 2 - 1, t * 2 - 1).mean()))
    model.train()
    out = {"psnr_db": float(np.mean(psnrs)), "ssim": float(np.mean(ssims))}
    if lp:
        out["lpips"] = float(np.mean(lp))
    return out


def train_v3(args: argparse.Namespace) -> int:
    root: Path = args.dataset_v3
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    if args.teacher not in manifest.get("teachers", {}):
        print(f"AVISO: teacher '{args.teacher}' nao listado em manifest.teachers", file=sys.stderr)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    if device.type == "cuda":
        torch.backends.cudnn.benchmark = True
        torch.backends.cuda.matmul.allow_tf32 = True
        torch.backends.cudnn.allow_tf32 = True
    train_f, val_f = select_v3_frames(manifest, args.max_frames, args.max_val_frames)
    # dados ficam na GPU como uint8; --data-device cpu para datasets muito grandes
    ddev = device if args.data_device == "auto" else torch.device("cpu")
    tlr, thr, tids = load_v3_pairs(root, args.teacher, train_f, ddev)
    vlr, vhr, vids = load_v3_pairs(root, args.teacher, val_f, device)
    if not tlr:
        print("ERRO: nenhum par de treino encontrado (predither+targets).", file=sys.stderr)
        return 1
    if not vlr:  # sem split val: reserva alguns frames de treino
        k = min(2, max(1, len(tlr) // 8))
        vlr, vhr, vids = [t.to(device) for t in tlr[-k:]], [t.to(device) for t in thr[-k:]], tids[-k:]
        if len(tlr) > k:
            tlr, thr, tids = tlr[:-k], thr[:-k], tids[:-k]
        print(f"AVISO: sem frames val; usando {k} frames de treino como val")

    torch.manual_seed(args.seed)
    gen = random.Random(args.seed)
    model = ECStudentESPCN4x(channels=args.channels, num_blocks=args.num_blocks, scale=4).to(device)
    if args.init_pt:
        model.load_state_dict(torch.load(args.init_pt, map_location="cpu", weights_only=False)["state_dict"])
    param_count = sum(p.numel() for p in model.parameters())
    weights = {"l1": args.w_l1, "lpips": args.w_lpips, "grad": args.w_grad, "ssim": args.w_ssim}
    crit = V3Loss(args.loss, weights, args.lpips_net, device)
    eval_lpips = crit.lpips if crit.lpips is not None else make_lpips(args.lpips_net, device)

    total_steps = args.epochs * args.steps_per_epoch
    warm = min(args.warmup_steps, max(0, total_steps - 1))
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)

    def lr_lambda(step: int) -> float:
        if step < warm:
            return (step + 1) / max(1, warm)
        prog = (step - warm) / max(1, total_steps - warm)
        return 0.01 + 0.99 * 0.5 * (1.0 + float(np.cos(np.pi * prog)))

    sched = torch.optim.lr_scheduler.LambdaLR(opt, lr_lambda)
    scaler = torch.amp.GradScaler("cuda", enabled=(device.type == "cuda" and args.amp == "fp16"))
    print(
        f"[v3] ch={args.channels} nb={args.num_blocks} ({param_count} params) teacher={args.teacher} dev={device} "
        f"amp={args.amp} loss={args.loss} w={weights} patch={args.patch_size} batch={args.batch_size} | "
        f"train={len(tlr)} val={len(vlr)} steps={total_steps}",
        flush=True,
    )

    out_pt: Path = args.out_pt
    out_pt.parent.mkdir(parents=True, exist_ok=True)
    log_path = out_pt.with_suffix(out_pt.suffix + ".log.json")
    curve: list[dict[str, Any]] = []
    best_key = float("inf")
    best_metrics: dict[str, float] = {}
    t0 = time.time()

    def save_ckpt(metrics: dict[str, float], epoch: int) -> None:
        torch.save(
            {
                "arch": "ECStudentESPCN4x",
                "channels": args.channels,
                "num_blocks": args.num_blocks,
                "scale": 4,
                "param_count": param_count,
                "dedither": "both",  # predither ja aplicado no dataset; o GL aplica antes da rede
                "composite_scenes": False,
                "teacher": args.teacher,
                "dataset": "dataset_v3",
                "loss": args.loss,
                "loss_weights": weights,
                "epoch": epoch,
                "metrics": metrics,
                "state_dict": {k: v.detach().cpu() for k, v in model.state_dict().items()},
            },
            out_pt,
        )

    model.train()
    step = 0
    for epoch in range(1, args.epochs + 1):
        agg: dict[str, float] = {}
        te = time.time()
        for _ in range(args.steps_per_epoch):
            bx, by = sample_v3_batch(tlr, thr, args.batch_size, args.patch_size, gen)
            bx = rgb_to_rgba(bx.to(device, non_blocking=True))
            by = by.to(device, non_blocking=True)
            with amp_ctx(device, args.amp):
                pred_full = model(bx)
            pred = pred_full[:, :3].float()
            loss, parts = crit(pred, by)
            loss = loss + 0.1 * F.l1_loss(pred_full[:, 3:4].float(), torch.ones_like(pred[:, :1]))
            opt.zero_grad(set_to_none=True)
            scaler.scale(loss).backward()
            scaler.step(opt)
            scaler.update()
            sched.step()
            step += 1
            parts["total"] = float(loss.detach())
            for k, v in parts.items():
                agg[k] = agg.get(k, 0.0) + v
        if device.type == "cuda":
            torch.cuda.synchronize()
        its = args.steps_per_epoch / max(1e-9, time.time() - te)
        rec: dict[str, Any] = {"epoch": epoch, "step": step, "it_per_s": round(its, 2), "lr": round(opt.param_groups[0]["lr"], 7)}
        rec.update({f"train_{k}": round(v / args.steps_per_epoch, 5) for k, v in agg.items()})
        if epoch % args.eval_every == 0 or epoch == args.epochs:
            m = eval_v3(model, eval_lpips, vlr, vhr)
            rec.update({f"val_{k}": round(v, 5) for k, v in m.items()})
            key = m.get("lpips", 0.0) if args.best_by == "lpips" else -m["psnr_db"]
            if key < best_key:
                best_key = key
                best_metrics = {k: round(v, 5) for k, v in m.items()}
                save_ckpt(best_metrics, epoch)
                rec["best"] = True
        curve.append(rec)
        log_path.write_text(
            json.dumps({"args": {k: str(v) for k, v in vars(args).items()}, "curve": curve, "best": best_metrics}, indent=1),
            encoding="utf-8",
        )
        print("  " + " ".join(f"{k}={v}" for k, v in rec.items()), flush=True)

    print(f"[v3] fim em {time.time() - t0:.1f}s | melhor val={best_metrics} | ckpt={out_pt} | log={log_path}")
    return 0


@torch.no_grad()
def run_infer(args: argparse.Namespace) -> int:
    src: Path = args.infer
    files = sorted(src.glob(args.infer_glob)) if src.is_dir() else [src]
    if not files:
        print(f"ERRO: nenhuma imagem em {src}", file=sys.stderr)
        return 1
    ck = torch.load(args.ckpt or args.out_pt, map_location="cpu", weights_only=False)
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = ECStudentESPCN4x(channels=int(ck["channels"]), num_blocks=int(ck["num_blocks"]), scale=int(ck["scale"]))
    model.load_state_dict(ck["state_dict"])
    model.to(device).eval()
    out_dir: Path = args.out
    out_dir.mkdir(parents=True, exist_ok=True)
    for f in files:
        x = rgb_to_rgba(torch.from_numpy(_load_rgb_u8(f)).to(device)[None].float() / 255.0)
        y = model(x)[0, :3].clamp(0, 1).mul(255.0).round().byte().cpu().numpy().transpose(1, 2, 0)
        dst = out_dir / f"{f.stem}.png"
        Image.fromarray(y, "RGB").save(dst)
        print(f"{f.name} -> {dst} {y.shape[1]}x{y.shape[0]}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Treino do modelo aluno B+ ESPCN-4x na GPU via pares ComfyUI")
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--out-pt", type=Path, default=DEFAULT_OUT_PT)
    ap.add_argument("--out-onnx", type=Path, default=DEFAULT_OUT_ONNX)
    ap.add_argument("--channels", type=int, default=40)
    ap.add_argument("--num-blocks", type=int, default=2)
    ap.add_argument("--epochs", type=int, default=45)
    ap.add_argument("--steps-per-epoch", type=int, default=24)
    ap.add_argument("--batch-size", type=int, default=16)
    ap.add_argument("--patch-size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--seed", type=int, default=19931211)
    ap.add_argument("--dedither", choices=("off", "cbod", "both"), default="off")
    ap.add_argument("--dedither-prob", type=float, default=1.0)
    ap.add_argument("--composite-scenes", action="store_true")
    ap.add_argument("--no-update-manifest", action="store_true")
    # --- G2.3: modo v3 full-frame + perda perceptual (defaults antigos intactos) ---
    g = ap.add_argument_group("v3 (full-frame distillation)")
    g.add_argument("--dataset-v3", type=Path, default=None, help="dir com manifest.json/predither/targets")
    g.add_argument("--teacher", default="animesharp", help="slug em targets/<teacher>/")
    g.add_argument("--max-frames", type=int, default=0, help="subconjunto deterministico (0=todos)")
    g.add_argument("--max-val-frames", type=int, default=16)
    g.add_argument("--loss", default="l1+lpips+grad+ssim")
    g.add_argument("--w-l1", type=float, default=1.0)
    g.add_argument("--w-lpips", type=float, default=0.1)
    g.add_argument("--w-grad", type=float, default=0.1)
    g.add_argument("--w-ssim", type=float, default=0.1)
    g.add_argument("--lpips-net", choices=("vgg", "alex"), default="vgg")
    g.add_argument("--amp", choices=("bf16", "fp16", "off"), default="bf16")
    g.add_argument("--warmup-steps", type=int, default=200)
    g.add_argument("--eval-every", type=int, default=1, help="epocas entre validacoes")
    g.add_argument("--best-by", choices=("lpips", "psnr"), default="lpips")
    g.add_argument("--data-device", choices=("auto", "cpu"), default="auto")
    g.add_argument("--init-pt", type=Path, default=None)
    g.add_argument("--infer", type=Path, default=None, help="png ou dir (predither) para inferir com o .pt")
    g.add_argument("--infer-glob", default="*.png")
    g.add_argument("--ckpt", type=Path, default=None, help="checkpoint do --infer (default --out-pt)")
    g.add_argument("--out", type=Path, default=None, help="dir de saida do --infer")
    args = ap.parse_args()

    if args.infer is not None:
        if args.out is None:
            ap.error("--infer requer --out")
        return run_infer(args)
    if args.dataset_v3 is not None:
        return train_v3(args)

    torch.manual_seed(args.seed)
    rng = random.Random(args.seed)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("entries", {})
    pairs: list[tuple[np.ndarray, np.ndarray]] = []
    sprite_pairs: list[tuple[np.ndarray, np.ndarray]] = []
    arena_map: dict[str, dict[str, tuple[np.ndarray, np.ndarray]]] = {}
    teacher_models: set[str] = set()
    for e in entries.values():
        hd_rel = e.get("hd_file")
        if not hd_rel:
            continue
        orig_p = ROOT / e["original_png"]
        hd_p = ROOT / hd_rel
        if orig_p.exists() and hd_p.exists():
            pair = load_pair_tensors(orig_p, hd_p, scale=4)
            pairs.append(pair)
            if e.get("model"):
                teacher_models.add(str(e["model"]))
            layer = e.get("layer", "")
            if layer == "sprite":
                sprite_pairs.append(pair)
            elif layer in ("plane_a", "plane_b"):
                aname = str(e.get("arena_name") or orig_p.stem)
                arena_map.setdefault(aname, {})[layer] = pair

    if len(pairs) < 4:
        print(f"ERRO: apenas {len(pairs)} pares HD encontrados (mínimo 4).", file=sys.stderr)
        return 1

    rng.shuffle(pairs)
    n_val = max(2, int(round(len(pairs) * 0.15)))
    val_pairs = pairs[:n_val]
    train_pairs = pairs[n_val:]

    # Separate arena + sprite splits for --composite-scenes
    arena_list = [arena_map[k] for k in sorted(arena_map.keys())]
    rng.shuffle(arena_list)
    n_val_ar = max(2, int(round(len(arena_list) * 0.2)))
    val_arenas = arena_list[:n_val_ar]
    train_arenas = arena_list[n_val_ar:] if len(arena_list) > n_val_ar else arena_list
    rng.shuffle(sprite_pairs)
    n_val_sp = max(4, int(round(len(sprite_pairs) * 0.15)))
    val_sprites = sprite_pairs[:n_val_sp]
    train_sprites = sprite_pairs[n_val_sp:]

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = ECStudentESPCN4x(channels=args.channels, num_blocks=args.num_blocks, scale=4).to(device)
    param_count = sum(p.numel() for p in model.parameters())
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    if args.composite_scenes:
        val_lr, val_hr = build_composite_eval_batches(val_arenas, val_sprites, args.patch_size, scale=4)
    else:
        val_lr, val_hr = build_eval_batches(val_pairs, args.patch_size, scale=4)
    val_lr = val_lr.to(device)
    val_hr = val_hr.to(device)
    if args.dedither != "off":
        val_lr = pre_dedither_torch(val_lr, args.dedither)

    print(
        f"Treinando ECStudentESPCN4x (ch={args.channels} nb={args.num_blocks} {param_count} params) em {device} | "
        f"composite={args.composite_scenes} dedither={args.dedither} (p={args.dedither_prob:.2f}) | "
        f"pares={len(pairs)} (val_crops={len(val_lr)})"
    )

    t0 = time.time()
    best_psnr = 0.0
    best_metrics: dict[str, float] = {}
    best_state = None

    for epoch in range(1, args.epochs + 1):
        model.train()
        ep_loss = 0.0
        for _ in range(args.steps_per_epoch):
            if args.composite_scenes:
                bx, by = sample_composite_batch(train_arenas, train_sprites, args.batch_size, args.patch_size, scale=4, rng=rng)
            else:
                bx, by = sample_train_batch(train_pairs, args.batch_size, args.patch_size, scale=4, rng=rng)
            bx = bx.to(device)
            by = by.to(device)
            if args.dedither != "off":
                if args.dedither_prob >= 1.0 or rng.random() < args.dedither_prob:
                    bx = pre_dedither_torch(bx, args.dedither)
            pred = model(bx)
            l_rgb = F.l1_loss(pred[:, :3], by[:, :3])
            l_grad = F.l1_loss(sobel_edges(pred[:, :3]), sobel_edges(by[:, :3]))
            l_alpha = F.l1_loss(pred[:, 3:4], by[:, 3:4])
            if args.composite_scenes:
                l_ssim = 1.0 - compute_ssim_tensor(pred[:, :3], by[:, :3])
                loss = l_rgb + 0.4 * l_grad + 0.15 * l_ssim + 0.5 * l_alpha
            else:
                loss = l_rgb + 0.5 * l_grad + 2.0 * l_alpha
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            ep_loss += float(loss.item())
        scheduler.step()

        if epoch % 10 == 0 or epoch == args.epochs:
            model.eval()
            with torch.no_grad():
                vpred = model(val_lr)
                mse = float(F.mse_loss(vpred[:, :3], val_hr[:, :3]).item())
                psnr = 10.0 * float(np.log10(1.0 / max(mse, 1e-10)))
                ssim = compute_ssim(vpred[:, :3], val_hr[:, :3])
                pm = vpred[:, 3] >= 0.5
                tm = val_hr[:, 3] >= 0.5
                inter = int((pm & tm).sum().item())
                union = int((pm | tm).sum().item())
                aiou = 1.0 if union == 0 else float(inter) / float(union)
            print(
                f"  epoch {epoch:3d}/{args.epochs} loss={ep_loss/args.steps_per_epoch:.4f} "
                f"PSNR={psnr:.2f} dB SSIM={ssim:.4f} AlphaIoU={aiou:.4f}",
                flush=True,
            )
            if psnr >= best_psnr:
                best_psnr = psnr
                best_metrics = {"psnr_db": round(psnr, 3), "ssim": round(ssim, 4), "alpha_iou": round(aiou, 4)}
                best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}

    if best_state is not None:
        model.load_state_dict({k: v.to(device) for k, v in best_state.items()})

    lat_info: dict[str, Any] = {}
    if device.type == "cuda":
        lat_info["fp16_320x224"] = benchmark_gpu_latency(model, device, torch.float16, args.channels, args.num_blocks, 4)
        lat_info["fp32_320x224"] = benchmark_gpu_latency(model, device, torch.float32, args.channels, args.num_blocks, 4)

    args.out_pt.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "arch": "ECStudentESPCN4x",
            "channels": args.channels,
            "num_blocks": args.num_blocks,
            "scale": 4,
            "param_count": param_count,
            "dedither": args.dedither,
            "composite_scenes": args.composite_scenes,
            "teacher": ", ".join(sorted(teacher_models)) or "ComfyUI HD",
            "metrics": best_metrics,
            "latency": lat_info,
            "state_dict": {k: v.detach().cpu() for k, v in model.state_dict().items()},
        },
        args.out_pt,
    )

    onnx_exported = False
    onnx_note = ""
    try:
        import onnx  # noqa: F401
        dummy = torch.rand((1, 4, 224, 320), device="cpu")
        cpu_model = ECStudentESPCN4x(channels=args.channels, num_blocks=args.num_blocks, scale=4)
        cpu_model.load_state_dict({k: v.detach().cpu() for k, v in model.state_dict().items()})
        cpu_model.eval()
        args.out_onnx.parent.mkdir(parents=True, exist_ok=True)
        torch.onnx.export(cpu_model, dummy, str(args.out_onnx), opset_version=17, dynamo=False)
        onnx_exported = True
        onnx_note = f"exported:{args.out_onnx.resolve().relative_to(ROOT).as_posix()}"
    except Exception as exc:
        onnx_exported = False
        onnx_note = f"not_exported:{type(exc).__name__}: {exc}"

    if not args.no_update_manifest:
        manifest["neural_model"] = {
            "architecture": "ECStudentESPCN4x",
            "channels": args.channels,
            "residual_blocks": args.num_blocks,
            "scale": 4,
            "param_count": param_count,
            "teacher": ", ".join(sorted(teacher_models)) or "ComfyUI 4x_IllustrationJaNai_V1_DAT2_190k.pth",
            "dataset_total_pairs": len(pairs),
            "dataset_train_pairs": len(train_pairs),
            "dataset_val_pairs": len(val_pairs),
            "train_seconds": round(time.time() - t0, 2),
            "metrics": best_metrics,
            "latency_gpu": lat_info,
            "device": torch.cuda.get_device_name(0) if device.type == "cuda" else "cpu",
            "checkpoint_pt": args.out_pt.resolve().relative_to(ROOT).as_posix(),
            "onnx_exported": onnx_exported,
            "onnx_note": onnx_note,
        }
        args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Checkpoint salvo em {args.out_pt} | lat={lat_info} | {onnx_note}")
    return 0


if __name__ == "__main__":
    import sys
    raise SystemExit(main())
