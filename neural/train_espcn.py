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
    args = ap.parse_args()

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
