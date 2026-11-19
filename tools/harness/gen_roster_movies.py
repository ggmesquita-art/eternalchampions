#!/usr/bin/env python3
"""Generate deterministic .ecm input movies covering all 9 fighters, Eternal Champion, all arenas
and the extra modes (2P VS, Tournament Round-A-Bout, 2P match end)."""

from __future__ import annotations

import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_ROM = ROOT / "roms" / "eternal_champions_us.gen"
MOVIES_DIR = ROOT / "harness" / "movies"

BIT_B = 0x0001
BIT_A = 0x0002
BIT_START = 0x0008
BIT_UP = 0x0010
BIT_DOWN = 0x0020
BIT_LEFT = 0x0040
BIT_RIGHT = 0x0080
BIT_C = 0x0100

PRACTICE_SPECS = [
    # (filename, p1_id, p2_id, bg_idx, expected_state, desc)
    ("03_practice_rax_vs_blade_s21.ecm", 1, 2, 1, 21, "RAX vs Blade (Blade Arena S21)"),
    ("04_practice_blade_vs_jetta_s22.ecm", 2, 3, 2, 22, "Blade vs Jetta (Jetta Arena S22)"),
    ("05_practice_jetta_vs_slash_s26.ecm", 3, 4, 7, 26, "Jetta vs Slash (Slash Arena S26)"),
    ("06_practice_slash_vs_trident_s24.ecm", 4, 5, 8, 24, "Slash vs Trident (Trident Arena S24)"),
    ("07_practice_trident_vs_xavier_s20.ecm", 5, 6, 9, 20, "Trident vs Xavier (Xavier Arena S20)"),
    ("08_practice_xavier_vs_midknight_s19.ecm", 6, 7, 4, 19, "Xavier vs Midknight (Midknight Arena S19)"),
    ("09_practice_midknight_vs_larcen_s27.ecm", 7, 8, 3, 27, "Midknight vs Larcen (Larcen Arena S27)"),
    ("10_practice_larcen_vs_rax_s23.ecm", 8, 1, 5, 23, "Larcen vs RAX (RAX Arena S23)"),
    ("11_practice_shadow_vs_larcen_s25.ecm", 0, 8, 6, 25, "Shadow vs Larcen (Shadow Arena S25)"),
]


def add_tap(masks: list[int], start: int, bit: int, hold: int = 8) -> int:
    for f in range(start, start + hold):
        if 1 <= f < len(masks):
            masks[f] |= bit
    return start + 20


def add_combat_sequence(masks: list[int], start: int) -> None:
    """Apply varied fighter movement and A/B/C/special inputs after round start."""
    seq = [
        (0, BIT_RIGHT, 14),
        (20, BIT_A, 8),
        (45, BIT_RIGHT | BIT_B, 8),
        (75, BIT_DOWN | BIT_C, 10),
        (105, BIT_LEFT, 12),
        (130, BIT_B, 8),
        (155, BIT_RIGHT | BIT_C, 10),
        (185, BIT_UP | BIT_RIGHT, 12),
        (210, BIT_A, 8),
        (235, BIT_LEFT | BIT_C, 10),
        (265, BIT_DOWN | BIT_B, 8),
    ]
    for off, bit, dur in seq:
        for f in range(start + off, min(len(masks), start + off + dur)):
            masks[f] |= bit


def build_base_header(total_frames: int) -> list[int]:
    masks = [0] * total_frames
    # Boot -> Sega/Title -> State 9 -> State 1 Main Menu -> One Player
    for f in (800, 900, 1240, 1400):
        for k in range(f, f + 10):
            masks[k] = BIT_START
    return masks


def build_practice_movie(p1_id: int, p2_id: int, bg_idx: int) -> tuple[list[int], int]:
    # Worst-case navigation frames: Opponent<=8, Bg<=9, CharSel<=8 -> round start around ~2700..3000
    total_frames = 3400
    masks = build_base_header(total_frames)
    t = 1540
    t = add_tap(masks, t, BIT_START, 10)  # Fight
    t += 120
    t = add_tap(masks, t, BIT_START, 10)  # Practice
    t += 120
    for _ in range(p2_id):
        t = add_tap(masks, t, BIT_RIGHT)
    t = add_tap(masks, t, BIT_DOWN)
    for _ in range(bg_idx):
        t = add_tap(masks, t, BIT_RIGHT)
    t = add_tap(masks, t, BIT_UP)
    t = add_tap(masks, t, BIT_UP)
    t = add_tap(masks, t, BIT_START, 10)  # Enter CharSelect
    t += 140
    for _ in range(p1_id):
        t = add_tap(masks, t, BIT_RIGHT)
    t = add_tap(masks, t, BIT_START, 10)  # Confirm P1 -> transition to arena
    combat_start = t + 360
    add_combat_sequence(masks, combat_start)
    return masks, total_frames


