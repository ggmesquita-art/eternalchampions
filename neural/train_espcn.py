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


def compute_ssim(pred: torch.Tensor, target: torch.Tensor) -> float:
    """Compute mean SSIM across (N, C, H, W) tensor in [0, 1]."""
    c1 = 0.01 ** 2
    c2 = 0.03 ** 2
    mu_x = F.avg_pool2d(pred, 7, stride=1, padding=3)
    mu_y = F.avg_pool2d(target, 7, stride=1, padding=3)
    sigma_x = F.avg_pool2d(pred * pred, 7, stride=1, padding=3) - mu_x * mu_x
    sigma_y = F.avg_pool2d(target * target, 7, stride=1, padding=3) - mu_y * mu_y
    sigma_xy = F.avg_pool2d(pred * target, 7, stride=1, padding=3) - mu_x * mu_y
    num = (2 * mu_x * mu_y + c1) * (2 * sigma_xy + c2)
    den = (mu_x * mu_x + mu_y * mu_y + c1) * (sigma_x + sigma_y + c2)
    ssim_map = num / torch.clamp(den, min=1e-8)
    return float(ssim_map.mean().item())


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


def benchmark_gpu_latency(model: nn.Module, device: torch.device, dtype: torch.dtype, runs: int = 80) -> dict[str, float]:
    m = ECStudentESPCN4x(channels=40, num_blocks=2, scale=4).to(device=device, dtype=dtype)
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
    ap.add_argument("--epochs", type=int, default=45)
    ap.add_argument("--steps-per-epoch", type=int, default=24)
    ap.add_argument("--batch-size", type=int, default=16)
    ap.add_argument("--patch-size", type=int, default=64)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--seed", type=int, default=19931211)
    args = ap.parse_args()

    torch.manual_seed(args.seed)
    rng = random.Random(args.seed)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("entries", {})
    pairs: list[tuple[np.ndarray, np.ndarray]] = []
    for e in entries.values():
        hd_rel = e.get("hd_file")
        if not hd_rel:
            continue
        orig_p = ROOT / e["original_png"]
        hd_p = ROOT / hd_rel
        if orig_p.exists() and hd_p.exists():
            pairs.append(load_pair_tensors(orig_p, hd_p, scale=4))

    if len(pairs) < 4:
        print(f"ERRO: apenas {len(pairs)} pares HD encontrados (mínimo 4).", file=sys.stderr)
        return 1

    rng.shuffle(pairs)
    n_val = max(2, int(round(len(pairs) * 0.15)))
    val_pairs = pairs[:n_val]
    train_pairs = pairs[n_val:]

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = ECStudentESPCN4x(channels=40, num_blocks=2, scale=4).to(device)
    param_count = sum(p.numel() for p in model.parameters())
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    val_lr, val_hr = build_eval_batches(val_pairs, args.patch_size, scale=4)
    val_lr = val_lr.to(device)
    val_hr = val_hr.to(device)

    print(
        f"Treinando ECStudentESPCN4x ({param_count} params) em {device} | "
        f"pares train={len(train_pairs)} val={len(val_pairs)} (val_crops={len(val_lr)})"
    )

    t0 = time.time()
    best_psnr = 0.0
    best_metrics: dict[str, float] = {}
    best_state = None

    for epoch in range(1, args.epochs + 1):
        model.train()
        ep_loss = 0.0
        for _ in range(args.steps_per_epoch):
            bx, by = sample_train_batch(train_pairs, args.batch_size, args.patch_size, scale=4, rng=rng)
            bx = bx.to(device)
            by = by.to(device)
            pred = model(bx)
            l_rgb = F.l1_loss(pred[:, :3], by[:, :3])
            l_grad = F.l1_loss(sobel_edges(pred[:, :3]), sobel_edges(by[:, :3]))
            l_alpha = F.l1_loss(pred[:, 3:4], by[:, 3:4])
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
        lat_info["fp16_320x224"] = benchmark_gpu_latency(model, device, torch.float16)
        lat_info["fp32_320x224"] = benchmark_gpu_latency(model, device, torch.float32)

    args.out_pt.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "arch": "ECStudentESPCN4x",
            "channels": 40,
            "num_blocks": 2,
            "scale": 4,
            "param_count": param_count,
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
        cpu_model = ECStudentESPCN4x(channels=40, num_blocks=2, scale=4)
        cpu_model.load_state_dict({k: v.detach().cpu() for k, v in model.state_dict().items()})
        cpu_model.eval()
        args.out_onnx.parent.mkdir(parents=True, exist_ok=True)
        torch.onnx.export(cpu_model, dummy, str(args.out_onnx), opset_version=17, dynamo=False)
        onnx_exported = True
        onnx_note = f"exported:{args.out_onnx.relative_to(ROOT).as_posix()}"
    except Exception as exc:
        onnx_exported = False
        onnx_note = f"not_exported:{type(exc).__name__}: {exc}"

    manifest["neural_model"] = {
        "architecture": "ECStudentESPCN4x",
        "channels": 40,
        "residual_blocks": 2,
        "scale": 4,
        "param_count": param_count,
        "teacher": "ComfyUI 4x_IllustrationJaNai_V1_DAT2_190k.pth",
        "dataset_total_pairs": len(pairs),
        "dataset_train_pairs": len(train_pairs),
        "dataset_val_pairs": len(val_pairs),
        "train_seconds": round(time.time() - t0, 2),
        "metrics": best_metrics,
        "latency_gpu": lat_info,
        "device": torch.cuda.get_device_name(0) if device.type == "cuda" else "cpu",
        "checkpoint_pt": args.out_pt.relative_to(ROOT).as_posix(),
        "onnx_exported": onnx_exported,
        "onnx_note": onnx_note,
    }
    args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Checkpoint salvo em {args.out_pt} | lat={lat_info} | {onnx_note}")
    return 0


if __name__ == "__main__":
    import sys
    raise SystemExit(main())
