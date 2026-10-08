#!/usr/bin/env python3
"""
Generate the N64Recomp inputs for Sands of Time from the ROM itself.

Sands of Time is an Ocarina of Time (Master Quest Debug) hack built with z64rom, so there is no ELF
to recompile from. Instead, this script produces a symbol file for N64Recomp's symbol-file input mode
(the mode used for Majora's Mask with Zelda64RecompSyms):

- the ROM is decompressed (every DMA file is written at its VROM address), N64Recomp reads code from it
- code sections:
    makerom entry, boot and code: vanilla MQ Debug code, as patched by z64rom
    uLib: z64rom's user library, loaded at a fixed address (0x80700000)
    overlays: actors and effects (from the extended tables that z64rom stores in the dmadata file),
              game states and kaleido overlays (from the vanilla tables in code)
- functions are found with spimdisasm. Vanilla code is named from z64hdr's MQ Debug symbols.
- overlay relocations (HI16/LO16) come from each overlay's relocation table

Usage: gen_sot_syms.py <rom.z64> <z64hdr dir> <output dir>
"""

import argparse
import concurrent.futures
import dataclasses
import hashlib
import os
import re
import struct
import sys
from pathlib import Path

import crunch64
import rabbitizer

# Known Sands of Time 1.22 ROM
SOT_ROM_MD5 = "33273fb052fa8ea94410352a6ce52f8f"

# MQ Debug layout (unchanged by z64rom)
ENTRY = dict(rom=0x1000, vram=0x80000400, size=0x60)
BOOT_VRAM = 0x80000460
BOOT_ROM = 0x1060
BOOT_TEXT_END = 0x80009320  # rspboot follows
CODE_VRAM = 0x8001CE60
CODE_TEXT_END = 0x801120C0  # RSP ucodes follow
GAMESTATE_TABLE = 0x8011F830
GAMESTATE_COUNT = 6
KALEIDO_TABLE = 0x8012D1A0
KALEIDO_COUNT = 2

# z64rom
ULIB_VRAM = 0x80700000
ULIB_DMA_INDEX = 3
CODE_DMA_INDEX = 28
# Extended tables stored in the dmadata file (see uLib_tables.c / uLib.h)
EXT_DMA_MAX = 3800
EXT_ACTOR_MAX = 1000
EXT_OBJECT_MAX = 1000
EXT_SCENE_MAX = 256
EXT_EFFECT_MAX = 64
EXT_ACTOR_TABLE = EXT_DMA_MAX * 0x10
EXT_OBJECT_TABLE = EXT_ACTOR_TABLE + EXT_ACTOR_MAX * 0x20
EXT_SCENE_TABLE = EXT_OBJECT_TABLE + EXT_OBJECT_MAX * 0x8
EXT_EFFECT_TABLE = EXT_SCENE_TABLE + EXT_SCENE_MAX * 0x14

OVL_VRAM_MIN = 0x80800000
OVL_VRAM_MAX = 0x81000000

R_MIPS_32 = 2
R_MIPS_26 = 4
R_MIPS_HI16 = 5
R_MIPS_LO16 = 6


def u32(data, off):
    return struct.unpack_from(">I", data, off)[0]


@dataclasses.dataclass
class Section:
    name: str
    rom: int
    vram: int
    size: int
    text_start: int  # vram
    text_end: int  # vram
    relocatable: bool
    functions: list = dataclasses.field(default_factory=list)  # (name, vram, size)
    relocs: list = dataclasses.field(default_factory=list)  # (type name, vram, target vram)
    # internal: offsets (from vram) of text words relocated as R_MIPS_26
    reloc26: set = dataclasses.field(default_factory=set)
    # internal: code addresses stored in the overlay's data (R_MIPS_32 in .data/.rodata), e.g. function tables
    data_code_ptrs: set = dataclasses.field(default_factory=set)


# ROM


def read_dmadata(rom: bytes):
    dma_off = rom.find(struct.pack(">IIII", 0, 0x1060, 0, 0))
    assert dma_off > 0, "dmadata not found"
    entries = []
    p = dma_off
    while True:
        e = struct.unpack_from(">IIII", rom, p)
        if e[1] == 0:
            break
        entries.append(e)
        p += 16
    return dma_off, entries


