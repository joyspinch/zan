#!/usr/bin/env python3
"""Localize non-API global symbols in an ngen Mach-O object.

The native backend emits its full runtime helper library into every object
with global (N_SECT|N_EXT) symbols, so two ngen objects can never be linked
together as-is. The Zan runtime object (runtime_core.zan compiled by the
bootstrap compiler) is the first second ngen object in the link, and this
tool makes it linkable: every defined global symbol outside the exported
API surface is flipped to local (N_SECT), so the consuming program keeps
using its own emitted helpers while the zan_* ABI symbols stay global.

Undefined externs are untouched — they must stay global to bind against
libSystem / the remaining C transition objects.
"""
import struct
import sys


def main():
    if len(sys.argv) < 3:
        sys.exit('usage: localize_syms.py <obj> <keep-global-name>...')
    path = sys.argv[1]
    keep = set(sys.argv[2:])
    with open(path, 'rb') as f:
        img = bytearray(f.read())
    magic, = struct.unpack_from('<I', img, 0)
    if magic != 0xfeedfacf:  # MH_MAGIC_64, little-endian
        sys.exit(f'{path}: not a little-endian 64-bit Mach-O object')
    ncmds, = struct.unpack_from('<I', img, 16)
    off = 32
    kept = localized = 0
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from('<II', img, off)
        if cmd == 0x2:  # LC_SYMTAB
            symoff, nsyms, stroff, _strsize = struct.unpack_from('<IIII', img, off + 8)
            for i in range(nsyms):
                e = symoff + 16 * i
                strx, = struct.unpack_from('<I', img, e)
                end = img.index(b'\0', stroff + strx)
                name = img[stroff + strx:end].decode()
                n_type = img[e + 4]
                if n_type != 0x0F:  # only N_SECT|N_EXT (defined globals)
                    continue
                if name in keep:
                    kept += 1
                    print(f'global  {name}')
                else:
                    img[e + 4] = 0x0E  # N_SECT, local
                    localized += 1
        off += cmdsize
    if kept == 0:
        sys.exit(f'{path}: none of the keep-list symbols were found defined')
    with open(path, 'wb') as f:
        f.write(img)
    print(f'{path}: {kept} kept global, {localized} localized')


if __name__ == '__main__':
    main()
