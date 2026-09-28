#!/usr/bin/env python3
"""Compare selected conformance cases with a reference compiler; full sweeps are explicit."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def normalized(data):
    return b'\n'.join(line.rstrip() for line in data.splitlines())


def reference_dylibs(refexe):
    """Dylibs the reference binary bound plus its rpath search dirs.

    The reference resolves [DllImport] driver externs by linking the
    stdlib's bundled driver dylibs (@rpath/... + LC_RPATH) and Homebrew
    system deps. The native link must resolve the same externs the same
    way -- dyld's dynamic_lookup only defers them to a fault at call
    time. Returns (absolute dylib paths, LC_RPATH dirs); the link passes
    the dylib paths (ld binds them by their @rpath install name) together
    with -rpath flags so dyld can find them.
    """
    try:
        out = subprocess.check_output(['otool', '-L', str(refexe)], text=True)
        rp = subprocess.check_output(['otool', '-l', str(refexe)], text=True)
    except (subprocess.CalledProcessError, OSError):
        return [], []
    rpaths = []
    libs = []
    lines = rp.splitlines()
    for i, line in enumerate(lines):
        if line.strip() == 'cmd LC_RPATH':
            # otool prints cmdsize (and sometimes more) before the path
            for j in (i + 1, i + 2, i + 3):
                if j >= len(lines):
                    break
                nxt = lines[j].strip()
                if nxt.startswith('path '):
                    rpaths.append(nxt[5:].split(' (')[0])
                    break
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', required=True, type=Path)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--runtime', default=os.environ.get('RT_OBJS', ''))
    parser.add_argument('--all', action='store_true')
    parser.add_argument('--suite', type=Path, default=ROOT.parent / 'zan-lang/tests/conformance')
    parser.add_argument('--timeout', type=int, default=60)
    parser.add_argument('cases', nargs='*')
    args = parser.parse_args()
    if args.all == bool(args.cases):
        parser.error('Select named cases OR --all; a full sweep is never implicit.')
    seed, reference = args.seed.resolve(), args.reference.resolve()
    runtime = [Path(p).resolve() for p in shlex.split(args.runtime)]
    cases = sorted(args.suite.glob('*.zan')) if args.all else [args.suite / (name if name.endswith('.zan') else name + '.zan') for name in args.cases]
    for path in cases:
        if not path.is_file():
            parser.error(f'Missing case: {path}')
    sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    version = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], text=True).strip()
    # The reference compiler resolves leftover DllImport symbols by probing
    # homebrew dylibs (libssl/libcrypto among them); mirror that so TLS
    # stdlib cases link against the same OpenSSL build.
    homebrew = Path.home() / '.homebrew'
    extra_libs = []
    for prefix in (homebrew / 'opt/openssl@3', homebrew / 'opt/openssl', Path('/opt/homebrew/opt/openssl@3')):
        if (prefix / 'lib/libssl.dylib').exists():
            extra_libs += ['-L' + str(prefix / 'lib'), '-lssl', '-lcrypto']
            break
    work = Path(tempfile.mkdtemp(prefix='zan-native-parity-'))
    manifest = {'seed': str(seed), 'seed_sha256': digest(seed), 'reference': str(reference),
                'reference_sha256': digest(reference), 'sdk': sdk, 'sdk_version': version,
                'runtime': {str(p): digest(p) for p in runtime},
                'sources': {str(p.resolve()): digest(p) for p in cases}}
    (work / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Artifacts:', work, flush=True)
    counts = Counter()
    with (work / 'results.jsonl').open('w') as results:
        for i, src in enumerate(cases):
            src = src.resolve()
            directory = work / f'{i:04d}-{src.stem}'
            directory.mkdir()
            refexe, obj, exe = directory / 'reference', directory / 'native.o', directory / 'native'
            record = {'case': src.stem, 'source': str(src), 'logs': str(directory)}

            def run(phase, cmd):
                (directory / (phase + '.command.json')).write_text(json.dumps(cmd) + '\n')
                try:
                    proc = subprocess.run(cmd, cwd=directory, capture_output=True, timeout=args.timeout)
                    output, error, rc = proc.stdout, proc.stderr, proc.returncode
                except subprocess.TimeoutExpired as exc:
                    output, error, rc = exc.stdout or b'', exc.stderr or b'', 'timeout'
                (directory / (phase + '.stdout')).write_bytes(output)
                (directory / (phase + '.stderr')).write_bytes(error)
                record[phase] = rc
                return rc, output

            # Both compilers find the same standard-library snapshot from their cwd.
            (directory / 'stdlib').symlink_to(ROOT / 'stdlib', target_is_directory=True)
            rc, _ = run('reference_compile', [str(reference), str(src), '-o', str(refexe)])
            if rc != 0:
                status = 'reference_compile_failed'
            else:
                rrc, rout = run('reference_run', [str(refexe)])
                if rrc == 'timeout':
                    status = 'reference_timeout'
                else:
                    rc, _ = run('native_compile', [str(seed), str(obj), str(src)])
                    if rc != 0:
                        status = 'native_compile_failed'
                    else:
                        reflibs, refrpaths = reference_dylibs(refexe)
                        rpath_args = [x for r in refrpaths for x in ('-rpath', r)]
                        rc, _ = run('native_link', ['/usr/bin/ld', '-arch', 'arm64', '-e', '_main',
                        '-platform_version', 'macos', '11.0', version, '-syslibroot', sdk,
                        '-L' + sdk + '/usr/lib',
                        # DllImport externs the stdlib keeps referenced
                        # (TLS wrappers) resolve via homebrew OpenSSL, as
                        # the reference linker does; anything still left
                        # over defers to dyld, so a called-but-unresolved
                        # symbol faults exactly where a native library
                        # would have been required.
                        '-undefined', 'dynamic_lookup',
                        *extra_libs,
                        # the reference's own DllImport driver dylibs
                        # (@rpath/... resolved via its LC_RPATH) -- the
                        # native object's identical externs must bind the
                        # same dylibs instead of faulting in dyld
                        *reflibs,
                        *rpath_args,
                        '-o', str(exe), str(obj), *map(str, runtime), '-lSystem'])
                        if rc != 0:
                            status = 'native_link_failed'
                        else:
                            nrc, nout = run('native_run', [str(exe)])
                            if nrc != rrc:
                                status = 'exit_mismatch'
                            elif normalized(nout) != normalized(rout):
                                status = 'output_mismatch'
                            elif rrc != 0:
                                status = 'matching_nonzero_exit'
                            else:
                                status = 'pass'
            record['status'] = status
            counts[status] += 1
            results.write(json.dumps(record) + '\n')
            results.flush()
            print(status, src.stem, flush=True)
    (work / 'summary.json').write_text(json.dumps(dict(counts), indent=2) + '\n')
    print(json.dumps(dict(counts), sort_keys=True))
    return int(any(status not in ('pass', 'matching_nonzero_exit') for status in counts))


if __name__ == '__main__':
    raise SystemExit(main())