def decompress_rom(rom: bytes):
    """Returns (decompressed rom with every file at its vrom, {dma index: file bytes})"""
    _, entries = read_dmadata(rom)
    size = max(e[1] for e in entries if e[0] != 0xFFFFFFFF)
    out = bytearray(size)
    files = {}
    for i, (vs, ve, ps, pe) in enumerate(entries):
        if vs == 0xFFFFFFFF or ps == 0xFFFFFFFF or ve <= vs:
            continue
        if pe:
            raw = rom[ps:pe]
            if raw[:4] != b"Yaz0":
                raise Exception(f"dma entry {i}: compressed file without Yaz0 header")
            data = crunch64.yaz0.decompress(raw)[: ve - vs]
        else:
            data = rom[ps : ps + (ve - vs)]
        data = bytes(data).ljust(ve - vs, b"\0")
        out[vs:ve] = data
        files[i] = (vs, data)
    return bytes(out), files, entries


# Overlays


def parse_overlay_relocs(data: bytes, vram: int):
    """Parse the relocation table at the end of an overlay file.

    Returns (text size, data size, rodata size, bss size, list of (section, type, offset))
    """
    off = u32(data, len(data) - 4)
    hdr = len(data) - off
    text_size, data_size, rodata_size, bss_size, n = struct.unpack_from(">IIIII", data, hdr)
    relocs = []
    for i in range(n):
        w = u32(data, hdr + 20 + i * 4)
        relocs.append((w >> 30, (w >> 24) & 0x3F, w & 0xFFFFFF))
    return text_size, data_size, rodata_size, bss_size, hdr, relocs


def overlay_section(name, vrom, data, vram):
    text_size, data_size, rodata_size, bss_size, hdr, relocs = parse_overlay_relocs(data, vram)
    if text_size == 0:
        return None
    sec = Section(
        name=name,
        rom=vrom,
        vram=vram,
        size=text_size + data_size + rodata_size,
        text_start=vram,
        text_end=vram + text_size,
        relocatable=True,
    )
    # Pair HI16/LO16 like Overlay_Relocate does: per register
    hi_by_reg = {}
    hi_targets = {}  # offset of HI16 -> target vram
    # Reloc offsets are relative to the start of their section
    section_base = {1: 0, 2: text_size, 3: text_size + data_size}
    for section_id, rtype, offset in relocs:
        if section_id in (2, 3) and rtype == R_MIPS_32:
            target = u32(data, section_base[section_id] + offset)
            if vram <= target < vram + text_size:
                insn = u32(data, target - vram)
                # .rodata also has jump tables (addresses inside functions): only keep function starts there
                if section_id == 2 or (insn >> 16) == 0x27BD:
                    sec.data_code_ptrs.add(target)
            continue
        if section_id != 1:  # text
            continue
        insn = u32(data, offset)
        if rtype == R_MIPS_HI16:
            hi_by_reg[(insn >> 16) & 0x1F] = offset
        elif rtype == R_MIPS_LO16:
            rs = (insn >> 21) & 0x1F
            hi_off = hi_by_reg.get(rs)
            if hi_off is None:
                print(f"warning: {name}: LO16 at 0x{offset:X} without HI16", file=sys.stderr)
                continue
            hi_insn = u32(data, hi_off)
            lo = insn & 0xFFFF
            target = ((hi_insn & 0xFFFF) << 16) + (lo - 0x10000 if lo & 0x8000 else lo)
            target &= 0xFFFFFFFF
            if hi_off not in hi_targets:
                hi_targets[hi_off] = target
                sec.relocs.append(("R_MIPS_HI16", vram + hi_off, target))
            sec.relocs.append(("R_MIPS_LO16", vram + offset, target))
        elif rtype == R_MIPS_26:
            sec.reloc26.add(offset)
    sec.relocs.sort(key=lambda r: r[1])
    return sec


