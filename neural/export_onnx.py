#!/usr/bin/env python3
"""Phase 4 / N2: Export ECStudentESPCN4x to single-file ONNX + verified FP32 weight pack.

Loads assets/models/ec_bplus_espcn4x.pt, exports assets/models/ec_bplus_espcn4x.onnx
with dynamic spatial axes (no external .onnx.data sidecar), packs a CRC32-verified
binary weight blob assets/models/ec_bplus_espcn4x_weights.bin ("ECBP" v1) for
zero-dependency C/OpenGL runtime loading in N3, verifies PyTorch FP32 <-> ONNX
Runtime parity across 320x224, 398x224 (widescreen) and real fight_320.png, checks
strict ORT determinism, benchmarks ORT latency, and updates pipeline/manifest.json.
"""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
import statistics
import struct
import sys
import time
import zlib
from pathlib import Path
from typing import Any

import numpy as np
import onnx
import onnxruntime as ort
import torch
from PIL import Image

NEURAL_DIR = Path(__file__).resolve().parent
ROOT = NEURAL_DIR.parent
if str(NEURAL_DIR) not in sys.path:
    sys.path.insert(0, str(NEURAL_DIR))

from train_espcn import ECStudentESPCN4x, compute_ssim  # noqa: E402

DEFAULT_PT = ROOT / "assets" / "models" / "ec_bplus_espcn4x.pt"
DEFAULT_ONNX = ROOT / "assets" / "models" / "ec_bplus_espcn4x.onnx"
DEFAULT_WEIGHTS = ROOT / "assets" / "models" / "ec_bplus_espcn4x_weights.bin"
DEFAULT_MANIFEST = ROOT / "pipeline" / "manifest.json"
DEFAULT_CAPTURE = ROOT / "assets" / "captures" / "fight_320.png"
DEFAULT_OUT_PNG = ROOT / "assets" / "captures" / "fight_bplus_onnx_1280.png"

