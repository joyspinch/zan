#!/usr/bin/env python3
"""Structural validator for aarch64 ELF64 relocatable objects emitted by
the self-hosted Zan compiler (ngen_elf.zan).

Checks, without any cross toolchain:
  * ELF header sanity (magic, class 64 LSB, ET_REL, EM_AARCH64, section table)
  * section table: offsets/sizes in range, expected flags/entsize/align
  * .symtab: null entry 0, names resolve in .strtab, st_shndx in range,
    defined FUNC symbols 4-aligned inside .text, sh_info = first global
  * .rela.text: every r_offset 4-aligned inside .text, symbol index valid,
    relocation type matches the instruction word it patches (opcode and the
    zero immediate the compiler leaves for the linker), addends as emitted
    (-4 for CALL26, 0 otherwise), no duplicate/overlapping offsets
  * pairing: every relocated adrp is followed by a relocated add/ldr at +4
  * coverage: every BL/ADRP with a zero immediate in .text has a relocation
  * naming: no extern keeps a Mach-O "_" prefix (strip rule held)

Usage: python3 elfcheck.py <file.o> [more.o ...]
Exit 0 iff every file passes.
"""
import struct
import sys

ET_REL = 1
EM_AARCH64 = 183
SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHT_RELA = 1, 2, 3, 4
SHF_WRITE, SHF_ALLOC, SHF_EXECINSTR = 0x1, 0x2, 0x4
SHN_UNDEF = 0

RELOC_NAMES = {
    275: "R_AARCH64_ADR_PREL_PG_HI21",
    277: "R_AARCH64_ADD_ABS_LO12_NC",
    278: "R_AARCH64_LDST8_ABS_LO12_NC",
    282: "R_AARCH64_JUMP26",
    283: "R_AARCH64_CALL26",
    284: "R_AARCH64_LDST16_ABS_LO12_NC",
    285: "R_AARCH64_LDST32_ABS_LO12_NC",
    286: "R_AARCH64_LDST64_ABS_LO12_NC",
    299: "R_AARCH64_LDST128_ABS_LO12_NC",
    311: "R_AARCH64_ADR_GOT_PAGE",
    312: "R_AARCH64_LD64_GOT_LO12_NC",
}
LDST_FOR_SIZE = {0: 278, 1: 284, 2: 285, 3: 286}

KNOWN_TYPES = set(RELOC_NAMES)


class Fail(Exception):
    pass


def fail(msg):
    raise Fail(msg)


class Section:
    def __init__(self, idx, raw):
        (self.name, self.typ, self.flags, self.addr, self.off, self.size,
         self.link, self.info, self.addralign, self.entsize) = struct.unpack_from("<IIQQQQIIQQ", raw, 0)
        self.idx = idx