def collect_overlays(files, entries, code_data):
    dmadata = files[2][1]
    overlays = []
    seen = set()

    def add(kind, index, vrom_start, vrom_end, vram_start, vram_end):
        if not (OVL_VRAM_MIN <= vram_start < vram_end <= OVL_VRAM_MAX):
            return
        # The VRAM range can include .bss (game states, pause menu)
        if not (0 < vrom_start < vrom_end) or not (0 <= (vram_end - vram_start) - (vrom_end - vrom_start) < 0x100000):
            return
        if vrom_start in seen:
            return
        overlays.append((f"ovl_{kind}_{index:04X}", vrom_start, vrom_end, vram_start))
        seen.add(vrom_start)

    for i in range(EXT_ACTOR_MAX):
        vs, ve, ras, rae = struct.unpack_from(">IIII", dmadata, EXT_ACTOR_TABLE + i * 0x20)
        add("actor", i, vs, ve, ras, rae)
    for i in range(EXT_EFFECT_MAX):
        vs, ve, ras, rae = struct.unpack_from(">IIII", dmadata, EXT_EFFECT_TABLE + i * 0x1C)
        add("effect", i, vs, ve, ras, rae)
    for i in range(GAMESTATE_COUNT):
        _, vs, ve, ras, rae = struct.unpack_from(">IIIII", code_data, GAMESTATE_TABLE - CODE_VRAM + i * 0x30)
        add("gamestate", i, vs, ve, ras, rae)
    for i in range(KALEIDO_COUNT):
        _, vs, ve, ras, rae = struct.unpack_from(">IIIII", code_data, KALEIDO_TABLE - CODE_VRAM + i * 0x1C)
        add("kaleido", i, vs, ve, ras, rae)
    return overlays


# Function discovery


# Decompressed ROM, shared with the worker processes
DEC_ROM = b""


def init_worker(rom: bytes):
    global DEC_ROM
    DEC_ROM = rom


def find_functions(section: Section, data: bytes, hints: dict[int, str]):
    """Find the functions of a section's text with spimdisasm. data: the section's bytes from its vram."""
    from spimdisasm import common, mips

    common.GlobalConfig.QUIET = True
    ctx = common.Context()
    text_size = section.text_end - section.text_start
    vrom = section.rom + (section.text_start - section.vram)
    assert DEC_ROM[vrom : vrom + text_size] == data[section.text_start - section.vram : section.text_end - section.vram]
    # Every section gets its own segment (spimdisasm only looks up the section's segment for hints)
    ctx.globalSegment.changeRanges(0, 0x10000000, 0x80000000, 0x80000400)
    segment = ctx.addOverlaySegment("sec", vrom, vrom + len(data), section.vram, section.vram + len(data) + 0x100000)
    for addr in sorted(hints):
        if section.text_start <= addr < section.text_end:
            segment.addFunction(addr, vromAddress=vrom + addr - section.text_start)
    text_section = mips.sections.SectionText(
        ctx, vrom, vrom + text_size, section.text_start, section.name, DEC_ROM, 0, "sec"
    )
    text_section.analyze()
    starts = {sym.vram for sym in text_section.symbolList}
    return split_at(section, starts | set(hints))


def split_at(section: Section, starts, data: bytes | None = None, labels=frozenset()):
    """Functions of a section from a set of start addresses: each one ends at the next start.

    With data, each function is trimmed to the code reachable from its start (z64rom replaced some
    vanilla functions with shorter ones, followed by their data). labels: addresses of jump table
    targets, which are reachable.
    """
    starts = sorted({a for a in starts if section.text_start <= a < section.text_end} | {section.text_start})
    ends = starts[1:] + [section.text_end]
    funcs = []
    for a, b in zip(starts, ends):
        if data is not None:
            b = trim_function(section, data, a, b, labels)
        if b > a:
            funcs.append((a, b - a))
    return funcs


