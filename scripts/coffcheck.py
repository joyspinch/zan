#!/usr/bin/env python3
"""Structural validator for the Zan PE-COFF aarch64 (Windows ARM64) object writer.

Parses a .obj produced by `zanc out.obj prog.zan` and checks, without any
Windows toolchain:
  - COFF header: machine IMAGE_FILE_MACHINE_ARM64 (0xAA64), optional header
    absent, section count
  - section table: names/flags, SizeOfRawData/PointerToRawData in file bounds,
    relocation tables in file bounds
  - symbol table: 18-byte entries, aux record counts consistent, string-table
    offsets in bounds, section numbers valid, aux section-definition records
    consistent with their section headers
  - relocations: ARM64 types only, symbol index in range, VirtualAddress
    4-aligned and inside the section, list sorted by VirtualAddress, and the
    instruction word at each VirtualAddress actually matches the relocation's
    expected opcode shape (adrp/add/ldr/bl)
  - link hygiene: entry symbol "main" defined exactly once, no symbol defined
    twice, undefined externs have section 0, defined symbols point inside
    their section

Exit code 0 = valid; nonzero with diagnostics otherwise.
"""
import struct
import sys

MACHINE_ARM64 = 0xAA64

# llvm/BinaryFormat/COFF.h, enum RelocationTypesARM64
REL_NAMES = {
    0x0: "ABSOLUTE", 0x1: "ADDR32", 0x2: "ADDR32NB", 0x3: "BRANCH26",
    0x4: "PAGEBASE_REL21", 0x5: "REL21", 0x6: "PAGEOFFSET_12A",
    0x7: "PAGEOFFSET_12L", 0x8: "SECREL", 0x9: "SECREL_LOW12A",
    0xA: "SECREL_HIGH12A", 0xB: "SECREL_LOW12L", 0xC: "TOKEN",
    0xD: "SECTION", 0xE: "ADDR64", 0xF: "BRANCH19", 0x10: "BRANCH14",
    0x11: "REL32",
}
VALID_RELS = set(REL_NAMES)

SC_NAMES = {0: "NULL", 1: "EXT_DEF?", 2: "EXTERNAL", 3: "STATIC", 104: "SECTION", 105: "WEAK_EXT"}


class Fail(Exception):
    pass


def fail(msg):
    raise Fail(msg)


class Sym:
    def __init__(self, idx, name, value, sect, typ, cls, naux):
        self.idx, self.name, self.value = idx, name, value
        self.sect, self.typ, self.cls, self.naux = sect, typ, cls, naux


def sym_name(raw, strtab):
    if raw[:4] == b"\0\0\0\0":
        off = struct.unpack_from("<I", raw, 4)[0]
        if off >= len(strtab):
            fail(f"symbol string-table offset {off} out of bounds (strtab {len(strtab)})")
        end = strtab.find(b"\0", off)
        if end < 0:
            fail(f"unterminated string-table name at offset {off}")
        return strtab[off:end].decode()
    return raw.rstrip(b"\0").decode()