def build_holo_trainer_movie() -> tuple[list[int], int]:
    total_frames = 2900
    masks = build_base_header(total_frames)
    t = 1540
    t = add_tap(masks, t, BIT_DOWN, 10)
    t = add_tap(masks, t, BIT_DOWN, 10)
    t = add_tap(masks, t, BIT_START, 10)  # Training Mode
    t += 120
    t = add_tap(masks, t, BIT_DOWN, 10)   # Holo-Trainer
    t = add_tap(masks, t, BIT_START, 10)  # Holo-Trainer options
    t += 120
    t = add_tap(masks, t, BIT_UP, 10)     # Download Hologram
    t = add_tap(masks, t, BIT_START, 10)  # Select Download Hologram
    t += 140
    t = add_tap(masks, t, BIT_START, 10)  # Confirm P1 (Shadow)
    add_combat_sequence(masks, 2450)
    return masks, total_frames


def build_boss_sanctuary_movie() -> tuple[list[int], int]:
    total_frames = 3000
    masks = build_base_header(total_frames)
    t = 1540
    t = add_tap(masks, t, BIT_START, 10)  # Fight
    t += 120
    t = add_tap(masks, t, BIT_DOWN, 10)   # Enter Contest
    t = add_tap(masks, t, BIT_START, 10)  # Select Enter Contest
    t += 150
    t = add_tap(masks, t, BIT_START, 10)  # Confirm P1 (Shadow); EC_DIAG_BOSS_SANCTUARY forces S18/P2=9
    add_combat_sequence(masks, 2520)
    return masks, total_frames


def build_2p_header(total_frames: int) -> list[int]:
    """Boot -> Title -> State 9 -> Main Menu (One Player / Two Players / Tournament Mode / ...)."""
    masks = [0] * total_frames
    for f in (800, 900, 1240):
        add_tap(masks, f, BIT_START, 10)
    return masks


def build_2p_select(masks: list[int], masks2: list[int], p1_id: int, p2_id: int, bg_idx: int) -> int:
    """Main Menu -> Two Players -> Fight -> Options (fixed Background, so the arena does not
    depend on the HV-counter RNG) -> Exit -> Fight -> character select (both pads).
    Returns the frame of the P1 confirm."""
    add_tap(masks, 1400, BIT_DOWN)
    add_tap(masks, 1420, BIT_START, 10)  # Two Players
    add_tap(masks, 1560, BIT_START, 10)  # Fight
    add_tap(masks, 1700, BIT_DOWN)
    add_tap(masks, 1720, BIT_START, 10)  # Options
    t = 1880
    for _ in range(bg_idx):
        t = add_tap(masks, t, BIT_RIGHT)  # Background: Random -> bg list index
    t = add_tap(masks, t, BIT_UP)  # cursor wraps to Exit
    add_tap(masks, t, BIT_START, 10)  # Exit -> Fight menu
    t += 150
    add_tap(masks, t, BIT_START, 10)  # Fight -> character select
    t += 160
    t1 = t
    for _ in range(p1_id):
        t1 = add_tap(masks, t1, BIT_RIGHT)
    add_tap(masks, t1, BIT_START, 10)  # P1 confirm
    t2 = t
    for _ in range(p2_id):
        t2 = add_tap(masks2, t2, BIT_RIGHT)
    add_tap(masks2, max(t2, t1) + 30, BIT_START, 10)  # P2 confirm after P1
    return t1


def add_p2_combat_sequence(masks2: list[int], start: int) -> None:
    """Mirrored P2 movement/attacks (P2 starts on the right side)."""
    seq = [
        (0, BIT_LEFT, 14),
        (30, BIT_B, 8),
        (60, BIT_LEFT | BIT_A, 8),
        (95, BIT_DOWN | BIT_B, 10),
        (125, BIT_RIGHT, 12),
        (150, BIT_C, 8),
        (180, BIT_LEFT | BIT_C, 10),
        (210, BIT_UP | BIT_LEFT, 12),
        (240, BIT_B, 8),
    ]
    for off, bit, dur in seq:
        for f in range(start + off, min(len(masks2), start + off + dur)):
            masks2[f] |= bit