def trim_function(section: Section, data: bytes, start: int, limit: int, labels) -> int:
    """End of the code reachable from start, before limit"""
    func_labels = sorted(l for l in labels if start < l < limit)
    reach = start  # furthest known reachable address
    pc = start
    end = start
    while pc < limit:
        word = u32(data, pc - section.vram)
        insn = rabbitizer.Instruction(word, pc)
        if not insn.isValid():
            break
        if insn.isBranch():
            target = insn.getBranchVramGeneric()
            if start <= target < limit:
                reach = max(reach, target)
        end = pc + 4
        terminator = False
        if insn.isReturn():
            terminator = True
        elif insn.isJump() and insn.isJumpWithAddress() and not insn.doesLink():
            terminator = True  # j: tail call or loop
        elif insn.isUnconditionalBranch():
            terminator = True
        elif insn.isJumptableJump():
            terminator = True
        if terminator:
            # include the delay slot
            end = pc + 8
            later_labels = [l for l in func_labels if l >= end]
            if reach < end and not later_labels:
                break
            if reach < end:
                reach = max(reach, later_labels[0])
            pc = end
            continue
        pc += 4
    return min(end, limit)


def text_pointers(section: Section, data: bytes):
    """Words of a section's data that point to its text (jump table entries, function pointers)"""
    pointers = set()
    data_start = section.text_end - section.vram
    for off in range(data_start, len(data) - 3, 4):
        w = u32(data, off)
        if section.text_start <= w < section.text_end and w % 4 == 0:
            pointers.add(w)
    return pointers


def jal_targets(data: bytes, text_start: int, text_end: int, base_vram: int, skip_offsets=frozenset(), with_jumps=True):
    targets = set()
    for off in range(text_start - base_vram, text_end - base_vram, 4):
        w = u32(data, off)
        if (w >> 26) == 3 and off not in skip_offsets:
            targets.add(0x80000000 | ((w & 0x3FFFFFF) << 2))
        elif (w >> 26) == 2 and off not in skip_offsets and with_jumps:
            # j to another section: a hook (tail call)
            target = 0x80000000 | ((w & 0x3FFFFFF) << 2)
            if not (text_start <= target < text_end):
                targets.add(target)
    return targets


# z64hdr names


def load_z64hdr_names(z64hdr: Path) -> dict[int, str]:
    names = {}
    sym_re = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*0x([0-9A-Fa-f]+);")
    for f in ("sym_src.ld",):
        for line in (z64hdr / "oot_mq_debug" / f).read_text().splitlines():
            m = sym_re.match(line)
            if m and not re.match(r"^_.*Segment", m.group(1)):
                addr = int(m.group(2), 16)
                names.setdefault(addr, m.group(1))
    return names


def calls_in_functions(sec: Section, data: bytes, skip_offsets=frozenset(), with_pointers=False):
    """Targets of jal (and of j to other sections) in a section's functions.

    with_pointers: also addresses built with lui + addiu/ori (function pointers)
    """
    targets = set()
    for _, vram, size in sec.functions:
        lui = {}
        for off in range(vram - sec.vram, vram + size - sec.vram, 4):
            if off in skip_offsets:
                continue
            w = u32(data, off)
            op = w >> 26
            target = 0x80000000 | ((w & 0x3FFFFFF) << 2)
            if op == 3 or (op == 2 and not (sec.text_start <= target < sec.text_end)):
                targets.add(target)
            elif with_pointers and op == 0x0F:  # lui
                lui[(w >> 16) & 0x1F] = (w & 0xFFFF) << 16
            elif with_pointers and op in (0x09, 0x0D):  # addiu, ori
                rs, rt, imm = (w >> 21) & 0x1F, (w >> 16) & 0x1F, w & 0xFFFF
                if rs in lui:
                    if op == 0x09:
                        imm = imm - 0x10000 if imm & 0x8000 else imm
                        addr = (lui[rs] + imm) & 0xFFFFFFFF
                    else:
                        addr = lui[rs] | imm
                    if addr >= 0x80000000 and addr % 4 == 0:
                        targets.add(addr | 0x100000000)  # marked as pointer
                lui.pop(rt, None)
    return targets