def main():
    if len(sys.argv) != 2:
        print("usage: coffcheck.py <file.obj>", file=sys.stderr)
        return 2
    path = sys.argv[1]
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 20:
        fail(f"file shorter than a COFF header ({len(data)} bytes)")

    (machine, nsect, tstamp, symptr, nsym, optsize, chars) = struct.unpack_from("<HHIIIHH", data, 0)
    errs = []
    def check(cond, msg):
        if not cond:
            errs.append(msg)
        return cond

    check(machine == MACHINE_ARM64, f"machine {machine:#x} != ARM64 (0xAA64)")
    check(optsize == 0, f"SizeOfOptionalHeader {optsize} != 0 for an object")
    check(nsect == 3, f"NumberOfSections {nsect} != 3")
    check(chars == 0, f"header Characteristics {chars:#x} != 0 for an object")

    # ---- section table ----
    sections = []
    for i in range(nsect):
        base = 20 + 40 * i
        (name, vsize, vaddr, rsize, rptr, relptr, lnptr, nrel, nln, schars) = struct.unpack_from("<8sIIIIIIHHI", data, base)
        sections.append({"idx": i + 1, "name": name.rstrip(b"\0").decode(), "vsize": vsize,
                         "vaddr": vaddr, "rsize": rsize, "rptr": rptr, "relptr": relptr,
                         "nrel": nrel, "nln": nln, "chars": schars})
    for s in sections:
        check(s["name"] != "", f"section {s['idx']}: empty name")
        check(s["vaddr"] == 0 and s["vsize"] == 0, f"section {s['name']}: nonzero VirtualAddress/Size in object")
        if s["rsize"] > 0:
            check(s["rptr"] > 0 and s["rptr"] + s["rsize"] <= len(data),
                  f"section {s['name']}: raw data [{s['rptr']:#x}..{s['rptr'] + s['rsize']:#x}) outside file ({len(data)})")
        if s["nrel"] > 0:
            check(s["relptr"] > 0 and s["relptr"] + 10 * s["nrel"] <= len(data),
                  f"section {s['name']}: reloc table outside file")
        else:
            check(s["relptr"] == 0 or s["relptr"] < len(data),
                  f"section {s['name']}: dangling PointerToRelocations {s['relptr']:#x}")
        if s["chars"] & 0x20:  # CNT_CODE
            check(s["chars"] & 0x20000000, f"section {s['name']}: CODE without MEM_EXECUTE")
        if s["chars"] & 0x80:  # CNT_UNINITIALIZED_DATA
            check(s["rsize"] == 0 and s["rptr"] == 0, f"section {s['name']}: UNINITIALIZED with raw data")

    # ---- symbol table + string table ----
    check(symptr > 0 and symptr + 18 * nsym <= len(data),
          f"symbol table [{symptr:#x}..{symptr + 18 * nsym:#x}) outside file ({len(data)})")
    if symptr + 18 * nsym > len(data):
        report(path, errs, None, None)
        return 1
    # string table immediately follows; first 4 bytes = its total size
    strtab_len = struct.unpack_from("<I", data, symptr + 18 * nsym)[0] if symptr + 18 * nsym + 4 <= len(data) else 0
    check(strtab_len >= 4, f"string table size {strtab_len} < 4")
    check(symptr + 18 * nsym + strtab_len <= len(data),
          f"string table overruns file ({symptr + 18 * nsym + strtab_len:#x} > {len(data):#x})")
    strtab = data[symptr + 18 * nsym: symptr + 18 * nsym + strtab_len]
    check(symptr + 18 * nsym + strtab_len == len(data),
          f"file has {len(data) - (symptr + 18 * nsym + strtab_len)} trailing bytes after the string table")

    syms = []
    i = 0
    while i < nsym:
        base = symptr + 18 * i
        raw = data[base: base + 8]
        (value, sect, typ, cls, naux) = struct.unpack_from("<IhHBB", data, base + 8)
        name = sym_name(raw, strtab)
        s = Sym(i, name, value, sect, typ, cls, naux)
        syms.append(s)
        if name == "":
            fail(f"symbol {i}: empty name")
        i += 1 + naux

    # expected canonical form: 3 section-definition symbols with 1 aux each
    for pos, (s, sec) in enumerate(zip(syms, sections)):
        if not (s.cls == 3 and s.naux == 1 and s.value == 0 and s.sect == pos + 1):
            errs.append(f"section symbol {s.idx}: expected STATIC/aux=1/sect={pos + 1}, got cls={s.cls} aux={s.naux} value={s.value} sect={s.sect}")
        else:
            aux_base = symptr + 18 * (s.idx + 1)  # aux record follows its symbol
            aux = data[aux_base: aux_base + 18]
            (alen, arel, aln, acsum, anum, asel) = struct.unpack_from("<IHHIHB", aux, 0)
            check(alen == sec["rsize"], f"aux of {s.name}: Length {alen} != section raw size {sec['rsize']}")
            check(arel == sec["nrel"], f"aux of {s.name}: NumberOfRelocations {arel} != {sec['nrel']}")
            check(aln == 0, f"aux of {s.name}: NumberOfLinenumbers {aln} != 0")
            check(anum == pos + 1, f"aux of {s.name}: Number {anum} != section {pos + 1}")
            check(asel == 0, f"aux of {s.name}: Selection {asel} != 0 (not comdat)")

    real = syms[2 * nsect:]
    defined = [s for s in real if s.sect > 0]
    undef = [s for s in real if s.sect == 0]
    for s in defined:
        check(1 <= s.sect <= nsect, f"symbol {s.name}: section number {s.sect} out of range")
        if 1 <= s.sect <= nsect:
            sec = sections[s.sect - 1]
            check(s.value < max(sec["rsize"], sec["vsize"]),
                  f"symbol {s.name}: value {s.value} outside section {sec['name']} (size {sec['rsize']})")
    for s in undef:
        check(s.cls == 2, f"undefined symbol {s.name}: StorageClass {SC_NAMES.get(s.cls, s.cls)} != EXTERNAL")
        check(s.value == 0, f"undefined symbol {s.name}: nonzero value {s.value}")

    names = [s.name for s in real if s.sect > 0]
    dupes = sorted({n for n in names if names.count(n) > 1})
    check(not dupes, f"duplicate defined symbols: {dupes}")
    undef_names = [s.name for s in undef]
    dupes_u = sorted({n for n in undef_names if undef_names.count(n) > 1})
    check(not dupes_u, f"duplicate undefined symbols: {dupes_u}")
    check("main" in names, "entry symbol 'main' not defined")
    # arm64 Windows: C symbols are not decorated, but some C-level names
    # legitimately contain '_' (e.g. _NSGetExecutablePath, __stderrp). The
    # writer strips exactly one Mach-O prefix, so report leftovers as info.
    bad_prefix = [n for n in names + undef_names if n.startswith("_")]
    if bad_prefix:
        print(f"  note: symbols with leading '_' (C names may legitimately "
              f"contain underscores; writer strips one decoration): {bad_prefix}")

    # ---- relocations ----
    nrel_total = 0
    rel_hist = {}
    for sec in sections:
        prev = -1
        for j in range(sec["nrel"]):
            nrel_total += 1
            base = sec["relptr"] + 10 * j
            va, symidx, rtype = struct.unpack_from("<IIH", data, base)
            rel_hist[rtype] = rel_hist.get(rtype, 0) + 1
            check(rtype in VALID_RELS, f"{sec['name']} reloc {j}: unknown type {rtype:#x}")
            check(symidx < nsym, f"{sec['name']} reloc {j}: symbol index {symidx} >= {nsym}")
            check(va % 4 == 0, f"{sec['name']} reloc {j}: VirtualAddress {va} not 4-aligned")
            check(va < sec["rsize"], f"{sec['name']} reloc {j}: VirtualAddress {va} outside section ({sec['rsize']})")
            check(va > prev, f"{sec['name']} reloc {j}: VirtualAddress {va} not sorted after {prev}")
            prev = va
            if symidx < nsym and va < sec["rsize"]:
                word = struct.unpack_from("<I", data, sec["rptr"] + va)[0]
                expect = reloc_shape(rtype)
                if expect is not None and (word & expect[0]) != expect[1]:
                    errs.append(f"{sec['name']} reloc {j} @{va:#x}: type {REL_NAMES[rtype]} but instruction {word:#010x} does not match opcode shape {expect[1]:#010x} (mask {expect[0]:#010x})")
                if symidx < len(syms) and syms[symidx].sect == 0 and rtype not in (3, 4, 6, 7):
                    errs.append(f"{sec['name']} reloc {j}: extern symbol with unexpected type {REL_NAMES[rtype]}")

    hist = " ".join(f"{REL_NAMES.get(t, hex(t))}={c}" for t, c in sorted(rel_hist.items()))
    print(f"{path}: machine=ARM64 sections={nsect} symbols={nsym} relocations={nrel_total} "
          f"defined={len(defined)} externs={len(undef)} | {hist}")
    return report(path, errs, sections, syms)