def build_2p_vs_movie(p1_id: int, p2_id: int, bg_idx: int) -> tuple[list[int], list[int]]:
    total_frames = 3900
    masks = build_2p_header(total_frames)
    masks2 = [0] * total_frames
    confirm = build_2p_select(masks, masks2, p1_id, p2_id, bg_idx)
    add_combat_sequence(masks, confirm + 580)  # round starts ~500 frames after the confirm
    add_p2_combat_sequence(masks2, confirm + 600)
    return masks, masks2


def build_tournament_movie(p1_id: int, p2_id: int) -> tuple[list[int], list[int]]:
    """Main Menu -> Tournament Mode -> Round-A-Bout -> Enter Tournament -> 2P select."""
    total_frames = 3400
    masks = build_2p_header(total_frames)
    masks2 = [0] * total_frames
    add_tap(masks, 1400, BIT_DOWN)
    add_tap(masks, 1420, BIT_DOWN)
    add_tap(masks, 1440, BIT_START, 10)  # Tournament Mode
    add_tap(masks, 1580, BIT_START, 10)  # Round-A-Bout
    for f in (1760, 1780, 1800):
        add_tap(masks, f, BIT_DOWN)  # Match Condition / P1 Skill / P2 Skill -> Enter Tournament
    add_tap(masks, 1850, BIT_START, 10)  # Enter Tournament -> character select
    t = 2010
    for _ in range(p1_id):
        t = add_tap(masks, t, BIT_RIGHT)
    add_tap(masks, t + 10, BIT_START, 10)
    t2 = 2010
    for _ in range(p2_id):
        t2 = add_tap(masks2, t2, BIT_RIGHT)
    add_tap(masks2, max(t2, t + 10) + 20, BIT_START, 10)
    add_combat_sequence(masks, 2650)
    add_p2_combat_sequence(masks2, 2670)
    return masks, masks2


def build_2p_match_end_movie(p1_id: int, p2_id: int, bg_idx: int) -> tuple[list[int], list[int]]:
    """2P VS: P1 walks in and attacks an idle P2 until the final-round KO ("WINS COMPETITION")
    and the post-match return to State 1 (end-of-match / Overkill-stage flow)."""
    total_frames = 7000
    masks = build_2p_header(total_frames)
    masks2 = [0] * total_frames
    confirm = build_2p_select(masks, masks2, p1_id, p2_id, bg_idx)
    attacks = (BIT_A, BIT_B, BIT_C)
    f, i = confirm + 480, 0
    while f < confirm + 3850:
        add_tap(masks, f, BIT_RIGHT, 20)
        add_tap(masks, f + 26, attacks[i % 3], 6)
        add_tap(masks, f + 50, attacks[(i + 1) % 3], 6)
        f += 80
        i += 1
    return masks, masks2


def build_2p_overkill_movie(
    p1_id: int, p2_id: int, bg_idx: int, walk_frames: int, total_frames: int
) -> tuple[list[int], list[int]]:
    """2P VS Overkill (Trident Arena S24). Same match as `build_2p_match_end_movie` until P2 is
    at 6 HP against the right wall in the final round (confirm + 3485); then both pads walk left
    for `walk_frames` so P2 stands in the Overkill zone and P1 lands a heavy C.

    Overkill rule (combat_core $5E50A -> $269E4 -> per-arena handler $26D74): decisive round
    ($1F15C), loser's hit strength `$128 >= $B`, loser X inside the arena zone at the KO frame
    (Trident: [$10A,$140] when `$E6` bit 3 is set, else [$309,$33F]) -> `$9C7C = 1`."""
    masks, masks2 = build_2p_match_end_movie(p1_id, p2_id, bg_idx)
    masks = masks[:total_frames] + [0] * max(0, total_frames - len(masks))
    masks2 = masks2[:total_frames] + [0] * max(0, total_frames - len(masks2))
    confirm = build_2p_select([0] * total_frames, [0] * total_frames, p1_id, p2_id, bg_idx)
    stop = confirm + 3485  # P2 HP 6, X 929 (measured on movie 16)
    for f in range(stop, total_frames):
        masks[f] = 0
    walk = stop + 20
    for f in range(walk, walk + walk_frames):
        masks[f] |= BIT_LEFT
        masks2[f] |= BIT_LEFT
    f = walk + walk_frames + 10
    for _ in range(6):
        add_tap(masks, f, BIT_RIGHT, 8)
        add_tap(masks, f + 14, BIT_C, 8)
        f += 60
    return masks, masks2


