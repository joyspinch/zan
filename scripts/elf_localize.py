#!/usr/bin/env python3
"""Localize non-API global symbols in an ngen ELF object.

ELF sibling of localize_syms.py (which is Mach-O only): ngen emits the
whole stdlib helper family (zan_dt_*, zan_dir_*, ...) as global symbols
into every object, so a runtime object linked alongside a program object
would collide with the program's own copies. Localizing the runtime
object's non-API globals keeps its definitions out of the dynamic symbol
resolution while leaving its references untouched.

    elf_localize.py <object> [--weaken] <keep-global>... [--weaken] <weak...>

Every DEFINED global symbol whose name is not in the keep list becomes
local; the --weaken list names symbols that become WEAK instead of local
(embed_* style optional overrides). Undefined globals (imports such as
malloc) are never touched. Relocations reference symbols by index, so
binding edits leave them valid.
"""

import struct
import sys


def fail(msg):
    sys.exit(f'{sys.argv[0]}: {msg}')


def main():
    args = sys.argv[1:]
    if not args:
        fail('usage: elf_localize.py <object> [--weaken] <keep...>')
    path = args[0]
    keeps, weaks = set(), set()
    target = keeps
    for a in args[1:]:
        if a == '--weaken':
            target = weaks
            continue
        target.add(a.lstrip('_'))

    img = bytearray(open(path, 'rb').read())
    if img[:4] != b'\x7fELF' or img[4] != 2 or img[5] != 1:
        fail(f'{path}: not a little-endian 64-bit ELF object')

    e_shoff, = struct.unpack_from('<Q', img, 0x28)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', img, 0x3a)

    def shdr(i):
        off = e_shoff + i * e_shentsize
        name, typ, flags, addr, offset, size, link, info, align, entsize = \
            struct.unpack_from('<IIQQQQIIQQ', img, off)
        return [name, typ, flags, addr, offset, size, link, info, align,
                entsize, off]

    sections = [shdr(i) for i in range(e_shnum)]
    strtab_off = sections[e_shstrndx][4]

    def cstr(off):
        end = img.index(b'\0', off)
        return img[off:end].decode(errors='replace')

    symtabs = [s for s in sections if s[1] == 2]  # SHT_SYMTAB
    if not symtabs:
        fail(f'{path}: no .symtab')
    if len(symtabs) != 1:
        fail(f'{path}: multiple .symtab sections')

    st = symtabs[0]
    entsize = st[9] or 24
    count = st[5] // entsize
    symtab_strtab_off = sections[st[6]][4]  # sh_link -> .strtab

    def sym_name(i):
        off = st[4] + i * entsize
        st_name, = struct.unpack_from('<I', img, off)
        if st_name == 0:
            return ''
        return cstr(symtab_strtab_off + st_name)

    localized, weakened = 0, 0
    first_nonlocal = count
    for i in range(count):
        off = st[4] + i * entsize
        st_name, st_info, st_other, st_shndx, st_value, st_size = \
            struct.unpack_from('<IBBHQQ', img, off)
        bind, typ = st_info >> 4, st_info & 0xF
        name = sym_name(i)
        new_bind = bind
        plain = name.lstrip('_')
        if bind == 1 and st_shndx != 0 and name:
            if plain in weaks:
                new_bind = 2
            elif plain not in keeps:
                new_bind = 0
        if new_bind != bind:
            struct.pack_into('<B', img, off + 4, (new_bind << 4) | typ)
            if new_bind == 0:
                localized += 1
            elif new_bind == 2:
                weakened += 1
        if new_bind != 0 and i < first_nonlocal:
            first_nonlocal = i

    # ld.lld requires all locals to precede the first non-local, so the
    # table is rewritten in stable partition order (null symbol stays at
    # index 0) and every relocation's symbol index is remapped.
    order = [0]  # the null symbol
    for i in range(1, count):
        off = st[4] + i * entsize
        st_info, = struct.unpack_from('<B', img, off + 4)
        if st_info >> 4 == 0:
            order.append(i)
    n_locals = len(order)
    for i in range(1, count):
        off = st[4] + i * entsize
        st_info, = struct.unpack_from('<B', img, off + 4)
        if st_info >> 4 != 0:
            order.append(i)
    remap = {old: new for new, old in enumerate(order)}

    body = b''.join(bytes(img[st[4] + old * entsize:
                             st[4] + (old + 1) * entsize]) for old in order)
    img[st[4]:st[4] + count * entsize] = body

    for sec in sections:
        if sec[1] not in (4, 9):  # SHT_RELA / SHT_REL
            continue
        ent = sec[9] or 16 if sec[1] == 4 else sec[9] or 8
        n = sec[5] // ent
        for i in range(n):
            off = sec[4] + i * ent
            if sec[1] == 4:
                r_offset, r_info, r_addend = struct.unpack_from('<QQq', img, off)
                sym, typ = r_info >> 32, r_info & 0xFFFFFFFF
                if sym:
                    sym = remap[sym]
                struct.pack_into('<QQq', img, off, r_offset,
                                 (sym << 32) | typ, r_addend)
            else:
                r_offset, r_info = struct.unpack_from('<QQ', img, off)
                sym, typ = r_info >> 32, r_info & 0xFFFFFFFF
                if sym:
                    sym = remap[sym]
                struct.pack_into('<QQ', img, off, r_offset,
                                 (sym << 32) | typ)

    struct.pack_into('<I', img, st[10] + 44, n_locals)  # sh_info

    open(path, 'wb').write(img)
    print(f'{path}: localized {localized}, weakened {weakened}, '
          f'kept {len(keeps)} API symbols, {n_locals} locals / '
          f'{count - n_locals} globals')


if __name__ == '__main__':
    main()