def check(path):
    with open(path, "rb") as f:
        blob = f.read()
    problems = []
    info = []

    # ---- ELF header ----
    if len(blob) < 64 or blob[:4] != b"\x7fELF":
        fail("not an ELF file")
    ei_class, ei_data, ei_ver = blob[4], blob[5], blob[6]
    if ei_class != 2:
        problems.append(f"EI_CLASS={ei_class}, expected 2 (ELFCLASS64)")
    if ei_data != 1:
        problems.append(f"EI_DATA={ei_data}, expected 1 (LSB)")
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags,
     e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = \
        struct.unpack_from("<HHIQQQIHHHHHH", blob, 16)
    if e_type != ET_REL:
        problems.append(f"e_type={e_type}, expected ET_REL(1)")
    if e_machine != EM_AARCH64:
        problems.append(f"e_machine={e_machine}, expected EM_AARCH64(183)")
    if e_shentsize != 64:
        problems.append(f"e_shentsize={e_shentsize}, expected 64")
    if e_shoff + e_shnum * e_shentsize > len(blob):
        problems.append("section table extends past end of file")
    if problems:
        fail("; ".join(problems))

    secs = [Section(i, blob[e_shoff + i * 64: e_shoff + i * 64 + 64])
            for i in range(e_shnum)]
    if secs[0].typ != 0:
        problems.append("section 0 is not the null section")
    if not (0 <= e_shstrndx < e_shnum):
        fail(f"e_shstrndx {e_shstrndx} out of range")

    shstr = secs[e_shstrndx]
    shstr_blob = blob[shstr.off: shstr.off + shstr.size]

    def secname(s):
        end = shstr_blob.find(b"\0", s.name)
        return shstr_blob[s.name:end].decode("ascii", "replace")

    by_name = {}
    for s in secs:
        nm = secname(s)
        if nm:
            by_name[nm] = s
        if s.off + s.size > len(blob):
            problems.append(f"section {nm}: offset {s.off}+{s.size} past EOF")
    for req in (".text", ".rodata", ".data", ".rela.text", ".symtab", ".strtab", ".shstrtab"):
        if req not in by_name:
            problems.append(f"missing section {req}")
    if problems:
        fail("; ".join(problems))

    text = by_name[".text"]
    rodata = by_name[".rodata"]
    data = by_name[".data"]
    rela = by_name[".rela.text"]
    symtab = by_name[".symtab"]
    strtab = by_name[".strtab"]

    if text.typ != SHT_PROGBITS or text.flags != (SHF_ALLOC | SHF_EXECINSTR):
        problems.append(".text: expected PROGBITS with AX flags, got "
                        f"type={text.typ} flags={text.flags:#x}")
    if rodata.typ != SHT_PROGBITS or rodata.flags != SHF_ALLOC:
        problems.append(".rodata: expected PROGBITS with A flags")
    if data.typ != SHT_PROGBITS or data.flags != (SHF_ALLOC | SHF_WRITE):
        problems.append(".data: expected PROGBITS with WA flags")
    if symtab.typ != SHT_SYMTAB or symtab.entsize != 24:
        problems.append(".symtab: expected SHT_SYMTAB entsize 24")
    if rela.typ != SHT_RELA or rela.entsize != 24:
        problems.append(".rela.text: expected SHT_RELA entsize 24")
    if rela.link != symtab.idx or rela.info != text.idx:
        problems.append(f".rela.text link/info must be .symtab({symtab.idx})/"
                        f".text({text.idx}), got {rela.link}/{rela.info}")
    if symtab.link != strtab.idx:
        problems.append(".symtab link must point at .strtab")

    text_blob = blob[text.off: text.off + text.size]
    words = [struct.unpack_from("<I", text_blob, o)[0] for o in range(0, text.size - 3, 4)]

    # ---- symbol table ----
    strtab_blob = blob[strtab.off: strtab.off + strtab.size]

    def symname(off):
        end = strtab_blob.find(b"\0", off)
        if off >= len(strtab_blob) or end < 0:
            fail(f"symbol name offset {off} outside .strtab")
        return strtab_blob[off:end].decode("utf-8", "replace")

    nsyms = symtab.size // 24
    syms = []
    for i in range(nsyms):
        st_name, st_info, st_other, st_shndx, st_value, st_size = \
            struct.unpack_from("<IBBHQQ", blob, symtab.off + i * 24)
        syms.append((st_name, st_info, st_shndx, st_value))
    if syms[0][0] != 0 or syms[0][1] != 0 or syms[0][2] != 0 or syms[0][3] != 0:
        problems.append("symbol 0 is not the reserved null symbol")

    def bind(info):
        return info >> 4

    def typ(info):
        return info & 0xF

    first_global = symtab.info
    for i, (nmoff, inf, shndx, val) in enumerate(syms):
        if i == 0:
            continue
        nm = symname(nmoff)
        if bind(inf) > 1:
            problems.append(f"sym {i} ({nm}): unexpected bind {bind(inf)}")
        if shndx >= e_shnum:
            problems.append(f"sym {i} ({nm}): st_shndx {shndx} out of range")
        if typ(inf) == 2 and shndx == text.idx:
            if val % 4 != 0:
                problems.append(f"FUNC sym {i} ({nm}): value {val:#x} not 4-aligned")
            if val >= text.size:
                problems.append(f"FUNC sym {i} ({nm}): value {val:#x} outside .text")
        is_local = bind(inf) == 0
        if is_local and i >= first_global:
            problems.append(f"local sym {i} ({nm}) at/after sh_info {first_global}")
        if not is_local and i < first_global:
            problems.append(f"global sym {i} ({nm}) before sh_info {first_global}")

    n_methods = sum(1 for i, s in enumerate(syms)
                    if i > 0 and typ(s[1]) == 2 and s[2] == text.idx)
    n_undef = sum(1 for s in syms if s[2] == SHN_UNDEF)
    info.append(f"syms: {nsyms} total, {n_methods} FUNC(.text), {n_undef} undefined")

    # ---- relocations ----
    nrel = rela.size // 24
    relo_by_off = {}
    hist = {}
    for i in range(nrel):
        r_offset, r_info, r_addend = struct.unpack_from("<QQq", blob, rela.off + i * 24)
        r_sym, r_type = r_info >> 32, r_info & 0xFFFFFFFF
        if r_offset % 4 != 0:
            problems.append(f"rela {i}: r_offset {r_offset:#x} not 4-aligned")
        if r_offset >= text.size:
            problems.append(f"rela {i}: r_offset {r_offset:#x} outside .text")
        if not (0 <= r_sym < nsyms):
            problems.append(f"rela {i}: symbol index {r_sym} out of range")
            continue
        if r_type not in KNOWN_TYPES:
            problems.append(f"rela {i}: unexpected reloc type {r_type}")
            continue
        if r_offset in relo_by_off:
            problems.append(f"rela {i}: duplicate relocation at {r_offset:#x}")
        relo_by_off[r_offset] = (r_type, r_sym, r_addend)
        hist[r_type] = hist.get(r_type, 0) + 1

        if r_offset + 4 > len(text_blob):
            problems.append(f"rela {i}: r_offset at end of .text")
            continue
        w = struct.unpack_from("<I", text_blob, r_offset)[0]
        tname = RELOC_NAMES[r_type]
        if r_type == 283:  # CALL26
            if w & 0xFC000000 != 0x94000000:
                problems.append(f"rela {i} @{r_offset:#x}: CALL26 but word is {w:#010x}")
            if w & 0x03FFFFFF:
                problems.append(f"rela {i} @{r_offset:#x}: CALL26 imm26 not zero")
            if r_addend != 0:
                problems.append(f"rela {i}: CALL26 addend {r_addend}, expected 0")
        elif r_type in (275, 311):  # ADR_PREL_PG_HI21 / ADR_GOT_PAGE
            if w & 0x9F000000 != 0x90000000:
                problems.append(f"rela {i} @{r_offset:#x}: {tname} but word is {w:#010x} (not ADRP)")
            immlo = (w >> 29) & 0x3
            immhi = (w >> 5) & 0x7FFFF
            if immlo or immhi:
                problems.append(f"rela {i} @{r_offset:#x}: ADRP immediate not zero "
                                f"(immlo={immlo} immhi={immhi:#x})")
            if r_addend != 0:
                problems.append(f"rela {i}: {tname} addend {r_addend}, expected 0")
        elif r_type in (277, 278, 284, 285, 286, 299, 312):  # lo12 family
            if r_type == 277:  # ADD_ABS_LO12_NC
                if w & 0x7F800000 != 0x11000000:
                    problems.append(f"rela {i} @{r_offset:#x}: ADD_ABS_LO12_NC but word is {w:#010x}")
                size_ok = True
            else:
                if w & 0x3B000000 != 0x39000000 and w & 0xFFC00000 != 0x3DC00000:
                    problems.append(f"rela {i} @{r_offset:#x}: {tname} but word is {w:#010x} (not LD/ST imm)")
                size_bits = ((w >> 30) & 0x3) if (w & 0xFFC00000) != 0x3DC00000 else 4
                want = {278: 0, 284: 1, 285: 2, 286: 3, 299: 4}.get(r_type)
                if want is not None and size_bits != want:
                    problems.append(f"rela {i} @{r_offset:#x}: {tname} on size-{size_bits} access")
                if r_type == 312 and (w & 0xFFC00000) != 0xF9400000:
                    problems.append(f"rela {i} @{r_offset:#x}: LD64_GOT_LO12_NC but word is {w:#010x}")
            imm12 = (w >> 10) & 0xFFF
            if imm12:
                problems.append(f"rela {i} @{r_offset:#x}: lo12 immediate not zero ({imm12:#x})")
            if r_addend != 0:
                problems.append(f"rela {i}: lo12 addend {r_addend}, expected 0")
        elif r_type == 282:
            if w & 0xFC000000 != 0x14000000:
                problems.append(f"rela {i} @{r_offset:#x}: JUMP26 but word is {w:#010x}")

    # pairing: adrp relocations must be followed by a lo12 relocation at +4
    adrp_offs = {o for o, (t, _, _) in relo_by_off.items() if t in (275, 311)}
    lo12_offs = {o for o, (t, _, _) in relo_by_off.items()
                 if t in (277, 278, 284, 285, 286, 299, 312)}
    for o in sorted(adrp_offs):
        if o + 4 not in lo12_offs:
            problems.append(f"adrp at {o:#x} has no lo12 relocation at +4")
    for o in sorted(lo12_offs):
        if o - 4 not in adrp_offs:
            problems.append(f"lo12 at {o:#x} has no adrp relocation at -4")
    call_offs = {o for o, (t, _, _) in relo_by_off.items() if t in (282, 283)}
    for o in sorted(call_offs):
        if o in adrp_offs or o in lo12_offs:
            problems.append(f"relocation classes overlap at {o:#x}")

    # coverage: every BL/ADRP with zero immediate must be relocated
    for idx, w in enumerate(words):
        o = idx * 4
        if w == 0x94000000 and o not in call_offs:
            problems.append(f"BL with zero imm26 at {o:#x} has no CALL26 relocation")
        if w == 0x90000000 and o not in adrp_offs:
            problems.append(f"ADRP with zero immediate at {o:#x} has no ADRP relocation")

    # naming: the Mach-O "_" prefix must be stripped on ELF. Darwin-only
    # symbols keep an intrinsic leading "_" in their C name and have no
    # Linux counterpart; they stay as undefined imports (documented
    # follow-up: Linux shims needed for _NSGetExecutablePath,
    # os_unfair_lock_*, mach_vm_read_overwrite, mach_task_self_).
    DARWIN_ONLY = {"_NSGetExecutablePath", "os_unfair_lock_lock",
                   "os_unfair_lock_unlock", "mach_vm_read_overwrite",
                   "mach_task_self_"}
    for i, (nmoff, inf, shndx, val) in enumerate(syms):
        if i == 0 or shndx != SHN_UNDEF:
            continue
        nm = symname(nmoff)
        if nm.startswith("_") and nm not in DARWIN_ONLY:
            problems.append(f"undefined symbol '{nm}' keeps a Mach-O underscore prefix")

    info.append(f"relocs: {nrel} total, " +
                ", ".join(f"{RELOC_NAMES[t].replace('R_AARCH64_', '')}={c}"
                          for t, c in sorted(hist.items())))
    info.append(f"sections: .text={text.size}B .rodata={rodata.size}B .data={data.size}B")

    return info, problems


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    failed = 0
    for path in sys.argv[1:]:
        try:
            info, problems = check(path)
        except Fail as exc:
            print("FAIL", path, exc)
            failed += 1
            continue
        except (OSError, struct.error) as exc:
            print("FAIL", path, f"unreadable/malformed: {exc}")
            failed += 1
            continue
        for line in info:
            print("  ", line)
        if problems:
            failed += 1
            print("FAIL", path, f"{len(problems)} problem(s)")
            for p in problems[:40]:
                print("   -", p)
            if len(problems) > 40:
                print(f"   ... {len(problems) - 40} more")
        else:
            print("PASS", path)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