def static_functions(sec: Section, data: bytes, hints):
    if sec.name == "ulib":
        # GCC-built z64rom library without symbols. Debug info of libgcc objects is mixed with
        # their code: only keep code that is called, or directly follows other code.
        found = find_functions(sec, data, hints)
        trimmed = split_at(sec, {a for a, _ in found}, data, text_pointers(sec, data))
        funcs = []
        prev_end = sec.text_start
        for a, size in trimmed:
            pad = prev_end
            while pad < a and u32(data, pad - sec.vram) == 0:
                pad += 4
            if pad == a or a in hints:
                funcs.append((a, size))
                prev_end = a + size
        return funcs
    # Vanilla code: z64hdr's names (from the decomp) are the function starts
    return split_at(sec, hints, data, text_pointers(sec, data))


def do_overlay(item):
    sec, data, extra_hints = item
    hints = {t: None for t in jal_targets(data, sec.text_start, sec.text_end, sec.vram)
             if sec.text_start <= t < sec.text_end}
    for t in extra_hints:
        # Only if it looks like a function start (stack frame setup)
        if sec.text_start <= t < sec.text_end and (u32(data, t - sec.vram) >> 16) == 0x27BD:
            hints[t] = None
    # Addresses of code loaded with HI16/LO16 pairs are function pointers (e.g. player action functions)
    for _, _, target in sec.relocs:
        if sec.text_start <= target < sec.text_end:
            hints[target] = None
    # Function pointers in the overlay's data (e.g. a game state's update function table)
    for target in sec.data_code_ptrs:
        hints[target] = None
    funcs = find_functions(sec, data, hints)
    return [(f"{sec.name}_{v:08X}", v, s) for v, s in funcs]


# Output


def write_syms(sections, path: Path):
    lines = ["# Generated by tools/gen_sot_syms.py from the Sands of Time ROM", ""]
    for sec in sections:
        lines.append("[[section]]")
        lines.append(f'name = "{sec.name}"')
        lines.append(f"rom = 0x{sec.rom:08X}")
        lines.append(f"vram = 0x{sec.vram:08X}")
        lines.append(f"size = 0x{sec.size:X}")
        lines.append("")
        lines.append("functions = [")
        for name, vram, size in sec.functions:
            lines.append(f'    {{ name = "{name}", vram = 0x{vram:08X}, size = 0x{size:X} }},')
        lines.append("]")
        lines.append("")
        if sec.relocatable:
            lines.append("relocs = [")
            for rtype, vram, target in sec.relocs:
                lines.append(f'    {{ type = "{rtype}", vram = 0x{vram:08X}, target_vram = 0x{target:08X} }},')
            lines.append("]")
            lines.append("")
    path.write_text("\n".join(lines))


def write_data_syms(static_sections, z64hdr: Path, path: Path):
    """Data symbols of the game (from z64hdr), for the patches"""
    func_names = {name for sec in static_sections for name, _, _ in sec.functions}
    func_addrs = {vram for sec in static_sections for _, vram, _ in sec.functions}
    # RAM buffers after code's bss (gGfxPools, ...), up to uLib
    buffers = Section("buffers", 0, 0x801759C0, ULIB_VRAM - 0x801759C0, 0x801759C0, 0x801759C0, False)
    static_sections = list(static_sections) + [buffers]
    symbols = {sec.name: {} for sec in static_sections}
    sym_re = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*0x([0-9A-Fa-f]+);")
    for f in ("sym_src.ld", "sym_other.ld"):
        for line in (z64hdr / "oot_mq_debug" / f).read_text().splitlines():
            m = sym_re.match(line)
            if m is None or m.group(1) in func_names or re.match(r"^_.*Segment", m.group(1)):
                continue
            addr = int(m.group(2), 16)
            if addr in func_addrs:
                continue
            for sec in static_sections:
                # Data and bss of the section (bss follows the section's ROM contents)
                end = {"boot": 0x80016DA0, "code": 0x801759C0}.get(sec.name, sec.vram + sec.size)
                if sec.vram <= addr < end:
                    symbols[sec.name].setdefault(m.group(1), addr)
                    break
    lines = ["# Generated by tools/gen_sot_syms.py: data symbols of the recompiled game (from z64hdr)", ""]
    for sec in static_sections:
        lines.append("[[section]]")
        lines.append(f'name = "{sec.name}"')
        lines.append(f"rom = 0x{sec.rom:08X}")
        lines.append(f"vram = 0x{sec.vram:08X}")
        lines.append(f"size = 0x{sec.size:X}")
        lines.append("")
        lines.append("symbols = [")
        for name, addr in sorted(symbols[sec.name].items(), key=lambda kv: (kv[1], kv[0])):
            lines.append(f'    {{ name = "{name}", vram = 0x{addr:08X} }},')
        lines.append("]")
        lines.append("")
    path.write_text("\n".join(lines))


