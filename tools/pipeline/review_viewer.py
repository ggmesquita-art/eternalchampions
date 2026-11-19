#!/usr/bin/env python3
"""Generate side-by-side HTML review gallery and interactive curation server (Phase 5 / M7)."""

from __future__ import annotations

import argparse
import html
import json
import mimetypes
import os
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_MANIFEST = ROOT / "pipeline" / "manifest.json"
DEFAULT_OUT = ROOT / "assets" / "review" / "index.html"
ASSETS_ROOT = (ROOT / "assets").resolve()
VALID_STATUSES = ("generated", "reviewed", "approved", "rejected")


def layer_category(entry: dict[str, Any]) -> str:
    layer = entry.get("layer", "")
    if layer == "sprite":
        return "sprites"
    if layer in ("plane_a", "plane_b"):
        if (entry.get("width"), entry.get("height")) == (8, 8):
            return "tiles"
        return "planes"
    return "other"


def recompute_summary(manifest: dict[str, Any]) -> dict[str, Any]:
    entries = manifest.get("entries", {})
    summary = manifest.setdefault("summary", {})
    summary["total_entries"] = len(entries)
    summary["extracted_count"] = sum(1 for e in entries.values() if e.get("status") == "extracted")
    summary["generated_count"] = sum(1 for e in entries.values() if e.get("status") == "generated")
    summary["reviewed_count"] = sum(1 for e in entries.values() if e.get("status") == "reviewed")
    summary["approved_count"] = sum(1 for e in entries.values() if e.get("status") == "approved")
    summary["rejected_count"] = sum(1 for e in entries.values() if e.get("status") == "rejected")
    return summary


def save_manifest_atomic(path: Path, manifest: dict[str, Any]) -> dict[str, Any]:
    summary = recompute_summary(manifest)
    tmp_path = path.parent / f".{path.name}.{os.getpid()}.tmp"
    tmp_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    os.replace(tmp_path, path)
    return summary


def resolve_asset_src(rel_path: str, out_dir: Path, interactive: bool) -> str:
    abs_path = (ROOT / rel_path).resolve()
    if interactive:
        try:
            rel_to_assets = abs_path.relative_to(ASSETS_ROOT).as_posix()
            return f"/assets/{rel_to_assets}"
        except ValueError:
            pass
    return Path(os.path.relpath(abs_path, out_dir)).as_posix()


