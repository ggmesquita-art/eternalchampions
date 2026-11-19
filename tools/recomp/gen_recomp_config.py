#!/usr/bin/env python3
"""tools/recomp/gen_recomp_config.py — gera game/ec_code_addrs.txt e game/ec_discovery.toml
a partir da ROM e do mapeamento CFG/tabelas da Fase 1 (DESIGN §§6–7).
"""

from __future__ import annotations

import json
import pathlib
import re
import struct
import tomllib

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000, Cs

ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
ROM_PATH = ROOT / "roms" / "eternal_champions_us.gen"
REA_INDEX_PATH = ROOT / "reference" / "rea" / "decompile-2026-11-18" / "decompile-index.json"
LABELS_PATH = ROOT / "disasm" / "symbols" / "labels.toml"
OUT_CODE_ADDRS = ROOT / "game" / "ec_code_addrs.txt"
OUT_DISCOVERY = ROOT / "game" / "ec_discovery.toml"

INLINE_WORDS = {0xF42A: 4, 0xF47A: 5, 0xFFAC: 5}
INLINE_TERM = {0xE332: 0xFFFF, 0xE3EE: 0xFFFF, 0xE5FC: 0xFFFF, 0xE61E: 0xFFFF}
YIELD_APIS = {0xDB0E, 0xDB14, 0xDB28, 0xDB2E, 0xDB42, 0xDB46, 0xDB4A, 0xDC10, 0x17192, 0x17196, 0x171AE}

TASK_A0_APIS = {0xDA9A, 0xDB0E, 0xDB14, 0xDB28, 0xDB4E, 0xDB8E, 0xDB96, 0xDBCA, 0xDBFE}
TASK_D0_APIS = {0xDA9A, 0xDB1E, 0xDBAC, 0xDBD8, 0xDBF0, 0xDBF6, 0xDBFC, 0xDBFE}
# Variáveis de RAM que guardam ponteiros de callback chamados via `jsr (a0)`
# ($968E: `movea.l ($968E).w,a0; jsr (a0)` em $1760A/$17E18; recebe $3533E/$3547C).
RAM_CALLBACK_VARS = {0x968E}

BRANCH_MNEMS = {
    "bra", "bsr", "bhi", "bls", "bcc", "bcs", "bne", "beq",
    "bvc", "bvs", "bpl", "bmi", "bge", "blt", "bgt", "ble",
    "bhs", "blo",
}
TERM_MNEMS = {"rts", "rte", "rtr", "illegal", "stop"}

CODE_RANGES = [
    (0x000200, 0x000A80),
    (0x002230, 0x026E4A),
    (0x028B12, 0x032194),
    (0x034B0A, 0x05E8A4),
    (0x2FCD1A, 0x2FF438),
]

PROTECTED_RANGES = [
    (0x000A80, 0x002230, "Programa do Z80 + dados"),
    (0x026E4A, 0x028B12, "Textos comprimidos (finais e créditos)"),
    (0x032194, 0x034B0A, "Textos comprimidos (biografias)"),
    (0x05E8A4, 0x2FCD1A, "Gráficos, animações, tabelas ($06A522/$07F65A), áudio"),
    (0x2FF438, 0x300000, "Tabelas de dados finais / padding"),
]

ABS_JUMP_TABLES = [
    (0x05BC38, 0x05BC60, 4, "abs", "Combat AI/action jump table 1"),
    (0x05BF94, 0x05BFBC, 4, "abs", "Combat AI/action jump table 2"),
    (0x2FD21C, 0x2FD25C, 4, "abs", "Replay sub-table 1"),
    (0x2FD786, 0x2FD7C6, 4, "abs", "Replay sub-table 2"),
    (0x2FEFF8, 0x2FF004, 4, "abs", "Replay HUD/stats table 1"),
    (0x2FF09E, 0x2FF0AA, 4, "abs", "Replay HUD/stats table 2"),
    (0x2FF194, 0x2FF1A0, 4, "abs", "Replay HUD/stats table 3"),
    (0x2FF242, 0x2FF25A, 4, "abs", "Replay HUD/stats table 4"),
]