def write_ecm(
    path: Path, rom_sha256: str, masks: list[int], comment: str, masks2: list[int] | None = None
) -> None:
    """Write an .ecm movie. With `masks2`, each line carries a 2nd hex column (pad 2)."""
    if masks2 is not None and len(masks2) != len(masks):
        raise ValueError(f"{path.name}: masks2 ({len(masks2)}) != masks ({len(masks)})")
    lines = [
        f"# {comment}",
        f"rom {rom_sha256}",
        f"frames {len(masks)}",
    ]
    if masks2 is None:
        lines.extend(f"{m:04x}" for m in masks)
    else:
        lines.extend(f"{m:04x} {m2:04x}" for m, m2 in zip(masks, masks2))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    rom_sha256 = hashlib.sha256(DEFAULT_ROM.read_bytes()).hexdigest()
    MOVIES_DIR.mkdir(parents=True, exist_ok=True)

    for fname, p1, p2, bg_idx, state, desc in PRACTICE_SPECS:
        masks, _ = build_practice_movie(p1, p2, bg_idx)
        write_ecm(MOVIES_DIR / fname, rom_sha256, masks, f"{desc} (P1={p1}, P2={p2}, S{state})")
        print(f"Wrote {fname}: {len(masks)} frames (P1={p1}, P2={p2}, bg={bg_idx} -> S{state})")

    holo_masks, _ = build_holo_trainer_movie()
    write_ecm(MOVIES_DIR / "12_holo_trainer_s14.ecm", rom_sha256, holo_masks, "Holo-Trainer (Shadow S14)")
    print(f"Wrote 12_holo_trainer_s14.ecm: {len(holo_masks)} frames (S14)")

    boss_masks, _ = build_boss_sanctuary_movie()
    write_ecm(
        MOVIES_DIR / "13_boss_sanctuary_s18.ecm",
        rom_sha256,
        boss_masks,
        "Eternal Champion Sanctuary Boss (Shadow vs Eternal Champion S18; EC_DIAG_BOSS_SANCTUARY=1)",
    )
    print(f"Wrote 13_boss_sanctuary_s18.ecm: {len(boss_masks)} frames (S18)")

    vs_m1, vs_m2 = build_2p_vs_movie(1, 2, 8)
    write_ecm(
        MOVIES_DIR / "14_2p_vs_rax_vs_blade_s24.ecm",
        rom_sha256,
        vs_m1,
        "2P VS: RAX (pad 1) vs Blade (pad 2) (Trident Arena S24)",
        vs_m2,
    )
    print(f"Wrote 14_2p_vs_rax_vs_blade_s24.ecm: {len(vs_m1)} frames (S24, 2 pads)")

    tr_m1, tr_m2 = build_tournament_movie(3, 6)
    write_ecm(
        MOVIES_DIR / "15_tournament_roundabout_jetta_vs_xavier_s27.ecm",
        rom_sha256,
        tr_m1,
        "Tournament Round-A-Bout: Jetta (pad 1) vs Xavier (pad 2) (Larcen Arena S27)",
        tr_m2,
    )
    print(f"Wrote 15_tournament_roundabout_jetta_vs_xavier_s27.ecm: {len(tr_m1)} frames (S27, 2 pads)")

    me_m1, me_m2 = build_2p_match_end_movie(1, 2, 8)
    write_ecm(
        MOVIES_DIR / "16_2p_vs_match_end_s24_to_s1.ecm",
        rom_sha256,
        me_m1,
        "2P VS match end: RAX KOs Blade in the final round -> WINS COMPETITION -> State 1",
        me_m2,
    )
    print(f"Wrote 16_2p_vs_match_end_s24_to_s1.ecm: {len(me_m1)} frames (S24 -> S1, 2 pads)")

    ok_m1, ok_m2 = build_2p_overkill_movie(1, 2, 8, walk_frames=325, total_frames=6900)
    write_ecm(
        MOVIES_DIR / "17_2p_vs_overkill_trident_s24.ecm",
        rom_sha256,
        ok_m1,
        "2P VS Overkill: RAX KOs Blade inside the Trident Arena zone (S24, $9C7C=1 at the end)",
        ok_m2,
    )
    print(f"Wrote 17_2p_vs_overkill_trident_s24.ecm: {len(ok_m1)} frames (S24 Overkill, 2 pads)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
