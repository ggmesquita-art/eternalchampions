"""Pranchas G2.3b: students B+ v3 (piloto) x teachers x B+ v2, recortes 1:1.

Uso (Python com Pillow):
    python tools/pipeline/student_board.py [--teachers animesharp,janai_esrgan,...]
    python tools/pipeline/student_board.py --final   # G2.4/G2.5: v1 x v2 x v3 fast/HQ x teacher
    python tools/pipeline/student_board.py --hq-style  # v3 HQ AnimeSharp x v3 HQ JaNai DAT2

Lê:
  assets/video_compare/upscalers_teacher/out/<cena>/<modelo teacher>.png e bplus_v2.png
  assets/video_compare/students_v3_pilot/<teacher>/<cena>_predither.png
Escreve:
  assets/video_compare/students_v3_pilot/board_<cena>_crops.png
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from upscaler_lab import REGIONS  # noqa: E402  (regiões de recorte nativas por cena)

LAB = ROOT / "assets" / "video_compare" / "upscalers_teacher"
PILOT = ROOT / "assets" / "video_compare" / "students_v3_pilot"
FINAL = ROOT / "assets" / "video_compare" / "v3_final"
TEACHER_FILES = {
    "animesharp": "4x-AnimeSharp.png",
    "janai_esrgan": "4x_IllustrationJaNai_V1_ESRGAN_135k.png",
    "janai_dat2": "4x_IllustrationJaNai_V1_DAT2_190k.png",
    "nomos_schatl": "4xNomos8kSCHAT-L.png",
}
SCENES = ["s2_sanctuary", "s5_trident", "s6_vietnam", "s7_salem"]
CW, CH, LABEL_H, ROW_LABEL_W = 256, 192, 18, 110


def region_list(scene: str) -> list[tuple[str, int, int]]:
    regs = REGIONS[scene]
    items = regs.items() if isinstance(regs, dict) else regs
    out = []
    for item in items:
        if isinstance(item, tuple) and len(item) == 2 and not isinstance(item[1], (int, float)):
            name, xy = item
            out.append((str(name), int(xy[0]), int(xy[1])))
        else:
            name, x, y = item[0], item[1], item[2]
            out.append((str(name), int(x), int(y)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--teachers", default=",".join(TEACHER_FILES))
    ap.add_argument("--final", action="store_true",
                    help="prancha final: nearest, B+ v1, B+ v2, v3 fast, v3 HQ, teacher AnimeSharp")
    ap.add_argument("--hq-style", action="store_true",
                    help="prancha de estilo HQ: v2, v3 HQ AnimeSharp/teacher, v3 HQ JaNai DAT2/teacher")
    args = ap.parse_args()
    teachers = [t for t in args.teachers.split(",") if t]

    for scene in SCENES:
        if args.hq_style:
            out_dir = FINAL / "hq_style"
            cols = [
                ("nearest", LAB / "out" / scene / "nearest_4x.png"),
                ("B+ v2", LAB / "out" / scene / "bplus_v2.png"),
                ("v3 HQ AnimeSharp", FINAL / "hq" / f"{scene}_predither.png"),
                ("AnimeSharp TEACHER", LAB / "out" / scene / TEACHER_FILES["animesharp"]),
                ("v3 HQ JaNai DAT2", FINAL / "hq_janai" / f"{scene}_predither.png"),
                ("JaNai DAT2 TEACHER", LAB / "out" / scene / TEACHER_FILES["janai_dat2"]),
            ]
        elif args.final:
            out_dir = FINAL
            cols: list[tuple[str, Path]] = [
                ("nearest", LAB / "out" / scene / "nearest_4x.png"),
                ("B+ v1", LAB / "out" / scene / "bplus_v1.png"),
                ("B+ v2", LAB / "out" / scene / "bplus_v2.png"),
                ("B+ v3 fast (40/3)", FINAL / "fast" / f"{scene}_predither.png"),
                ("B+ v3 HQ (56/4)", FINAL / "hq" / f"{scene}_predither.png"),
                ("AnimeSharp TEACHER", LAB / "out" / scene / TEACHER_FILES["animesharp"]),
            ]
        else:
            out_dir = PILOT
            cols = [("bplus_v2", LAB / "out" / scene / "bplus_v2.png")]
            for t in teachers:
                cols.append((f"{t} TEACHER", LAB / "out" / scene / TEACHER_FILES[t]))
                cols.append((f"{t} STUDENT v3", PILOT / t / f"{scene}_predither.png"))
        cols = [(n, p) for n, p in cols if p.exists()]
        if (args.final or args.hq_style) and len(cols) < 6:
            continue  # cena sem saida v3 (sem input predither)
        imgs = {n: Image.open(p).convert("RGB") for n, p in cols}
        regs = region_list(scene)
        W = ROW_LABEL_W + CW * len(cols)
        H = LABEL_H + CH * len(regs)
        board = Image.new("RGB", (W, H), (24, 24, 24))
        d = ImageDraw.Draw(board)
        for ci, (n, _) in enumerate(cols):
            d.text((ROW_LABEL_W + ci * CW + 4, 3), n, fill=(255, 220, 120))
        for ri, (rn, nx, ny) in enumerate(regs):
            y0 = LABEL_H + ri * CH
            d.text((4, y0 + 4), rn, fill=(160, 230, 160))
            for ci, (n, _) in enumerate(cols):
                x, y = nx * 4, ny * 4
                board.paste(imgs[n].crop((x, y, x + CW, y + CH)), (ROW_LABEL_W + ci * CW, y0))
        out_dir.mkdir(parents=True, exist_ok=True)
        out = out_dir / f"board_{scene}_crops.png"
        board.save(out, optimize=True)
        print(out, board.size)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