BRA_JUMP_TABLES = [
    (0x00C5D0, 0x00C5DA, 2, "bra_s", "Sub_C578 switch ($C5CC)"),
    (0x00CC6A, 0x00CC7E, 4, "bra_w", "Sub_CC66 switch ($CC66)"),
    (0x00E8A8, 0x00E8B2, 2, "bra_s", "Sub_E884 object state switch ($E8A4)"),
    (0x00F56A, 0x00F586, 4, "bra_w", "Sub_F56A H-scroll mode switch"),
    (0x00F5EA, 0x00F606, 4, "bra_w", "Sub_F5EA V-scroll mode switch"),
    (0x037E46, 0x037E4C, 2, "bra_s", "Fighter AI state switch ($37E42)"),
    (0x2FE00E, 0x2FE01A, 2, "bra_s", "Replay HUD switch ($2FE00A)"),
    (0x012CA2, 0x012CCA, 4, "bra_w", "Object switch ($12C8A)"),
    (0x026A40, 0x026A64, 4, "bra_w", "Scene/state handler switch ($26A2C)"),
    # Trampolins `jmp (base,pc,Dn.w)` + `bra.w` sem cobertura estática
    # (ec_dispatch_audit.log: fallback_hybrid/bra_w_trampoline). Sem estes, um
    # índice nunca visto antes (ex.: $024CEC, fim de partida) vira dispatch miss.
    (0x013566, 0x0135E2, 4, "bra_w", "Object switch ($13562, índice por tabela de bytes)"),
    (0x01371A, 0x01372E, 4, "bra_w", "P2 input-mode switch ($13716)"),
    (0x0200BA, 0x0200CA, 4, "bra_w", "Task switch ($200B6)"),
    (0x020364, 0x020384, 4, "bra_w", "Task switch ($20360)"),
    (0x0208CE, 0x0208EA, 4, "bra_w", "Task switch ($208CA)"),
    (0x021968, 0x021978, 4, "bra_w", "Task switch ($21964)"),
    (0x02306A, 0x023076, 4, "bra_w", "Task switch ($23066)"),
    (0x023184, 0x023190, 4, "bra_w", "Task switch ($23180)"),
    (0x024248, 0x024258, 4, "bra_w", "Task switch ($24244)"),
    (0x024CCC, 0x024CF0, 4, "bra_w", "Victory/result task switch ($24CC8)"),
    (0x024F1E, 0x024F32, 4, "bra_w", "Task switch ($24F1A)"),
    (0x025F7C, 0x025FD4, 4, "bra_w", "Task switch ($25F78)"),
    (0x028EAA, 0x028EB6, 4, "bra_w", "Round setup switch ($28EA6)"),
    (0x02D39A, 0x02D3C2, 4, "bra_w", "Menu switch ($2D396)"),
    (0x039E94, 0x039EB4, 4, "bra_w", "Fighter switch ($39E90)"),
    (0x03C034, 0x03C054, 4, "bra_w", "Fighter switch ($3C030)"),
    (0x03FD58, 0x03FD78, 4, "bra_w", "Fighter switch ($3FD54)"),
    (0x040172, 0x040182, 4, "bra_w", "Fighter switch ($4016E)"),
    (0x0401F4, 0x040214, 4, "bra_w", "Fighter switch ($401F0)"),
]

PCREL16_JUMP_TABLES = [
    (0x010C6E, 0x010E6E, 2, "pcrel16", "Sub_10B74 HV-sync VM opcode table"),
]


def in_code_regions(a: int) -> bool:
    return (a % 2 == 0) and any(lo <= a < hi for lo, hi in CODE_RANGES)


def is_valid_code_start(rom: bytes, md: Cs, a: int, allow_text: bool = False) -> bool:
    """allow_text=True: o endereço vem de um ponteiro de código explícito (ex.:
    `movea.l #imm,a0` + `jsr` API de task), então a heurística de texto ASCII não
    se aplica — $05E2FE (`4a 6d 00 68 6f 00 00 74`) era descartado por ela."""
    if not in_code_regions(a):
        return False
    chunk = rom[a : a + 8]
    if (
        not allow_text
        and all(0x20 <= b <= 0x7E or b == 0 for b in chunk)
        and sum(0x41 <= b <= 0x7A or b == 0x20 for b in chunk) >= 4
    ):
        return False
    res = list(md.disasm(rom[a : a + 16], a, count=2))
    if not res:
        return False
    first = res[0]
    if first.bytes[:2] in (b"\x00\x00", b"\xff\xff"):
        return False
    if (
        first.mnemonic.endswith(".b")
        and first.mnemonic.split(".")[0] in ("ori", "andi", "subi", "addi", "eori", "cmpi")
        and len(first.bytes) >= 4
        and first.bytes[2] != 0
    ):
        return False
    return True


