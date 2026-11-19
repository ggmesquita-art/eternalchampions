#!/usr/bin/env python3
"""ComfyUI HD Asset Pipeline (Phase 5 / M7).

Reads extracted entries from pipeline/manifest.json, submits them to the running
ComfyUI server via REST API, enforces silhouette tolerances against 4x-scaled
native alpha masks, saves 4x RGBA outputs under assets/hd/, and records full
reproducibility metadata (workflow, version, model, seed, silhouette_iou) back
into pipeline/manifest.json without auto-approving any entry.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path
from typing import Any

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_MANIFEST = ROOT / "pipeline" / "manifest.json"
DEFAULT_WORKFLOW = ROOT / "pipeline" / "workflows" / "upscale_4x_pixelart.json"
DEFAULT_HD_DIR = ROOT / "assets" / "hd"


def http_json(url: str, data: bytes | None = None, headers: dict[str, str] | None = None, timeout: float = 30.0) -> Any:
    last_exc: Exception | None = None
    for attempt in range(3):
        try:
            req = urllib.request.Request(url, data=data, headers=headers or {})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                return json.loads(resp.read().decode("utf-8"))
        except (urllib.error.URLError, TimeoutError, OSError) as exc:
            last_exc = exc
            if attempt < 2:
                time.sleep(1.0 + attempt)
    assert last_exc is not None
    raise last_exc


def save_manifest_atomic(path: Path, manifest: dict[str, Any]) -> None:
    entries = manifest.get("entries", {})
    summary = manifest.setdefault("summary", {})
    summary["total_entries"] = len(entries)
    summary["extracted_count"] = sum(1 for e in entries.values() if e.get("status") == "extracted")
    summary["generated_count"] = sum(1 for e in entries.values() if e.get("status") == "generated")
    summary["reviewed_count"] = sum(1 for e in entries.values() if e.get("status") == "reviewed")
    summary["approved_count"] = sum(1 for e in entries.values() if e.get("status") == "approved")
    path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def upload_png(server: str, png_bytes: bytes, filename: str, subfolder: str = "ec_recomp") -> dict[str, Any]:
    boundary = f"----ECRecompBoundary{uuid.uuid4().hex}"
    parts = [
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="image"; filename="{filename}"\r\n'
        f"Content-Type: image/png\r\n\r\n".encode("utf-8"),
        png_bytes,
        b"\r\n",
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="subfolder"\r\n\r\n{subfolder}\r\n'.encode("utf-8"),
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="overwrite"\r\n\r\ntrue\r\n'.encode("utf-8"),
        f"--{boundary}--\r\n".encode("utf-8"),
    ]
    body = b"".join(parts)
    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}
    return http_json(f"{server}/upload/image", data=body, headers=headers, timeout=30.0)


def bleed_rgb_into_transparent(img: Image.Image, iterations: int = 4) -> Image.Image:
    """Fill transparent pixels with nearest opaque neighbor colors to avoid black edge halos."""
    rgba = np.array(img.convert("RGBA"), dtype=np.uint8)
    rgb = rgba[:, :, :3].astype(np.float32)
    mask = rgba[:, :, 3] > 0
    if mask.all() or not mask.any():
        return Image.fromarray(rgba[:, :, :3], mode="RGB")

    h, w = mask.shape
    cur_rgb = rgb.copy()
    cur_mask = mask.copy()

    for _ in range(iterations):
        if cur_mask.all():
            break
        acc = np.zeros_like(cur_rgb)
        cnt = np.zeros((h, w), dtype=np.float32)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                sy0, sy1 = max(0, -dy), h - max(0, dy)
                dy0, dy1 = max(0, dy), h - max(0, -dy)
                sx0, sx1 = max(0, -dx), w - max(0, dx)
                dx0, dx1 = max(0, dx), w - max(0, -dx)
                m_slice = cur_mask[sy0:sy1, sx0:sx1]
                acc[dy0:dy1, dx0:dx1] += cur_rgb[sy0:sy1, sx0:sx1] * m_slice[:, :, None]
                cnt[dy0:dy1, dx0:dx1] += m_slice.astype(np.float32)
        fill = (~cur_mask) & (cnt > 0)
        if not fill.any():
            break
        cur_rgb[fill] = acc[fill] / cnt[fill, None]
        cur_mask[fill] = True

    return Image.fromarray(np.clip(cur_rgb, 0, 255).astype(np.uint8), mode="RGB")


def enforce_silhouette(
    orig_rgba: Image.Image,
    hd_rgb: Image.Image,
    scale: int,
    tolerance_px: int,
) -> tuple[Image.Image, float]:
    """Enforce 4x silhouette boundary against native alpha mask and compute IoU."""
    target_w = orig_rgba.width * scale
    target_h = orig_rgba.height * scale
    if hd_rgb.size != (target_w, target_h):
        hd_rgb = hd_rgb.resize((target_w, target_h), Image.Resampling.LANCZOS)

    orig_a = orig_rgba.split()[3]
    a_nearest = orig_a.resize((target_w, target_h), Image.Resampling.NEAREST)
    a_bilinear = orig_a.resize((target_w, target_h), Image.Resampling.BILINEAR)

    exact_bin = np.array(a_nearest, dtype=np.uint8) >= 128
    smooth_arr = np.array(a_bilinear, dtype=np.uint8)

    if tolerance_px <= 0:
        final_a_arr = np.where(exact_bin, 255, 0).astype(np.uint8)
    else:
        dilated_img = a_nearest.filter(ImageFilter.MaxFilter(size=2 * tolerance_px + 1))
        dilated_bin = np.array(dilated_img, dtype=np.uint8) >= 128
        # Inside exact native mask keep full opacity; within tolerance halo keep smooth diagonal AA only
        final_a_arr = np.where(exact_bin, 255, np.where(dilated_bin & (smooth_arr >= 128), smooth_arr, 0)).astype(np.uint8)

    final_bin = final_a_arr >= 128
    inter = int(np.logical_and(exact_bin, final_bin).sum())
    union = int(np.logical_or(exact_bin, final_bin).sum())
    iou = 1.0 if union == 0 else float(inter) / float(union)

    hd_arr = np.array(hd_rgb.convert("RGB"), dtype=np.uint8)
    out_rgba = np.dstack([hd_arr, final_a_arr])
    return Image.fromarray(out_rgba, mode="RGBA"), iou


def submit_and_wait(server: str, prompt_graph: dict[str, Any], timeout_s: float = 120.0) -> bytes:
    client_id = uuid.uuid4().hex
    payload = json.dumps({"prompt": prompt_graph, "client_id": client_id}).encode("utf-8")
    resp = http_json(f"{server}/prompt", data=payload, headers={"Content-Type": "application/json"}, timeout=30.0)
    prompt_id = resp["prompt_id"]

    deadline = time.time() + timeout_s
    while time.time() < deadline:
        hist = http_json(f"{server}/history/{prompt_id}", timeout=15.0)
        if prompt_id in hist:
            item = hist[prompt_id]
            status_info = item.get("status", {})
            if status_info.get("status_str") == "error":
                raise RuntimeError(f"ComfyUI execution error ({prompt_id}): {status_info}")
            outputs = item.get("outputs", {})
            for node_out in outputs.values():
                imgs = node_out.get("images")
                if imgs:
                    im = imgs[0]
                    qs = urllib.parse.urlencode({
                        "filename": im["filename"],
                        "subfolder": im.get("subfolder", ""),
                        "type": im.get("type", "output"),
                    })
                    with urllib.request.urlopen(f"{server}/view?{qs}", timeout=30.0) as r:
                        return r.read()
            raise RuntimeError(f"ComfyUI completed without output image ({prompt_id})")
        time.sleep(0.15)
    raise TimeoutError(f"ComfyUI prompt {prompt_id} timed out after {timeout_s}s")


def substitute_placeholders(obj: Any, replacements: dict[str, str]) -> Any:
    if isinstance(obj, dict):
        return {k: substitute_placeholders(v, replacements) for k, v in obj.items()}
    if isinstance(obj, list):
        return [substitute_placeholders(v, replacements) for v in obj]
    if isinstance(obj, str) and obj in replacements:
        return replacements[obj]
    return obj


def layer_category(entry: dict[str, Any]) -> str:
    layer = entry.get("layer", "")
    if layer == "sprite":
        return "sprites"
    if layer in ("plane_a", "plane_b"):
        if (entry.get("width"), entry.get("height")) == (8, 8):
            return "tiles"
        return "planes"
    return "other"


def select_entries(entries: dict[str, dict[str, Any]], layers: set[str], limit: int, force: bool) -> list[tuple[str, dict[str, Any]]]:
    candidates: list[tuple[str, dict[str, Any]]] = []
    for key, e in entries.items():
        cat = layer_category(e)
        if cat not in layers:
            continue
        if not force and e.get("hd_file") and (ROOT / e["hd_file"]).exists():
            continue
        candidates.append((key, e))

    # Group by context and pick largest assets first in round-robin so every fighter/arena gets represented
    by_ctx: dict[str, list[tuple[str, dict[str, Any]]]] = {}
    for k, e in candidates:
        ctx = str(e.get("context", ""))
        by_ctx.setdefault(ctx, []).append((k, e))
    for ctx_list in by_ctx.values():
        ctx_list.sort(key=lambda pair: (pair[1].get("width", 0) * pair[1].get("height", 0), pair[0]), reverse=True)

    ordered: list[tuple[str, dict[str, Any]]] = []
    ctx_keys = sorted(by_ctx.keys())
    idx = 0
    while len(ctx_keys) > 0:
        added_any = False
        for ctx in ctx_keys:
            lst = by_ctx[ctx]
            if idx < len(lst):
                ordered.append(lst[idx])
                added_any = True
                if limit > 0 and len(ordered) >= limit:
                    return ordered
        if not added_any:
            break
        idx += 1
    return ordered


def main() -> int:
    ap = argparse.ArgumentParser(description="Submete catálogo VRAM ao ComfyUI e atualiza pipeline/manifest.json")
    ap.add_argument("--server", default="http://127.0.0.1:8188")
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--workflow", type=Path, default=DEFAULT_WORKFLOW)
    ap.add_argument("--hd-dir", type=Path, default=DEFAULT_HD_DIR)
    ap.add_argument("--layers", default="planes,sprites", help="Conjunto de categorias: planes,sprites,tiles")
    ap.add_argument("--limit", type=int, default=0, help="Limite máximo de entradas (0 = sem limite)")
    ap.add_argument("--silhouette-tolerance", type=int, default=1, help="Tolerância de borda de silhueta em px 4x")
    ap.add_argument("--seed", type=int, default=19931211)
    ap.add_argument("--reference", type=Path, default=None, help="Folha de referência opcional para remaster SDXL")
    ap.add_argument("--force", action="store_true", help="Reprocessa mesmo se hd_file já existir")
    args = ap.parse_args()

    try:
        stats = http_json(f"{args.server.rstrip('/')}/system_stats", timeout=5.0)
    except Exception as exc:
        print(f"ERRO: ComfyUI inacessível em {args.server}: {exc}", file=sys.stderr)
        return 1

    devices = stats.get("devices", [])
    dev_name = devices[0].get("name", "unknown") if devices else "unknown"
    print(f"ComfyUI online ({args.server}): version={stats.get('system',{}).get('comfyui_version')} device={dev_name}")

    wf_doc = json.loads(args.workflow.read_text(encoding="utf-8"))
    wf_meta = wf_doc.get("_meta", {})
    wf_name = str(wf_meta.get("workflow", args.workflow.stem))
    wf_ver = str(wf_meta.get("workflow_version", "1.0"))
    model_name = str(wf_meta.get("model", "unknown"))
    scale = int(wf_meta.get("scale", 4))

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("entries", {})
    wanted_layers = {x.strip() for x in args.layers.split(",") if x.strip()}
    selected = select_entries(entries, wanted_layers, args.limit, args.force)
    print(f"Selecionados {len(selected)} assets para ComfyUI (layers={sorted(wanted_layers)}, limit={args.limit})")

    ref_uploaded_name = ""
    if args.reference is not None and args.reference.exists():
        up_ref = upload_png(args.server.rstrip("/"), args.reference.read_bytes(), f"ref_{args.reference.name}")
        ref_uploaded_name = f"{up_ref.get('subfolder', 'ec_recomp')}/{up_ref['name']}".lstrip("/")

    import io
    processed = 0
    min_iou = 1.0
    server = args.server.rstrip("/")

    for i, (key, entry) in enumerate(selected, start=1):
        cat = layer_category(entry)
        orig_abs = ROOT / entry["original_png"]
        if not orig_abs.exists():
            print(f"AVISO: arquivo ausente {orig_abs}", file=sys.stderr)
            continue

        orig_rgba = Image.open(orig_abs).convert("RGBA")
        prepped_rgb = bleed_rgb_into_transparent(orig_rgba, iterations=4)
        buf = io.BytesIO()
        prepped_rgb.save(buf, format="PNG")
        up = upload_png(server, buf.getvalue(), f"{key[:16]}.png")
        subf = up.get("subfolder", "")
        uploaded_ref = f"{subf}/{up['name']}" if subf else up["name"]

        graph = copy.deepcopy(wf_doc["prompt"])
        repl = {
            "__INPUT_IMAGE__": uploaded_ref,
            "__OUTPUT_PREFIX__": f"ec_recomp/{cat}/{key[:16]}",
            "__REF_IMAGE__": ref_uploaded_name or uploaded_ref,
        }
        graph = substitute_placeholders(graph, repl)
        # Apply deterministic seed if any KSampler node is present
        for node in graph.values():
            if node.get("class_type") == "KSampler" and "seed" in node.get("inputs", {}):
                node["inputs"]["seed"] = args.seed

        out_bytes = submit_and_wait(server, graph, timeout_s=180.0)
        hd_rgb = Image.open(io.BytesIO(out_bytes))
        final_rgba, iou = enforce_silhouette(orig_rgba, hd_rgb, scale=scale, tolerance_px=args.silhouette_tolerance)
        min_iou = min(min_iou, iou)

        out_rel = Path("assets/hd") / cat / f"{key[:16]}_{scale}x.png"
        out_abs = ROOT / out_rel
        out_abs.parent.mkdir(parents=True, exist_ok=True)
        final_rgba.save(out_abs, format="PNG")

        entry["hd_file"] = str(out_rel).replace("\\", "/")
        entry["workflow"] = wf_name
        entry["workflow_version"] = wf_ver
        entry["model"] = model_name
        entry["seed"] = args.seed
        entry["silhouette_iou"] = round(iou, 6)
        # Do not auto-approve: preserve human review gate (§10.3)
        if entry.get("status") == "extracted":
            entry["status"] = "generated"
        processed += 1

        if i % 10 == 0 or i == len(selected):
            print(
                f"  [{i}/{len(selected)}] {entry['context']} {cat} {orig_rgba.width}x{orig_rgba.height} -> "
                f"{final_rgba.width}x{final_rgba.height} IoU={iou:.4f}",
                flush=True,
            )
            save_manifest_atomic(args.manifest, manifest)

    save_manifest_atomic(args.manifest, manifest)
    summary = manifest["summary"]
    print(
        f"ComfyUI concluído: {processed} assets em {args.manifest} "
        f"(generated={summary['generated_count']}, approved={summary['approved_count']}, min_iou={min_iou:.4f})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
