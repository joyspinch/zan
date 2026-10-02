#!/usr/bin/env python3
"""Golden-verify conformance cases natively, no oracle run.

The parity sweep compares native vs reference; cases where the ORACLE side
cannot complete (reference_compile_failed / reference_timeout / oracle
exit-wedge) never reach a comparison, so their NATIVE output is never
checked against the conformance .out golden. This tool closes that gap:
compile+link+run natively with the same recipe the sweep uses (stdlib
snapshot, OpenSSL, the reference's own DllImport driver dylibs when a
reference binary is available) and diff stdout against tests/conformance
/<case>.out.
"""
import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONF = ROOT.parent / 'zan-lang/tests/conformance'


def reference_dylibs(refexe):
    """Same extraction the sweep uses (see native_parity.reference_dylibs)."""
    try:
        out = subprocess.check_output(['otool', '-L', str(refexe)], text=True)
        rp = subprocess.check_output(['otool', '-l', str(refexe)], text=True)
    except (subprocess.CalledProcessError, OSError):
        return [], []
    rpaths = []
    lines = rp.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            for j in (i + 1, i + 2, i + 3):
                if j >= len(lines):
                    break
                nxt = lines[j].strip()
                if nxt.startswith('path '):
                    rpaths.append(nxt[5:].split(' (')[0])
                    break
    libs = []
    for line in out.splitlines()[1:]:
        name = line.strip().split(' (')[0].strip()
        if not name or 'libSystem' in name:
            continue
        if name.startswith('@rpath/'):
            rel = name[len('@rpath/'):]
            for r in rpaths:
                cand = Path(r) / rel
                if cand.exists():
                    libs.append(str(cand))
                    break
        elif name.startswith('/'):
            libs.append(name)
    seen, uniq = set(), []
    for lib in libs:
        if lib not in seen:
            seen.add(lib)
            uniq.append(lib)
    return uniq, rpaths


def find_reference(case, artifact_dirs):
    for d in artifact_dirs:
        cand = Path(d)
        if not cand.is_dir():
            continue
        for sub in sorted(cand.glob(f'*-{case}')):
            exe = sub / 'reference'
            if exe.exists():
                return exe
    return None


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--runtime', type=Path, nargs='+', required=True)
    parser.add_argument('--suite', type=Path, default=CONF)
    parser.add_argument('--work', type=Path, default=None,
                        help='dir holding zan-native-parity-* artifact trees to '
                             'scavenge reference binaries from (repeatable via '
                             'multiple --work flags is NOT supported; pass the '
                             'parent temp dir)')
    parser.add_argument('cases', nargs='+')
    args = parser.parse_args()

    sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'],
                                  text=True).strip()
    version = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'],
                                      text=True).strip()
    homebrew = Path.home() / '.homebrew'
    extra_libs = []
    for prefix in (homebrew / 'opt/openssl@3', homebrew / 'opt/openssl',
                   Path('/opt/homebrew/opt/openssl@3')):
        if (prefix / 'lib/libssl.dylib').exists():
            extra_libs += ['-L' + str(prefix / 'lib'), '-lssl', '-lcrypto']
            break

    artifacts = []
    if args.work:
        artifacts = sorted(args.work.glob('zan-native-parity-*'))

    work = args.work or Path(tempfile.mkdtemp(prefix='zan-golden-native-'))
    counts = Counter()
    for case in args.cases:
        src = args.suite / (case + '.zan')
        golden = args.suite / (case + '.out')
        if not src.is_file() or not golden.is_file():
            print(f'FAIL {case}: missing source or golden', flush=True)
            counts['missing'] += 1
            continue
        cdir = work / f'golden-{case}'
        cdir.mkdir(parents=True, exist_ok=True)
        (cdir / 'stdlib').symlink_to(ROOT / 'stdlib', target_is_directory=True)

        def run(phase, cmd, timeout=120):
            proc = subprocess.run(cmd, cwd=cdir, capture_output=True, timeout=timeout)
            (cdir / (phase + '.stderr')).write_bytes(proc.stderr)
            (cdir / (phase + '.stdout')).write_bytes(proc.stdout)
            return proc.returncode, proc.stdout

        rc, err = run('compile', [str(args.seed), str(cdir / 'native.o'), str(src)])
        if rc != 0:
            print(f'FAIL {case}: compile exit {rc}: {err.decode(errors="replace")[:300]}',
                  flush=True)
            counts['compile_failed'] += 1
            continue

        reflibs, rpaths = [], []
        refexe = find_reference(case, artifacts)
        if refexe:
            reflibs, rpaths = reference_dylibs(refexe)
        rpath_args = [x for r in rpaths for x in ('-rpath', r)]
        rc, err = run('link', ['/usr/bin/ld', '-arch', 'arm64', '-e', '_main',
                               '-platform_version', 'macos', '11.0', version,
                               '-syslibroot', sdk, '-L' + sdk + '/usr/lib',
                               '-undefined', 'dynamic_lookup',
                               *extra_libs, *reflibs, *rpath_args,
                               '-o', str(cdir / 'native'), str(cdir / 'native.o'),
                               *map(str, args.runtime), '-lSystem'])
        if rc != 0:
            print(f'FAIL {case}: link exit {rc}: {err.decode(errors="replace")[:300]}',
                  flush=True)
            counts['link_failed'] += 1
            continue

        try:
            rc, out = run('run', [str(cdir / 'native')])
        except subprocess.TimeoutExpired:
            print(f'FAIL {case}: run timeout', flush=True)
            counts['run_timeout'] += 1
            continue
        if out == golden.read_bytes():
            print(f'PASS {case}', flush=True)
            counts['pass'] += 1
        else:
            gl = out.decode(errors='replace').splitlines()
            xl = golden.read_text(errors='replace').splitlines()
            first = next((i for i in range(max(len(gl), len(xl)))
                          if (gl[i] if i < len(gl) else '<EOF>') !=
                             (xl[i] if i < len(xl) else '<EOF>')), -1)
            detail = (f'got={gl[first]!r} exp={xl[first]!r}' if first >= 0 else '?')
            print(f'FAIL {case}: exit {rc} lines {len(gl)}vs{len(xl)} @{first+1} {detail}',
                  flush=True)
            counts['golden_diff'] += 1
    print(json.dumps(dict(counts), sort_keys=True))
    return int(counts.get('pass', 0) != len(args.cases))


if __name__ == '__main__':
    raise SystemExit(main())