def render_gallery_html(manifest: dict[str, Any], out_dir: Path, interactive: bool = False) -> str:
    entries = manifest.get("entries", {})
    summary = recompute_summary(dict(manifest))
    items_with_hd = [
        (k, e) for k, e in entries.items() if e.get("hd_file") and (ROOT / e["hd_file"]).exists()
    ]
    items_with_hd.sort(
        key=lambda pair: (
            layer_category(pair[1]),
            str(pair[1].get("context", "")),
            str(pair[1].get("layer", "")),
            pair[0],
        )
    )

    contexts = sorted({str(e.get("context", "")) for _, e in items_with_hd})
    cards: list[str] = []
    dis_attr = "" if interactive else ' disabled title="Execute: .venv/bin/python tools/pipeline/review_viewer.py --serve"'

    for key, e in items_with_hd:
        orig_src = resolve_asset_src(e["original_png"], out_dir, interactive)
        hd_src = resolve_asset_src(e["hd_file"], out_dir, interactive)
        ctx = str(e.get("context", ""))
        status = str(e.get("status", "extracted"))
        layer = str(e.get("layer", ""))
        cat = layer_category(e)
        w = int(e.get("width", 0))
        h = int(e.get("height", 0))
        sc = int(e.get("scale", 4))
        iou = e.get("silhouette_iou")
        iou_val = float(iou) if isinstance(iou, (int, float)) else 1.0
        iou_s = f"{iou_val:.4f}" if isinstance(iou, (int, float)) else "-"
        p1 = e.get("fighter_p1") or "-"
        p2 = e.get("fighter_p2") or "-"
        arena = e.get("arena_name") or "-"

        cards.append(
            f'<div class="card" data-key="{html.escape(key)}" data-category="{html.escape(cat)}" '
            f'data-context="{html.escape(ctx)}" data-status="{html.escape(status)}" '
            f'data-layer="{html.escape(layer)}" data-iou="{iou_val:.6f}" '
            f'data-orig="{html.escape(orig_src)}" data-hd="{html.escape(hd_src)}" '
            f'data-dims="{w}x{h} → {w*sc}x{h*sc}">\n'
            f'  <div class="meta"><strong>{html.escape(ctx)}</strong> · <span>{html.escape(cat)}/{html.escape(layer)}</span> · '
            f'<code>{key[:12]}</code> · {w}x{h} → {w*sc}x{h*sc} · IoU <b>{iou_s}</b></div>\n'
            f'  <div class="sub">Arena: {html.escape(arena)} | P1: {html.escape(str(p1))} | P2: {html.escape(str(p2))} | '
            f'Status: <span class="badge status-{html.escape(status)}">{html.escape(status)}</span> | '
            f'Model: {html.escape(str(e.get("model", "-")))} ({html.escape(str(e.get("workflow", "-")))})</div>\n'
            f'  <div class="pair">\n'
            f'    <figure><img src="{html.escape(orig_src)}" class="pix" alt="original"><figcaption>Original ({w}x{h})</figcaption></figure>\n'
            f'    <figure><img src="{html.escape(hd_src)}" alt="hd"><figcaption>ComfyUI HD ({w*sc}x{h*sc})</figcaption></figure>\n'
            f'  </div>\n'
            f'  <div class="actions">\n'
            f'    <button type="button" onclick="setCardStatus(\'{html.escape(key)}\', \'reviewed\')"{dis_attr}>Reviewed</button>\n'
            f'    <button type="button" class="btn-approve" onclick="setCardStatus(\'{html.escape(key)}\', \'approved\')"{dis_attr}>Approve</button>\n'
            f'    <button type="button" class="btn-reject" onclick="setCardStatus(\'{html.escape(key)}\', \'rejected\')"{dis_attr}>Reject</button>\n'
            f'    <button type="button" onclick="setCardStatus(\'{html.escape(key)}\', \'generated\')"{dis_attr}>Reset</button>\n'
            f'  </div>\n'
            f'</div>'
        )

    ctx_options = "\n".join(f'<option value="{html.escape(c)}">{html.escape(c)}</option>' for c in contexts)
    mode_banner = (
        "Modo Interativo (--serve ativo: alterações gravam em pipeline/manifest.json)"
        if interactive
        else "Modo Estático (somente leitura) — para aprovar na UI, rode: .venv/bin/python tools/pipeline/review_viewer.py --serve"
    )

    return f"""<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<title>EternalRecomp — Revisão Visual HD (M7)</title>
<style>
  body {{ font-family: system-ui, sans-serif; background: #111318; color: #e8ecf2; margin: 20px; }}
  header {{ display: flex; flex-direction: column; gap: 10px; margin-bottom: 16px; border-bottom: 1px solid #2b303c; padding-bottom: 12px; }}
  .row {{ display: flex; flex-wrap: wrap; gap: 10px; align-items: center; }}
  .stats {{ font-size: 13px; color: #b8c2d0; display: flex; flex-wrap: wrap; gap: 12px; }}
  .banner {{ font-size: 12px; color: #9fc5f8; background: #182230; border: 1px solid #2c4364; padding: 6px 10px; border-radius: 6px; }}
  select, input, button {{ background: #1d222c; color: #e8ecf2; border: 1px solid #394254; padding: 5px 10px; border-radius: 6px; cursor: pointer; font-size: 12px; }}
  input[type=number] {{ width: 70px; cursor: text; }}
  button:hover:not(:disabled) {{ background: #2a3342; }}
  button:disabled {{ opacity: 0.45; cursor: not-allowed; }}
  .btn-approve {{ border-color: #2e7d32; color: #a5d6a7; }}
  .btn-reject {{ border-color: #c62828; color: #ef9a9a; }}
  details.anim {{ background: #161a22; border: 1px solid #2a3140; border-radius: 10px; padding: 10px 14px; margin-bottom: 16px; }}
  details.anim summary {{ cursor: pointer; font-weight: 600; font-size: 14px; }}
  .player-stage {{ display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 10px; max-width: 900px; }}
  .player-stage figure {{ min-height: 220px; display: flex; flex-direction: column; justify-content: center; align-items: center; }}
  .player-stage img {{ max-height: 220px; }}
  .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(560px, 1fr)); gap: 14px; }}
  .card {{ background: #191d25; border: 1px solid #2a3140; border-radius: 10px; padding: 12px; }}
  .meta {{ font-size: 13px; margin-bottom: 4px; }}
  .sub {{ font-size: 11px; color: #9aa6b8; margin-bottom: 8px; }}
  .badge {{ padding: 1px 6px; border-radius: 4px; font-weight: 600; }}
  .status-generated {{ background: #2c3e50; color: #8ad; }}
  .status-reviewed {{ background: #4a3f18; color: #ffe082; }}
  .status-approved {{ background: #1b4d2b; color: #a5d6a7; }}
  .status-rejected {{ background: #5a1d1d; color: #ef9a9a; }}
  .pair {{ display: grid; grid-template-columns: 1fr 1fr; gap: 10px; align-items: start; }}
  .actions {{ display: flex; gap: 6px; margin-top: 8px; justify-content: flex-end; }}
  figure {{ margin: 0; background: repeating-conic-gradient(#222 0% 25%, #2a2a2a 0% 50%) 50% / 16px 16px; border-radius: 6px; padding: 6px; text-align: center; }}
  img {{ max-width: 100%; height: auto; display: inline-block; max-height: 260px; object-fit: contain; }}
  img.pix {{ image-rendering: pixelated; min-width: 96px; min-height: 96px; }}
  figcaption {{ font-size: 11px; color: #b8c2d0; margin-top: 4px; }}
</style>
</head>
<body>
<header>
  <div class="row">
    <h2 style="margin:0;">EternalRecomp — Revisão Original × ComfyUI HD (<span id="cnt-hd">{len(items_with_hd)}</span> gerados / <span id="cnt-total">{summary.get('total_entries', 0)}</span> catalogados)</h2>
  </div>
  <div class="stats">
    <span>Visíveis: <strong id="cnt-visible">{len(items_with_hd)}</strong></span>
    <span>generated: <strong id="cnt-generated">{summary.get('generated_count', 0)}</strong></span>
    <span>reviewed: <strong id="cnt-reviewed">{summary.get('reviewed_count', 0)}</strong></span>
    <span>approved: <strong id="cnt-approved">{summary.get('approved_count', 0)}</strong></span>
    <span>rejected: <strong id="cnt-rejected">{summary.get('rejected_count', 0)}</strong></span>
  </div>
  <div class="banner">{html.escape(mode_banner)}</div>
  <div class="row">
    <label>Categoria:
      <select id="catFilter" onchange="filterCards()">
        <option value="">Todas</option>
        <option value="planes">planes</option>
        <option value="sprites">sprites</option>
      </select>
    </label>
    <label>Contexto:
      <select id="ctxFilter" onchange="filterCards()">
        <option value="">Todos</option>
        {ctx_options}
      </select>
    </label>
    <label>Status:
      <select id="statusFilter" onchange="filterCards()">
        <option value="">Todos</option>
        <option value="generated">generated</option>
        <option value="reviewed">reviewed</option>
        <option value="approved">approved</option>
        <option value="rejected">rejected</option>
      </select>
    </label>
    <label>IoU máx.:
      <input id="iouFilter" type="number" min="0" max="1" step="0.01" value="1.00" onchange="filterCards()">
    </label>
    <button type="button" onclick="resetFilters()">Limpar filtros</button>
    <button type="button" onclick="batchStatus('reviewed')"{dis_attr}>Marcar visíveis como reviewed</button>
    <button type="button" class="btn-approve" onclick="batchStatus('approved')"{dis_attr}>Aprovar visíveis</button>
  </div>
</header>

<details class="anim" open>
  <summary>Player Lado a Lado em Loop (§10.3 — sprites visíveis filtrados)</summary>
  <div class="row" style="margin-top:8px;">
    <button type="button" id="playBtn" onclick="togglePlay()">Play</button>
    <button type="button" onclick="stepPlayer(-1)">Prev</button>
    <button type="button" onclick="stepPlayer(1)">Next</button>
    <label>FPS:
      <select id="fpsSelect" onchange="restartTimer()">
        <option value="4">4</option>
        <option value="6" selected>6</option>
        <option value="8">8</option>
        <option value="12">12</option>
      </select>
    </label>
    <span id="playerMeta" style="font-size:12px;color:#b8c2d0;">—</span>
  </div>
  <div class="player-stage">
    <figure><img id="playerOrig" class="pix" alt="original frame"><figcaption>Original (pixelado 4×)</figcaption></figure>
    <figure><img id="playerHd" alt="hd frame"><figcaption>ComfyUI HD (4×)</figcaption></figure>
  </div>
</details>

<div class="grid" id="grid">
{chr(10).join(cards)}
</div>

<script>
const INTERACTIVE = {str(interactive).lower()};
let playFrames = [];
let playIdx = 0;
let playTimer = null;

function visibleCards() {{
  return Array.from(document.querySelectorAll('.card')).filter(el => el.style.display !== 'none');
}}

function rebuildPlayerList() {{
  const vis = visibleCards();
  const sprites = vis.filter(el => el.dataset.category === 'sprites');
  playFrames = sprites.length ? sprites : vis;
  if (playIdx >= playFrames.length) playIdx = 0;
  renderPlayerFrame();
}}

function renderPlayerFrame() {{
  const meta = document.getElementById('playerMeta');
  const origImg = document.getElementById('playerOrig');
  const hdImg = document.getElementById('playerHd');
  if (!playFrames.length) {{
    meta.textContent = 'Nenhum quadro visível para reproduzir.';
    origImg.removeAttribute('src');
    hdImg.removeAttribute('src');
    return;
  }}
  const el = playFrames[playIdx % playFrames.length];
  origImg.src = el.dataset.orig;
  hdImg.src = el.dataset.hd;
  const iou = parseFloat(el.dataset.iou || '1').toFixed(4);
  meta.textContent = `Quadro ${{playIdx + 1}}/${{playFrames.length}} · ${{el.dataset.context}} · ${{el.dataset.key.slice(0, 12)}} · ${{el.dataset.dims}} · IoU ${{iou}} · ${{el.dataset.status}}`;
}}

function stepPlayer(delta) {{
  if (!playFrames.length) return;
  playIdx = (playIdx + delta + playFrames.length) % playFrames.length;
  renderPlayerFrame();
}}

function restartTimer() {{
  if (playTimer) {{
    clearInterval(playTimer);
    playTimer = null;
  }}
  const playing = document.getElementById('playBtn').textContent === 'Pause';
  if (playing) {{
    const fps = Math.max(1, parseInt(document.getElementById('fpsSelect').value || '6', 10));
    playTimer = setInterval(() => stepPlayer(1), Math.round(1000 / fps));
  }}
}}

function togglePlay() {{
  const btn = document.getElementById('playBtn');
  btn.textContent = (btn.textContent === 'Play') ? 'Pause' : 'Play';
  restartTimer();
}}

function filterCards() {{
  const cat = document.getElementById('catFilter').value;
  const ctx = document.getElementById('ctxFilter').value;
  const st = document.getElementById('statusFilter').value;
  const maxIou = parseFloat(document.getElementById('iouFilter').value || '1');
  let vis = 0;
  for (const el of document.querySelectorAll('.card')) {{
    const iou = parseFloat(el.dataset.iou || '1');
    const ok = (!cat || el.dataset.category === cat) &&
               (!ctx || el.dataset.context === ctx) &&
               (!st || el.dataset.status === st) &&
               (isNaN(maxIou) || iou <= maxIou + 1e-9);
    el.style.display = ok ? '' : 'none';
    if (ok) vis++;
  }}
  document.getElementById('cnt-visible').textContent = String(vis);
  rebuildPlayerList();
}}

function resetFilters() {{
  document.getElementById('catFilter').value = '';
  document.getElementById('ctxFilter').value = '';
  document.getElementById('statusFilter').value = '';
  document.getElementById('iouFilter').value = '1.00';
  filterCards();
}}

function applySummary(summary) {{
  if (!summary) return;
  document.getElementById('cnt-total').textContent = String(summary.total_entries ?? 0);
  document.getElementById('cnt-generated').textContent = String(summary.generated_count ?? 0);
  document.getElementById('cnt-reviewed').textContent = String(summary.reviewed_count ?? 0);
  document.getElementById('cnt-approved').textContent = String(summary.approved_count ?? 0);
  document.getElementById('cnt-rejected').textContent = String(summary.rejected_count ?? 0);
}}

async function updateStatus(keys, status) {{
  if (!INTERACTIVE || !keys.length) return;
  const resp = await fetch('/api/status', {{
    method: 'POST',
    headers: {{ 'Content-Type': 'application/json' }},
    body: JSON.stringify({{ keys, status }}),
  }});
  const data = await resp.json();
  if (!resp.ok || !data.ok) {{
    alert('Erro ao atualizar status: ' + (data.error || resp.statusText));
    return;
  }}
  const keySet = new Set(keys);
  for (const el of document.querySelectorAll('.card')) {{
    if (keySet.has(el.dataset.key)) {{
      el.dataset.status = status;
      const badge = el.querySelector('.badge');
      if (badge) {{
        badge.className = 'badge status-' + status;
        badge.textContent = status;
      }}
    }}
  }}
  applySummary(data.summary);
  filterCards();
}}

function setCardStatus(key, status) {{
  updateStatus([key], status);
}}

function batchStatus(status) {{
  const keys = visibleCards().map(el => el.dataset.key);
  if (!keys.length) return;
  if (!window.confirm(`Atualizar ${{keys.length}} asset(s) visível(is) para '${{status}}'?`)) return;
  updateStatus(keys, status);
}}

rebuildPlayerList();
</script>
</body>
</html>
"""