WEIGHT_MAGIC = b"ECBP"
WEIGHT_VERSION = 1
LEAKY_RELU_SLOPE = 0.1


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fp:
        for chunk in iter(lambda: fp.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def canonical_tensor_specs(channels: int, num_blocks: int, scale: int) -> list[tuple[str, tuple[int, ...]]]:
    specs: list[tuple[str, tuple[int, ...]]] = [
        ("head.0.weight", (channels, 4, 3, 3)),
        ("head.0.bias", (channels,)),
    ]
    for b in range(num_blocks):
        specs.extend([
            (f"body.{b}.conv1.weight", (channels, channels, 3, 3)),
            (f"body.{b}.conv1.bias", (channels,)),
            (f"body.{b}.conv2.weight", (channels, channels, 3, 3)),
            (f"body.{b}.conv2.bias", (channels,)),
        ])
    out_ch = 4 * scale * scale
    specs.extend([
        ("tail.weight", (out_ch, channels, 3, 3)),
        ("tail.bias", (out_ch,)),
    ])
    return specs


def export_weight_pack(
    state_dict: dict[str, torch.Tensor],
    channels: int,
    num_blocks: int,
    scale: int,
    out_path: Path,
) -> dict[str, Any]:
    """Serialize all model weights to versioned little-endian ECBP binary + round-trip check."""
    specs = canonical_tensor_specs(channels, num_blocks, scale)
    arrays: list[np.ndarray] = []
    total_floats = 0
    for name, expected_shape in specs:
        if name not in state_dict:
            raise KeyError(f"Tensor ausente no checkpoint: {name}")
        arr = np.ascontiguousarray(state_dict[name].detach().cpu().numpy(), dtype="<f4")
        if tuple(arr.shape) != expected_shape:
            raise ValueError(f"Shape inválido em {name}: {arr.shape} != {expected_shape}")
        arrays.append(arr)
        total_floats += int(arr.size)

    payload = b"".join(arr.tobytes(order="C") for arr in arrays)
    crc32 = zlib.crc32(payload) & 0xFFFFFFFF
    slope_bits = struct.unpack("<I", struct.pack("<f", LEAKY_RELU_SLOPE))[0]
    header = struct.pack(
        "<4s7I",
        WEIGHT_MAGIC,
        WEIGHT_VERSION,
        channels,
        num_blocks,
        scale,
        slope_bits,
        total_floats,
        crc32,
    )
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(header + payload)

    expected_bytes = 32 + total_floats * 4
    raw = out_path.read_bytes()
    if len(raw) != expected_bytes:
        raise RuntimeError(f"Tamanho inválido do pack: {len(raw)} != {expected_bytes}")

    r_magic, r_ver, r_ch, r_blk, r_sc, r_slope, r_cnt, r_crc = struct.unpack("<4s7I", raw[:32])
    r_payload = raw[32:]
    if (r_magic, r_ver, r_ch, r_blk, r_sc, r_slope, r_cnt) != (
        WEIGHT_MAGIC,
        WEIGHT_VERSION,
        channels,
        num_blocks,
        scale,
        slope_bits,
        total_floats,
    ):
        raise RuntimeError("Round-trip do cabeçalho ECBP falhou")
    if (zlib.crc32(r_payload) & 0xFFFFFFFF) != r_crc or r_crc != crc32:
        raise RuntimeError("Round-trip CRC32 do pack ECBP falhou")

    offset = 0
    max_diff = 0.0
    for (name, _), expected_arr in zip(specs, arrays):
        n = int(expected_arr.size)
        decoded = np.frombuffer(r_payload, dtype="<f4", count=n, offset=offset).reshape(expected_arr.shape)
        offset += n * 4
        diff = float(np.max(np.abs(decoded - expected_arr)))
        if diff > max_diff:
            max_diff = diff
        if diff != 0.0:
            raise RuntimeError(f"Round-trip mismatch em {name}: max_diff={diff}")

    return {
        "path": out_path.relative_to(ROOT).as_posix(),
        "size_bytes": len(raw),
        "sha256": sha256_file(out_path),
        "total_floats": total_floats,
        "crc32": f"0x{crc32:08x}",
        "roundtrip_max_diff": max_diff,
    }


def remove_stale_onnx(out_onnx: Path) -> None:
    for candidate in (
        out_onnx,
        Path(str(out_onnx) + ".data"),
        out_onnx.with_suffix(".onnx.data"),
    ):
        if candidate.exists():
            candidate.unlink()


def export_onnx_model(model: torch.nn.Module, out_onnx: Path) -> tuple[int, str]:
    """Export single-file ONNX (try legacy opset 17 first, fallback to dynamo opset 18)."""
    out_onnx.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.rand((1, 4, 224, 320), dtype=torch.float32, device="cpu")

    remove_stale_onnx(out_onnx)
    try:
        torch.onnx.export(
            model,
            (dummy,),
            str(out_onnx),
            input_names=["input_rgba"],
            output_names=["output_rgba"],
            opset_version=17,
            dynamo=False,
            do_constant_folding=True,
            dynamic_axes={
                "input_rgba": {2: "height", 3: "width"},
                "output_rgba": {2: "out_height", 3: "out_width"},
            },
        )
        sidecar = Path(str(out_onnx) + ".data")
        if sidecar.exists() or out_onnx.with_suffix(".onnx.data").exists():
            raise RuntimeError("External .onnx.data sidecar generated unexpectedly under dynamo=False")
        onnx.checker.check_model(str(out_onnx))
        return 17, "dynamo=False"
    except Exception as legacy_exc:
        remove_stale_onnx(out_onnx)
        h_dim = torch.export.Dim("height", min=8, max=1024)
        w_dim = torch.export.Dim("width", min=8, max=1024)
        try:
            torch.onnx.export(
                model,
                (dummy,),
                str(out_onnx),
                input_names=["input_rgba"],
                output_names=["output_rgba"],
                opset_version=18,
                dynamo=True,
                external_data=False,
                dynamic_shapes={"x": {2: h_dim, 3: w_dim}},
            )
        except Exception as dynamo_exc:
            raise RuntimeError(
                f"Falha ONNX export (legacy: {legacy_exc}; dynamo: {dynamo_exc})"
            ) from dynamo_exc

        sidecar = Path(str(out_onnx) + ".data")
        if sidecar.exists() or out_onnx.with_suffix(".onnx.data").exists():
            raise RuntimeError("External .onnx.data sidecar generated under dynamo=True")
        onnx.checker.check_model(str(out_onnx))
        return 18, f"dynamo=True (fallback: {type(legacy_exc).__name__})"


def summarize_onnx_graph(onnx_path: Path) -> dict[str, Any]:
    m = onnx.load(str(onnx_path))
    ops = collections.Counter(node.op_type for node in m.graph.node)
    inputs = [
        {
            "name": inp.name,
            "dims": [
                (d.dim_param if d.dim_param else int(d.dim_value))
                for d in inp.type.tensor_type.shape.dim
            ],
        }
        for inp in m.graph.input
    ]
    outputs = [
        {
            "name": out.name,
            "dims": [
                (d.dim_param if d.dim_param else int(d.dim_value))
                for d in out.type.tensor_type.shape.dim
            ],
        }
        for out in m.graph.output
    ]
    return {
        "op_counts": dict(sorted(ops.items())),
        "node_count": len(m.graph.node),
        "inputs": inputs,
        "outputs": outputs,
    }


def create_ort_session(onnx_path: Path) -> tuple[ort.InferenceSession, str, str]:
    """Try CUDAExecutionProvider cleanly; fall back to CPUExecutionProvider recording error."""
    available = ort.get_available_providers()
    cuda_note = "unavailable_in_build"
    if "CUDAExecutionProvider" in available:
        try:
            sess = ort.InferenceSession(str(onnx_path), providers=["CUDAExecutionProvider"])
            active = sess.get_providers()
            if "CUDAExecutionProvider" in active:
                return sess, "CUDAExecutionProvider", f"active ({active})"
            cuda_note = f"cuda_provider_dropped_by_ort (active={active})"
        except Exception as exc:
            cuda_note = f"cuda_unavailable: {type(exc).__name__}: {exc}"

    sess_cpu = ort.InferenceSession(str(onnx_path), providers=["CPUExecutionProvider"])
    active_cpu = sess_cpu.get_providers()
    return sess_cpu, active_cpu[0], cuda_note


def compute_array_metrics(pred: np.ndarray, ref: np.ndarray) -> tuple[float, float, float]:
    diff = np.abs(pred - ref)
    max_abs_err = float(np.max(diff))
    mse = float(np.mean((pred - ref) ** 2))
    psnr_db = 999.0 if mse <= 1e-18 else 10.0 * math.log10(1.0 / mse)
    with torch.no_grad():
        ssim = compute_ssim(torch.from_numpy(pred[:, :3]), torch.from_numpy(ref[:, :3]))
    return max_abs_err, psnr_db, ssim


def validate_case(
    name: str,
    model_cpu: torch.nn.Module,
    sess: ort.InferenceSession,
    inp_nchw: np.ndarray,
    expected_hw: tuple[int, int],
    check_determinism: bool = False,
) -> tuple[dict[str, Any], np.ndarray]:
    x_torch = torch.from_numpy(inp_nchw)
    with torch.no_grad():
        pt_out = model_cpu(x_torch).cpu().numpy().astype(np.float32)

    input_name = sess.get_inputs()[0].name
    ort_out = sess.run(None, {input_name: inp_nchw})[0].astype(np.float32)

    if tuple(ort_out.shape) != (1, 4, expected_hw[0], expected_hw[1]):
        raise RuntimeError(f"Shape inválido em {name}: {ort_out.shape} != (1, 4, {expected_hw[0]}, {expected_hw[1]})")

    max_err, psnr_db, ssim = compute_array_metrics(ort_out, pt_out)
    if not (max_err < 1e-4 and psnr_db > 90.0):
        raise RuntimeError(
            f"Paridade falhou em {name}: max_abs_err={max_err:.3e}, psnr={psnr_db:.2f} dB"
        )

    result: dict[str, Any] = {
        "input_shape": list(inp_nchw.shape),
        "output_shape": list(ort_out.shape),
        "max_abs_err": max_err,
        "psnr_db": round(psnr_db, 3),
        "ssim": round(ssim, 7),
    }

    if check_determinism:
        ort_out2 = sess.run(None, {input_name: inp_nchw})[0].astype(np.float32)
        det_max_err = float(np.max(np.abs(ort_out - ort_out2)))
        if det_max_err != 0.0:
            raise RuntimeError(f"Não determinístico em {name}: det_max_err={det_max_err}")
        result["det_max_err"] = det_max_err

    return result, ort_out


def benchmark_ort_latency(sess: ort.InferenceSession, provider_used: str) -> dict[str, Any]:
    input_name = sess.get_inputs()[0].name
    rng = np.random.default_rng(1993)
    x = rng.random((1, 4, 224, 320), dtype=np.float32)
    warmup = 15 if "CUDA" in provider_used else 5
    runs = 60 if "CUDA" in provider_used else 10

    for _ in range(warmup):
        _ = sess.run(None, {input_name: x})

    times_ms: list[float] = []
    for _ in range(runs):
        t0 = time.perf_counter()
        _ = sess.run(None, {input_name: x})
        times_ms.append((time.perf_counter() - t0) * 1000.0)

    times_sorted = sorted(times_ms)
    p50 = statistics.median(times_sorted)
    p99 = times_sorted[min(len(times_sorted) - 1, int(0.99 * len(times_sorted)))]
    return {
        "provider": provider_used,
        "runs": runs,
        "p50_ms": round(p50, 3),
        "p99_ms": round(p99, 3),
        "mean_ms": round(statistics.mean(times_sorted), 3),
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="Exporta e valida ECStudentESPCN4x para ONNX + pack binário ECBP")
    ap.add_argument("--pt", type=Path, default=DEFAULT_PT)
    ap.add_argument("--out-onnx", type=Path, default=DEFAULT_ONNX)
    ap.add_argument("--out-weights", type=Path, default=DEFAULT_WEIGHTS)
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--capture", type=Path, default=DEFAULT_CAPTURE)
    ap.add_argument("--out-png", type=Path, default=DEFAULT_OUT_PNG)
    args = ap.parse_args()

    if not args.pt.exists():
        print(f"ERRO: checkpoint PyTorch ausente: {args.pt}", file=sys.stderr)
        return 1

    ck = torch.load(args.pt, map_location="cpu", weights_only=False)
    channels = int(ck["channels"])
    num_blocks = int(ck["num_blocks"])
    scale = int(ck["scale"])
    model = ECStudentESPCN4x(channels=channels, num_blocks=num_blocks, scale=scale)
    model.load_state_dict(ck["state_dict"])
    model.eval()

    opset_used, export_mode = export_onnx_model(model, args.out_onnx)
    graph_info = summarize_onnx_graph(args.out_onnx)
    onnx_size = args.out_onnx.stat().st_size
    onnx_sha = sha256_file(args.out_onnx)

    weights_info = export_weight_pack(ck["state_dict"], channels, num_blocks, scale, args.out_weights)

    available_providers = ort.get_available_providers()
    sess, provider_used, cuda_note = create_ort_session(args.out_onnx)

    gen = torch.Generator().manual_seed(19931211)
    rand_320 = torch.rand((1, 4, 224, 320), generator=gen, dtype=torch.float32).numpy()
    rand_398 = torch.rand((1, 4, 224, 398), generator=gen, dtype=torch.float32).numpy()

    parity_320, _ = validate_case(
        "random_320x224", model, sess, rand_320, (224 * scale, 320 * scale), check_determinism=True
    )
    parity_398, _ = validate_case(
        "random_398x224", model, sess, rand_398, (224 * scale, 398 * scale), check_determinism=False
    )

    parity_info: dict[str, Any] = {
        "random_320x224": parity_320,
        "random_398x224": parity_398,
    }

    if args.capture.exists():
        img = Image.open(args.capture).convert("RGBA")
        arr_hwc = np.asarray(img, dtype=np.float32) / 255.0
        cap_nchw = np.ascontiguousarray(arr_hwc.transpose(2, 0, 1)[None, ...], dtype=np.float32)
        h, w = img.height, img.width
        cap_res, ort_cap = validate_case(
            args.capture.name, model, sess, cap_nchw, (h * scale, w * scale), check_determinism=False
        )
        if cap_res["ssim"] <= 0.99999:
            raise RuntimeError(f"SSIM abaixo do limiar em {args.capture.name}: {cap_res['ssim']}")

        out_u8 = np.clip(np.rint(ort_cap[0].transpose(1, 2, 0) * 255.0), 0.0, 255.0).astype(np.uint8)
        args.out_png.parent.mkdir(parents=True, exist_ok=True)
        Image.fromarray(out_u8, mode="RGBA").save(args.out_png)
        cap_res["output_png"] = args.out_png.relative_to(ROOT).as_posix()

        if torch.cuda.is_available():
            with torch.no_grad():
                m_fp16 = ECStudentESPCN4x(channels=channels, num_blocks=num_blocks, scale=scale).to(
                    device="cuda", dtype=torch.float16
                )
                m_fp16.load_state_dict({k: v.to(device="cuda", dtype=torch.float16) for k, v in ck["state_dict"].items()})
                m_fp16.eval()
                fp16_out = m_fp16(torch.from_numpy(cap_nchw).to(device="cuda", dtype=torch.float16))
                fp16_np = fp16_out.detach().float().cpu().numpy()
            fp16_err, fp16_psnr, fp16_ssim = compute_array_metrics(fp16_np, ort_cap)
            cap_res["fp16_cuda_vs_onnx_max_abs_err"] = fp16_err
            cap_res["fp16_cuda_vs_onnx_psnr_db"] = round(fp16_psnr, 3)
            cap_res["fp16_cuda_vs_onnx_ssim"] = round(fp16_ssim, 6)

        parity_info["fight_320"] = cap_res

    ort_latency = benchmark_ort_latency(sess, provider_used)

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    nm = manifest.get("neural_model", {})
    nm.update({
        "onnx_exported": True,
        "onnx_note": "exported_and_verified",
        "onnx_file": args.out_onnx.relative_to(ROOT).as_posix(),
        "onnx_opset": opset_used,
        "onnx_export_mode": export_mode,
        "onnx_size_bytes": onnx_size,
        "onnx_sha256": onnx_sha,
        "onnx_graph": graph_info,
        "weights_bin": weights_info["path"],
        "weights_size_bytes": weights_info["size_bytes"],
        "weights_sha256": weights_info["sha256"],
        "weights_crc32": weights_info["crc32"],
        "weights_roundtrip_max_diff": weights_info["roundtrip_max_diff"],
        "onnx_parity": parity_info,
        "ort_provider_used": provider_used,
        "ort_available_providers": available_providers,
        "ort_cuda_note": cuda_note,
        "ort_latency": ort_latency,
    })
    manifest["neural_model"] = nm
    args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(
        f"[ONNX] {args.out_onnx.relative_to(ROOT)} | opset={opset_used} ({export_mode}) | "
        f"size={onnx_size} B | sha256={onnx_sha[:12]}"
    )
    print(f"[ONNX] graph ops={graph_info['op_counts']} | inputs={graph_info['inputs']} -> outputs={graph_info['outputs']}")
    print(
        f"[ECBP] {weights_info['path']} | size={weights_info['size_bytes']} B | "
        f"floats={weights_info['total_floats']} | crc32={weights_info['crc32']} | "
        f"roundtrip_max_diff={weights_info['roundtrip_max_diff']}"
    )
    print(f"[ORT] provider={provider_used} | available={available_providers} | cuda_note={cuda_note}")
    print(f"[PARITY] {json.dumps(parity_info, ensure_ascii=False)}")
    print(f"[LATENCY] {json.dumps(ort_latency, ensure_ascii=False)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