def build_cfg_and_leads(rom: bytes) -> dict:
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    md.detail = False

    core_extra: set[int] = {
        0x000200, 0x0006B6, 0x0006DE, 0x0006E2,
        0x002230, 0x00FD24, 0x00FE1E,
        0x00C870, 0x00CA80, 0x00CB42, 0x00D084, 0x00D4CC, 0x00D6A0,
        0x00D7E4, 0x00DB14, 0x00DC4E, 0x00DCEA, 0x00E684, 0x00F6A0,
        0x0109EA, 0x010AA8, 0x010B74, 0x010BE2, 0x010BFE, 0x011414,
        0x016F32, 0x0172E6, 0x0182A0, 0x01FF00, 0x02644C, 0x0312B8,
        0x057F64, 0x05AE6E, 0x05AEC6, 0x05B01A, 0x05B25E, 0x05B618,
        0x05B6B2, 0x05DF02,
        0x2FCD1A, 0x2FCD56, 0x2FD050, 0x2FD0AE, 0x2FDCEE, 0x2FE2A4,
    }

    # Vetores de exceção
    for i in range(1, 64):
        v = struct.unpack(">I", rom[i * 4 : i * 4 + 4])[0]
        if in_code_regions(v):
            core_extra.add(v)

    # Tabela de estados (40 handlers)
    state_handlers: set[int] = set()
    for i in range(40):
        off = struct.unpack(">h", rom[0x27B4 + i * 2 : 0x27B4 + i * 2 + 2])[0]
        h = 0x27B4 + off
        if in_code_regions(h):
            state_handlers.add(h)
    core_extra |= state_handlers

    # Funções REA conhecidas
    rea_funcs: set[int] = set()
    if REA_INDEX_PATH.is_file():
        idx = json.loads(REA_INDEX_PATH.read_text())
        for f in idx.get("functions", []):
            a = int(f["address"], 16)
            if in_code_regions(a):
                rea_funcs.add(a)

    # Tabela mestra de objetos $06A522 (233 IDs) + descritores diretos
    obj_tasks: set[int] = set()
    base = 0x06A522
    n_objs = struct.unpack(">H", rom[base : base + 2])[0] // 2
    for i in range(n_objs):
        p = base + struct.unpack(">H", rom[base + i * 2 : base + i * 2 + 2])[0]
        for o in (0x1A, 0x1E):
            v = struct.unpack(">I", rom[p + o : p + o + 4])[0]
            if is_valid_code_start(rom, md, v):
                obj_tasks.add(v)
    for p in (0x06C608, 0x06C62A, 0x06C64C):
        for o in (0x1A, 0x1E):
            v = struct.unpack(">I", rom[p + o : p + o + 4])[0]
            if is_valid_code_start(rom, md, v):
                obj_tasks.add(v)

    # Descritores de IA de lutadores ($00C14C, $01B1C2, $0030D6 + 4 formas chefe)
    ai_funcs: set[int] = set()
    ai_descs = {0x0428E6, 0x042F42, 0x043556, 0x043866}
    for tbl, cnt in ((0x00C14C, 10), (0x01B1C2, 10), (0x0030D6, 9)):
        for i in range(cnt):
            d = struct.unpack(">I", rom[tbl + i * 4 : tbl + i * 4 + 4])[0]
            if 0x034B0A <= d < 0x044000 and d % 2 == 0:
                ai_descs.add(d)
    for d in sorted(ai_descs):
        ptrs = struct.unpack(">6I", rom[d : d + 24])
        for t in ptrs[:3]:
            if in_code_regions(t):
                n = struct.unpack(">H", rom[t : t + 2])[0]
                if 0 <= n <= 64:
                    for k in range(n + 1):
                        off_fn = t + 2 + k * 8 + 4
                        if off_fn + 4 <= len(rom):
                            fn = struct.unpack(">I", rom[off_fn : off_fn + 4])[0]
                            if is_valid_code_start(rom, md, fn):
                                ai_funcs.add(fn)
        for fn in ptrs[3:]:
            if is_valid_code_start(rom, md, fn):
                ai_funcs.add(fn)

    # Tabelas absolutas e branch tables de despacho
    jt_funcs: set[int] = set()
    # Slots `bra` de tabelas declaradas à mão são instruções comprovadas: não
    # passam pela heurística de texto ASCII de is_valid_code_start (ex.: $E8AE
    # `60 5E 60 5E 36 2D 00 42` e $C5D0 `60 2A 60 46 ...` eram descartados).
    jt_slots: set[int] = set()
    for lo, hi, stride, _fmt, _desc in ABS_JUMP_TABLES:
        for o in range(lo, hi, stride):
            v = struct.unpack(">I", rom[o : o + 4])[0]
            if is_valid_code_start(rom, md, v):
                jt_funcs.add(v)
    for lo, hi, stride, fmt, _desc in BRA_JUMP_TABLES:
        for o in range(lo, hi, stride):
            if fmt == "bra_s" and rom[o] == 0x60 and rom[o + 1] != 0:
                disp = struct.unpack(">b", rom[o + 1 : o + 2])[0]
                tgt = o + 2 + disp
                jt_funcs.add(o)
                jt_slots.add(o)
                if is_valid_code_start(rom, md, tgt):
                    jt_funcs.add(tgt)
            elif fmt == "bra_w" and rom[o : o + 2] == b"\x60\x00":
                disp = struct.unpack(">h", rom[o + 2 : o + 4])[0]
                tgt = o + 2 + disp
                jt_funcs.add(o)
                jt_slots.add(o)
                if is_valid_code_start(rom, md, tgt):
                    jt_funcs.add(tgt)
    for lo, hi, stride, _fmt, _desc in PCREL16_JUMP_TABLES:
        for o in range(lo, hi, stride):
            disp = struct.unpack(">h", rom[o : o + 2])[0]
            tgt = lo + disp
            if is_valid_code_start(rom, md, tgt):
                jt_funcs.add(tgt)

    # Callbacks de menu validados ($02D5DC..$02EF00)
    menu_cbs: set[int] = set()
    for off in range(0x02D5DC, 0x02EF00, 2):
        v = struct.unpack(">I", rom[off : off + 4])[0]
        if 0x02D300 <= v < 0x032194 and v % 2 == 0 and is_valid_code_start(rom, md, v):
            v0 = struct.unpack(">I", rom[v : v + 4])[0]
            if not (0x02D000 <= v0 < 0x032194):
                ins4 = list(md.disasm(rom[v : v + 24], v, count=4))
                if len(ins4) >= 2 and all(
                    i.mnemonic.split(".")[0] not in ("dc.w", "illegal", "chk", "trapv", "ori")
                    for i in ins4[:2]
                ):
                    menu_cbs.add(v)
                    # Lista de callbacks do descritor de menu: um `bra.w` por item
                    # (ex.: $2DA50 One-Player Menu: Fight/Battle Room/Training/Return).
                    # Só a base era semente; $2DA54/$2DA58/$2D918/$2DBE8 viravam miss.
                    q = v
                    while rom[q : q + 2] == b"\x60\x00":
                        t = q + 2 + struct.unpack(">h", rom[q + 2 : q + 4])[0]
                        if not is_valid_code_start(rom, md, t):
                            break
                        menu_cbs.add(q)
                        jt_slots.add(q)
                        menu_cbs.add(t)
                        q += 4

    # Labels de código conhecidos em disasm/symbols/labels.toml
    label_code: set[int] = set()
    if LABELS_PATH.is_file():
        ldata = tomllib.loads(LABELS_PATH.read_text(encoding="utf-8"))
        for e in ldata.get("label", []):
            if e.get("type") == "code":
                a = int(e["address"])
                if is_valid_code_start(rom, md, a):
                    label_code.add(a)

    direct_extras = {
        0x010BE2, 0x010BFE,
        0x0182A0, 0x02FB0E, 0x048312, 0x05873A, 0x05E2FE,
        0x2FCD1A, 0x2FCD56, 0x2FDCEE,
    }
    seeds = core_extra | rea_funcs | obj_tasks | ai_funcs | jt_funcs | menu_cbs | label_code | direct_extras

    visited: set[int] = set()
    insns: dict[int, object] = {}
    inline_conts: set[int] = set()
    yield_conts: set[int] = set()
    dynamic_tasks: set[int] = set()
    work = [s for s in sorted(seeds) if s in jt_slots or is_valid_code_start(rom, md, s)]

    def add_work(a: int) -> None:
        if a not in visited and in_code_regions(a):
            work.append(a)

    while work:
        pc = work.pop()
        last_a0: int | None = None
        last_d0: int | None = None
        while in_code_regions(pc) and pc not in visited:
            visited.add(pc)
            res = list(md.disasm(rom[pc : min(pc + 16, len(rom))], pc, count=1))
            if not res:
                break
            ins = res[0]
            if (
                ins.mnemonic.endswith(".b")
                and ins.mnemonic.split(".")[0] in ("ori", "andi", "subi", "addi", "eori", "cmpi")
                and len(ins.bytes) >= 4
                and ins.bytes[2] != 0
            ):
                visited.remove(pc)
                break
            insns[pc] = ins
            mn = ins.mnemonic.lower()
            base_mn = mn.split(".")[0]
            op = ins.op_str

            m_a0 = re.match(r"#?\$([0-9a-fA-F]{4,6})(?:\.l)?,\s*a0$", op)
            if base_mn in ("movea", "lea") and m_a0:
                v = int(m_a0.group(1), 16)
                last_a0 = v if in_code_regions(v) else None
            elif op.endswith(", a0") or op.endswith(",a0"):
                last_a0 = None

            m_d0 = re.match(r"#\$([0-9a-fA-F]{4,6}),\s*d0$", op)
            if base_mn == "move" and m_d0:
                v = int(m_d0.group(1), 16)
                last_d0 = v if in_code_regions(v) else None
            elif op.endswith(", d0") or op.endswith(",d0"):
                last_d0 = None

            m_slot = re.match(r"#\$([0-9a-fA-F]{4,6}),\s*\$(2|8|c|36|76|108|130)\((a[045])\)$", op)
            if base_mn == "move" and m_slot:
                v = int(m_slot.group(1), 16)
                if is_valid_code_start(rom, md, v):
                    dynamic_tasks.add(v)
                    add_work(v)

            m_ramcb = re.match(r"#\$([0-9a-fA-F]{4,6}),\s*\$([0-9a-fA-F]{4})\.w$", op)
            if base_mn == "move" and m_ramcb and int(m_ramcb.group(2), 16) in RAM_CALLBACK_VARS:
                v = int(m_ramcb.group(1), 16)
                if is_valid_code_start(rom, md, v):
                    dynamic_tasks.add(v)
                    add_work(v)

            if base_mn == "rts" and last_d0 and is_valid_code_start(rom, md, last_d0):
                dynamic_tasks.add(last_d0)
                add_work(last_d0)

            next_pc = pc + ins.size
            if ins.bytes[:2] == b"\x4e\xb9":
                tgt = struct.unpack(">I", ins.bytes[2:6])[0]
                if in_code_regions(tgt):
                    add_work(tgt)
                if tgt in TASK_A0_APIS and last_a0 and is_valid_code_start(rom, md, last_a0, allow_text=True):
                    dynamic_tasks.add(last_a0)
                    add_work(last_a0)
                if tgt in TASK_D0_APIS and last_d0 and is_valid_code_start(rom, md, last_d0, allow_text=True):
                    dynamic_tasks.add(last_d0)
                    add_work(last_d0)
                if tgt in INLINE_WORDS:
                    next_pc += INLINE_WORDS[tgt] * 2
                    inline_conts.add(next_pc)
                elif tgt in INLINE_TERM:
                    while next_pc + 2 <= len(rom):
                        w = struct.unpack(">H", rom[next_pc : next_pc + 2])[0]
                        next_pc += 2
                        if w == INLINE_TERM[tgt]:
                            break
                    inline_conts.add(next_pc)
                elif tgt in YIELD_APIS:
                    yield_conts.add(next_pc)
            elif ins.bytes[:2] == b"\x4e\xf9":
                tgt = struct.unpack(">I", ins.bytes[2:6])[0]
                if in_code_regions(tgt):
                    add_work(tgt)
                if tgt in TASK_A0_APIS and last_a0 and is_valid_code_start(rom, md, last_a0):
                    dynamic_tasks.add(last_a0)
                    add_work(last_a0)
                if tgt in TASK_D0_APIS and last_d0 and is_valid_code_start(rom, md, last_d0):
                    dynamic_tasks.add(last_d0)
                    add_work(last_d0)
                break
            elif base_mn == "jmp":
                m = re.match(r"\$([0-9a-fA-F]+)\(pc,\s*d[0-7]\.[wl]\)", op)
                if m:
                    tbl = int(m.group(1), 16)
                    q = tbl
                    while q + 4 <= len(rom) and struct.unpack(">H", rom[q : q + 2])[0] == 0x6000:
                        add_work(q)
                        q += 4
                    # Duff's device: a base é a 1ª de uma sequência de instruções de
                    # 2 bytes com o mesmo mnemônico (ex.: $05E246 `exg d1,d6; exg d2,d4; ...`,
                    # embaralhamento de paleta em $5E20E). O walk linear a partir dela
                    # cobre a cauda inteira; o code_generator a converte em switch interno.
                    if q == tbl:
                        duff = list(md.disasm(rom[tbl : tbl + 4], tbl, count=2))
                        if (
                            len(duff) == 2
                            and duff[0].size == 2
                            and duff[1].size == 2
                            and duff[0].mnemonic == duff[1].mnemonic
                        ):
                            add_work(tbl)
                break
            elif base_mn in BRANCH_MNEMS or base_mn.startswith("db"):
                m = re.search(r"\$([0-9a-fA-F]+)$", op)
                if m:
                    tgt = int(m.group(1), 16)
                    if in_code_regions(tgt):
                        add_work(tgt)
                if base_mn == "bra":
                    break
            elif base_mn in TERM_MNEMS:
                break
            pc = next_pc

    code_addrs = sorted(insns.keys())
    code_set = set(code_addrs)

    extra_list = sorted(a for a in (core_extra | rea_funcs) if a in code_set)
    late_raw = (obj_tasks | ai_funcs | jt_funcs | menu_cbs | label_code | dynamic_tasks | inline_conts | yield_conts | direct_extras)
    late_list = sorted(a for a in late_raw if a in code_set and a not in set(extra_list))

    return {
        "code_addrs": code_addrs,
        "extra": extra_list,
        "late_extra": late_list,
        "stats": {
            "total_insns": len(code_addrs),
            "extra_count": len(extra_list),
            "late_extra_count": len(late_list),
            "inline_conts": len(inline_conts & code_set),
            "yield_conts": len(yield_conts & code_set),
        },
    }