def update_manifest_statuses(
    manifest_path: Path,
    keys: list[str],
    status: str,
) -> tuple[int, dict[str, Any]]:
    if status not in VALID_STATUSES:
        raise ValueError(f"Status inválido: {status}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    entries = manifest.get("entries", {})
    for k in keys:
        if k not in entries or not entries[k].get("hd_file"):
            raise KeyError(f"Chave inexistente ou sem hd_file: {k}")
    for k in keys:
        entries[k]["status"] = status
    summary = save_manifest_atomic(manifest_path, manifest)
    return len(keys), summary


def make_handler(manifest_path: Path, lock: threading.Lock) -> type[BaseHTTPRequestHandler]:
    class ReviewHandler(BaseHTTPRequestHandler):
        def _send_bytes(self, code: int, body: bytes, content_type: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def _send_json(self, code: int, obj: Any) -> None:
            self._send_bytes(code, (json.dumps(obj, ensure_ascii=False) + "\n").encode("utf-8"), "application/json; charset=utf-8")

        def do_GET(self) -> None:  # noqa: N802
            req_path = urllib.parse.urlsplit(self.path).path
            if req_path in ("/", "/index.html"):
                with lock:
                    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                page = render_gallery_html(manifest, ROOT / "assets" / "review", interactive=True)
                self._send_bytes(200, page.encode("utf-8"), "text/html; charset=utf-8")
                return

            if req_path == "/api/summary":
                with lock:
                    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                self._send_json(200, recompute_summary(manifest))
                return

            if req_path.startswith("/assets/"):
                rel_raw = urllib.parse.unquote(req_path[len("/assets/"):])
                if not rel_raw or "\x00" in rel_raw or ":" in rel_raw:
                    self._send_json(403, {"error": "forbidden path"})
                    return
                rel_path = Path(rel_raw)
                if rel_path.is_absolute() or ".." in rel_path.parts:
                    self._send_json(403, {"error": "path traversal blocked"})
                    return
                try:
                    target = (ASSETS_ROOT / rel_path).resolve()
                    target.relative_to(ASSETS_ROOT)
                except ValueError:
                    self._send_json(403, {"error": "outside assets root"})
                    return
                if not target.is_file():
                    self._send_json(404, {"error": "not found"})
                    return
                ctype, _ = mimetypes.guess_type(str(target))
                self._send_bytes(200, target.read_bytes(), ctype or "application/octet-stream")
                return

            self._send_json(404, {"error": "not found"})

        def do_POST(self) -> None:  # noqa: N802
            req_path = urllib.parse.urlsplit(self.path).path
            if req_path != "/api/status":
                self._send_json(404, {"error": "not found"})
                return
            try:
                clen = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self._send_json(400, {"error": "invalid Content-Length"})
                return
            if clen <= 0 or clen > 4 * 1024 * 1024:
                self._send_json(400, {"error": "invalid body size"})
                return
            try:
                payload = json.loads(self.rfile.read(clen).decode("utf-8"))
                keys = payload.get("keys")
                status = payload.get("status")
                if not isinstance(keys, list) or not keys or not all(isinstance(k, str) and k for k in keys):
                    self._send_json(400, {"error": "keys must be a non-empty list of strings"})
                    return
                if status not in VALID_STATUSES:
                    self._send_json(400, {"error": f"invalid status: {status}"})
                    return
                with lock:
                    updated, summary = update_manifest_statuses(manifest_path, keys, status)
                self._send_json(200, {"ok": True, "updated": updated, "summary": summary})
            except KeyError as exc:
                self._send_json(400, {"error": str(exc)})
            except Exception as exc:
                self._send_json(400, {"error": str(exc)})

        def log_message(self, fmt: str, *args: Any) -> None:
            return

    return ReviewHandler


def run_cli_curation(args: argparse.Namespace) -> int:
    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    entries = manifest.get("entries", {})
    matched: list[str] = []
    explicit_keys = set(args.keys) if args.keys else None

    for key, e in entries.items():
        if not e.get("hd_file") or not (ROOT / e["hd_file"]).exists():
            continue
        if explicit_keys is not None and key not in explicit_keys:
            continue
        if args.context and str(e.get("context", "")) != args.context:
            continue
        if args.category != "all" and layer_category(e) != args.category:
            continue
        matched.append(key)

    if explicit_keys is not None and len(matched) != len(explicit_keys):
        missing = sorted(explicit_keys - set(matched))
        print(f"ERRO: chaves não encontradas ou sem hd_file: {missing}", file=sys.stderr)
        return 1

    if args.dry_run:
        print(f"[DRY-RUN] {len(matched)} entradas selecionadas -> status='{args.set_status}'")
        return 0

    updated, summary = update_manifest_statuses(args.manifest, matched, args.set_status)
    print(f"Atualizadas {updated} entradas -> '{args.set_status}' | summary={summary}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Gerador e servidor interativo de revisão visual Original x HD")
    ap.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--serve", action="store_true", help="Inicia servidor HTTP local de curadoria interativa")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--set-status", choices=VALID_STATUSES, default=None, help="Atualiza status via CLI")
    ap.add_argument("--keys", nargs="*", default=None, help="Lista explícita de chaves para --set-status")
    ap.add_argument("--context", default=None, help="Filtro de contexto para --set-status")
    ap.add_argument("--category", choices=("all", "planes", "sprites", "tiles"), default="all")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if args.set_status is not None:
        return run_cli_curation(args)

    if args.serve:
        lock = threading.Lock()
        handler = make_handler(args.manifest, lock)
        server = ThreadingHTTPServer((args.host, args.port), handler)
        print(f"Servidor de revisão HD em http://{args.host}:{args.port}/ (manifest={args.manifest})", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()
        return 0

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    out_dir = args.out.parent.resolve()
    doc = render_gallery_html(manifest, out_dir, interactive=False)
    args.out.write_text(doc, encoding="utf-8")
    hd_count = sum(1 for e in manifest.get("entries", {}).values() if e.get("hd_file") and (ROOT / e["hd_file"]).exists())
    print(f"Galeria de revisão escrita em {args.out} ({hd_count} pares HD)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