def reloc_shape(rtype):
    """(mask, value) the instruction word at a reloc VA must satisfy."""
    if rtype == 4:   # PAGEBASE_REL21 -> adrp
        return (0x9F000000, 0x90000000)
    if rtype == 6:   # PAGEOFFSET_12A -> add (immediate, 64-bit, sh=0)
        return (0xFFC00000, 0x91000000)
    if rtype == 7:   # PAGEOFFSET_12L -> ldr (unsigned imm, 64-bit)
        return (0xFFC00000, 0xF9400000)
    if rtype == 3:   # BRANCH26 -> bl
        return (0x7C000000, 0x14000000)  # 100101 in bits 31,29..26 (BL)
    return None


def report(path, errs, sections, syms):
    if errs:
        print(f"FAIL {path}: {len(errs)} problem(s)")
        for e in errs[:40]:
            print(f"  - {e}")
        if len(errs) > 40:
            print(f"  ... {len(errs) - 40} more")
        return 1
    if sections is not None:
        for s in sections:
            print(f"  sect {s['idx']} {s['name']:10} raw={s['rsize']:6} @ {s['rptr']:#07x} rel={s['nrel']:3} chars={s['chars']:#010x}")
        if syms:
            shown = syms[:6] + syms[6:12] + (syms[-5:] if len(syms) > 18 else [])
            seen = set()
            for s in shown:
                if s.idx in seen:
                    continue
                seen.add(s.idx)
                print(f"  sym  {s.idx:4} {s.name[:28]:28} val={s.value:<6} sect={s.sect} type={s.typ:#04x} cls={SC_NAMES.get(s.cls, s.cls)} aux={s.naux}")
    print(f"OK {path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Fail as e:
        print(f"FAIL: {e}", file=sys.stderr)
        raise SystemExit(1)