# RSP microcodes in code: (name, text start, text end, data start, data end, text md5, data md5)
UCODES = [
    ("aspMain", 0x801120C0, 0x80113070, 0x80155C70, 0x80155F50,
     0x316046D1748C3487EF8792D42CC6B433, 0xAD2D1D7E8C2AFD1FB7E4267E57597EB7),
    ("njpgdspMain", 0x80114930, 0x80115420, 0x80157D30, 0x80157D90,
     0x1CAB4DC7403C218956ADC82DFFC624C0, 0xCF5303B2528507DAD6DA93DF2A52A01F),
]


def extract_ucodes(code_data: bytes, out: Path):
    """Write the RSP microcode texts for RSPRecomp (checked against the vanilla ones)"""
    (out / "rsp").mkdir(exist_ok=True)
    for name, ts, te, ds, de, text_md5, data_md5 in UCODES:
        text = code_data[ts - CODE_VRAM : te - CODE_VRAM]
        data = code_data[ds - CODE_VRAM : de - CODE_VRAM]
        if int(hashlib.md5(text).hexdigest(), 16) != text_md5 or int(hashlib.md5(data).hexdigest(), 16) != data_md5:
            raise SystemExit(f"RSP microcode {name} differs from vanilla")
        (out / "rsp" / f"{name}.text.bin").write_bytes(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("z64hdr", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count())
    args = ap.parse_args()

    rom = args.rom.read_bytes()
    md5 = hashlib.md5(rom).hexdigest()
    if md5 != SOT_ROM_MD5:
        print(f"warning: unknown ROM (md5 {md5}), expected Sands of Time 1.22 ({SOT_ROM_MD5})", file=sys.stderr)
    if rom[:4] != b"\x80\x37\x12\x40":
        raise SystemExit("The ROM must be in big-endian (.z64) format")

    args.out.mkdir(parents=True, exist_ok=True)
    print("Decompressing the ROM")
    global DEC_ROM
    dec, files, entries = decompress_rom(rom)
    DEC_ROM = dec
    (args.out / "sot_decompressed.z64").write_bytes(dec)

    names = load_z64hdr_names(args.z64hdr)

    code_vrom, code_data = files[CODE_DMA_INDEX]
    ulib_vrom, ulib_data = files[ULIB_DMA_INDEX]
    boot_data = dec[BOOT_ROM : 0x12F70]

    # uLib text ends after its last function (libgcc helpers), followed by debug info
    last_jr = max(off for off in range(0, len(ulib_data) - 4, 4) if u32(ulib_data, off) == 0x03E00008)
    ulib_text_end = last_jr + 8
    while ulib_text_end < len(ulib_data) and rabbitizer.Instruction(u32(ulib_data, ulib_text_end)).isValid():
        ulib_text_end += 4

    static_sections = [
        (Section("entry", ENTRY["rom"], ENTRY["vram"], ENTRY["size"], ENTRY["vram"], ENTRY["vram"] + ENTRY["size"], False),
         dec[ENTRY["rom"] : ENTRY["rom"] + ENTRY["size"]]),
        (Section("boot", BOOT_ROM, BOOT_VRAM, len(boot_data), BOOT_VRAM, BOOT_TEXT_END, False), boot_data),
        (Section("code", code_vrom, CODE_VRAM, len(code_data), CODE_VRAM, CODE_TEXT_END, False), code_data),
        (Section("ulib", ulib_vrom, ULIB_VRAM, len(ulib_data), ULIB_VRAM, ULIB_VRAM + ulib_text_end, False), ulib_data),
    ]

    overlay_list = collect_overlays(files, entries, code_data)
    print(f"{len(overlay_list)} overlays")
    overlay_sections = []
    for name, vs, ve, vram in overlay_list:
        sec = overlay_section(name, vs, dec[vs:ve], vram)
        if sec is not None:
            overlay_sections.append((sec, dec[vs:ve]))

    print("Finding functions")
    # Pause menu and player overlays: code refers to some of their functions by link address
    # (KaleidoManager_GetRamAddr, e.g. Player_Init in z_player_call.c)
    kaleido_pointers = set()
    for sec, data in static_sections:
        if sec.name in ("boot", "code"):
            sec.functions = [(None, v, s) for v, s in split_at(sec, names, data, text_pointers(sec, data))]
            kaleido_pointers |= {t & 0xFFFFFFFF for t in calls_in_functions(sec, data, with_pointers=True)
                                 if t & 0x100000000 and OVL_VRAM_MIN <= (t & 0xFFFFFFFF) < OVL_VRAM_MAX}

    # Overlays are analyzed on their own
    items = [(sec, data, kaleido_pointers if sec.name.startswith("ovl_kaleido") else ())
             for sec, data in overlay_sections]
    with concurrent.futures.ProcessPoolExecutor(max_workers=args.jobs, initializer=init_worker, initargs=(dec,)) as ex:
        for (sec, _), funcs in zip(overlay_sections, ex.map(do_overlay, items)):
            sec.functions = funcs

    # Static code: z64hdr's names, plus the targets of calls from code, until no new call target is found.
    # Only calls from inside functions count (sections can contain data, e.g. debug info in uLib).
    static_hints: dict[int, str] = {a: n for a, n in names.items()}
    for sec, data in overlay_sections:
        for t in calls_in_functions(sec, data, sec.reloc26):
            static_hints.setdefault(t, None)
    for iteration in range(10):
        for sec, data in static_sections:
            sec.functions = [(names.get(v) or f"func_{v:08X}", v, s) for v, s in static_functions(sec, data, static_hints)]
        new_targets = set()
        for sec, data in static_sections:
            for t in calls_in_functions(sec, data, with_pointers=sec.name == "ulib"):
                if t & 0x100000000:
                    # Pointers: only to code of uLib, starting with a stack frame setup or used as a callback
                    t &= 0xFFFFFFFF
                    if not (ULIB_VRAM <= t < static_sections[3][0].text_end):
                        continue
                    ulib_sec, ulib_data = static_sections[3]
                    if (u32(ulib_data, t - ULIB_VRAM) >> 16) != 0x27BD:  # addiu $sp, $sp, ...
                        continue
                if t not in static_hints:
                    new_targets.add(t)
        if not new_targets:
            break
        for t in new_targets:
            static_hints[t] = None

    # Unique names
    used = {}
    all_sections = [s for s, _ in static_sections] + [s for s, _ in overlay_sections]
    for sec in all_sections:
        renamed = []
        for name, vram, size in sec.functions:
            if name in used:
                name = f"{name}_{sec.name}"
            used[name] = vram
            renamed.append((name, vram, size))
        sec.functions = renamed

    # Static calls that didn't land on a function start
    starts = {v for s, _ in static_sections for _, v, _ in s.functions}
    missing = sorted(t for t in static_hints if static_hints[t] is None and t not in starts
                     and any(s.text_start <= t < s.text_end for s, _ in static_sections))
    if missing:
        print(f"warning: {len(missing)} call targets are not function starts: "
              + ", ".join(f"0x{t:08X}" for t in missing[:20]), file=sys.stderr)

    extract_ucodes(code_data, args.out)
    write_syms(all_sections, args.out / "sot.syms.toml")
    write_data_syms([s for s, _ in static_sections], args.z64hdr, args.out / "sot.datasyms.toml")
    n_funcs = sum(len(s.functions) for s in all_sections)
    print(f"Wrote {args.out / 'sot.syms.toml'}: {len(all_sections)} sections, {n_funcs} functions")


if __name__ == "__main__":
    main()
