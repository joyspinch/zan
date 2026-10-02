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
DRIVERS = ROOT.parent / 'zan-lang/stdlib'
_driver_index = None


def driver_dylibs(undef):
    """DllImport 驱动库解析:stdlib **/drivers/macos-arm64 里按导出符号
    覆盖未定义集的 dylib。GUI 家族的金档检查此前没有这一步,弱未定
    符号落成空指针调用,进程在 Window_ctor 里不可杀地停驻(U 状态,
    SIGKILL 与调试器暂停都无效,chart_axis_dataminmax 实证),把整条
    检查队列冻死。"""
    global _driver_index
    if _driver_index is None:
        _driver_index = []
        for dylib in sorted(DRIVERS.glob('**/drivers/macos-arm64/*.dylib')):
            try:
                exp = subprocess.check_output(
                    ['nm', '-gU', '--defined-only', str(dylib)], text=True)
            except (subprocess.CalledProcessError, OSError):
                continue
            syms = {ln.split()[-1] for ln in exp.splitlines() if ln.strip()}
            _driver_index.append((dylib, syms))
    return [dylib for dylib, syms in _driver_index if undef & syms]


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
        # resolve:子进程 cwd 换到 cdir,相对 suite 路径会在那里失效
        src = (args.suite / (case + '.zan')).resolve()
        golden = (args.suite / (case + '.out')).resolve()
        if not src.is_file() or not golden.is_file():
            print(f'FAIL {case}: missing source or golden', flush=True)
            counts['missing'] += 1
            continue
        cdir = work / f'golden-{case}'
        cdir.mkdir(parents=True, exist_ok=True)
        sl = cdir / 'stdlib'
        if not sl.exists():
            sl.symlink_to(ROOT / 'stdlib', target_is_directory=True)
        # 编译前摘掉残留数据链/scratch:上一例失败现场会把含 .zan 的树
        # 留在 cdir,编译器会把它当源码收编(examples/gui_charts 的 Gui
        # 命名空间撞车实证)。
        for junk in ('tests', 'examples'):
            tj = cdir / junk
            if tj.is_symlink() or tj.exists():
                tj.unlink()

        def run(phase, cmd, timeout=120):
            # 不可杀子进程版 subprocess.run:UE 停驻(S 状态查不出,kill
            # 后仍不退)时 communicate 二段 15s 死线,超了就弃例继续,
            # 返回 rc=None 由调用方记 wedge。
            p = subprocess.Popen(cmd, cwd=cdir, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
            try:
                out, err = p.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                try:
                    out, err = p.communicate(timeout=15)
                except subprocess.TimeoutExpired:
                    return None, b''
            (cdir / (phase + '.stderr')).write_bytes(err)
            (cdir / (phase + '.stdout')).write_bytes(out)
            return p.returncode, out

        rc, err = run('compile', [str(args.seed), str(cdir / 'native.o'), str(src)])
        if rc != 0:
            print(f'FAIL {case}: compile exit {rc}: {err.decode(errors="replace")[:300]}',
                  flush=True)
            counts['compile_failed'] += 1
            continue

        undef = set()
        try:
            ul = subprocess.check_output(['nm', '-u', str(cdir / 'native.o')],
                                         text=True)
            undef = {ln.strip() for ln in ul.splitlines() if ln.strip()}
        except (subprocess.CalledProcessError, OSError):
            pass
        drivers = driver_dylibs(undef)
        drpaths = sorted({str(d.parent) for d in drivers})

        reflibs, rpaths = [], []
        refexe = find_reference(case, artifacts)
        if refexe:
            reflibs, rpaths = reference_dylibs(refexe)
        rpath_args = [x for r in rpaths + drpaths for x in ('-rpath', r)]
        rc, err = run('link', ['/usr/bin/ld', '-arch', 'arm64', '-e', '_main',
                               '-platform_version', 'macos', '11.0', version,
                               '-syslibroot', sdk, '-L' + sdk + '/usr/lib',
                               '-undefined', 'dynamic_lookup',
                               *extra_libs, *reflibs, *map(str, drivers),
                               *rpath_args,
                               '-o', str(cdir / 'native'), str(cdir / 'native.o'),
                               *map(str, args.runtime), '-lSystem'])
        if rc != 0:
            print(f'FAIL {case}: link exit {rc}: {err.decode(errors="replace")[:300]}',
                  flush=True)
            counts['link_failed'] += 1
            continue

        # 数据链与 scratch 只在 RUN 前落位:编译期 cdir 里多出含 .zan 的
        # 树会被编译器当源码收编(examples/gui_charts 的 Gui 命名空间
        # 撞车实证),编译期只许 stdlib 一条链。
        for lnk, src in (('tests', CONF.parent.parent / 'tests'),
                         ('examples', CONF.parent.parent / 'examples')):
            tl = cdir / lnk
            if tl.is_symlink() and tl.readlink() == src:
                pass
            else:
                if tl.is_symlink() or tl.exists():
                    tl.unlink()
                tl.symlink_to(src, target_is_directory=True)
        sc = cdir / '_scratch'
        if not sc.exists():
            sc.mkdir()

        rc, out = run('run', [str(cdir / 'native')], timeout=60)
        if rc is None:
            print(f'FAIL {case}: run wedge (unkillable child)', flush=True)
            counts['run_wedge'] += 1
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