def format_hex_array(addrs: list[int], per_line: int = 8) -> str:
    if not addrs:
        return "[]"
    lines = ["["]
    for i in range(0, len(addrs), per_line):
        chunk = ", ".join(f"0x{a:06X}" for a in addrs[i : i + per_line])
        lines.append(f"  {chunk},")
    lines.append("]")
    return "\n".join(lines)


def main() -> int:
    rom = ROM_PATH.read_bytes()
    res = build_cfg_and_leads(rom)

    OUT_CODE_ADDRS.parent.mkdir(parents=True, exist_ok=True)
    OUT_CODE_ADDRS.write_text(
        "# Endereços de início de instrução 68000 validados na Fase 1 (sem inline args/dados)\n"
        + "\n".join(f"{a:06X}" for a in res["code_addrs"])
        + "\n"
    )

    lines = [
        "# game/ec_discovery.toml — gerado por tools/recomp/gen_recomp_config.py",
        "# Metadados apenas (endereços hex de código e tabelas; zero dados da ROM).",
        "",
        "[functions]",
        "extra = " + format_hex_array(res["extra"]),
        "",
        "late_extra = " + format_hex_array(res["late_extra"]),
        "",
        "# Estado dispatcher ($27B4: 40 offsets word pcrel16 relativos a $27B4)",
        "[[jump_table]]",
        "start  = 0x0027B4",
        "end    = 0x002804",
        "stride = 2",
        'format = "pcrel16"',
        "",
    ]

    for lo, hi, stride, fmt, desc in ABS_JUMP_TABLES + BRA_JUMP_TABLES + PCREL16_JUMP_TABLES:
        lines += [
            f"# {desc}",
            "[[jump_table]]",
            f"start  = 0x{lo:06X}",
            f"end    = 0x{hi:06X}",
            f"stride = {stride}",
            f'format = "{fmt}"',
            "",
        ]

    for lo, hi, desc in PROTECTED_RANGES:
        lines += [
            f"# {desc}",
            "[[protected_range]]",
            f"lo = 0x{lo:06X}",
            f"hi = 0x{hi:06X}",
            "",
        ]

    toml_txt = "\n".join(lines)
    # Autovalidação com tomllib antes de gravar
    tomllib.loads(toml_txt)
    OUT_DISCOVERY.write_text(toml_txt)

    st = res["stats"]
    print(
        f"OK: {OUT_CODE_ADDRS.relative_to(ROOT)} ({st['total_insns']} insns) | "
        f"{OUT_DISCOVERY.relative_to(ROOT)} (extra={st['extra_count']}, "
        f"late_extra={st['late_extra_count']}, inline_conts={st['inline_conts']}, "
        f"yield_conts={st['yield_conts']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
