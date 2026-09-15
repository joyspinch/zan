#!/usr/bin/env python3
"""Compile native fixtures, link with system ld, and compare exact golden output."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed', required=True, type=Path)
    parser.add_argument('--runtime', default=os.environ.get('RT_OBJS', ''))
    parser.add_argument('--expected', type=Path, help='Override golden for a single explicit fixture')
    parser.add_argument('fixtures', nargs='*', type=Path)
    args = parser.parse_args()
    if args.expected and len(args.fixtures) != 1:
        parser.error('--expected requires exactly one explicit fixture')
    seed = args.seed.resolve()
    runtime = [str(Path(p).resolve()) for p in shlex.split(args.runtime)]
    sdk = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
    version = subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-version'], text=True).strip()
    work = Path(tempfile.mkdtemp(prefix='zan-native-regression-'))
    fixtures = args.fixtures or sorted((ROOT / 'tests/selfhost').glob('kernel*.zan')) + [
        ROOT / 'tests/selfhost/dict_minimal.zan', ROOT / 'tests/selfhost/dict_growth.zan',
        ROOT / 'tests/selfhost/native_extern.zan', ROOT / 'tests/selfhost/list_string_search.zan',
        ROOT / 'tests/selfhost/native_lexical.zan', ROOT / 'tests/selfhost/native_local_frame.zan',
        ROOT / 'tests/selfhost/native_dict_out_address.zan', ROOT / 'tests/selfhost/native_numeric_runtime.zan',
        ROOT / 'tests/selfhost/native_string_ops.zan', ROOT / 'tests/selfhost/host_args_bounds.zan',
        ROOT / 'tests/selfhost/native_generic_overload_fit.zan', ROOT / 'tests/selfhost/native_float_return_arg.zan']
    failed = 0
    for i, fixture in enumerate(fixtures):
        fixture = fixture.resolve()
        prefix = work / f'{i}-{fixture.stem}'
        commands = [
            [str(seed), str(prefix) + '.o', str(fixture)],
            ['/usr/bin/ld', '-arch', 'arm64', '-e', '_main', '-platform_version', 'macos', '11.0', version,
             '-syslibroot', sdk, '-L' + sdk + '/usr/lib', '-o', str(prefix), str(prefix) + '.o', *runtime, '-lSystem'],
            [str(prefix)],
        ]
        try:
            for phase, cmd in zip(('compile', 'link', 'run'), commands):
                result = subprocess.run(cmd, cwd=ROOT, capture_output=True, timeout=60)
                Path(str(prefix) + '.' + phase + '.stdout').write_bytes(result.stdout)
                Path(str(prefix) + '.' + phase + '.stderr').write_bytes(result.stderr)
                if result.returncode:
                    raise RuntimeError(f'{phase} exit {result.returncode}: {result.stderr.decode(errors="replace")[:1000]}')
            expected = (args.expected or fixture.with_suffix('.out')).read_bytes()
            if result.stdout != expected:
                raise RuntimeError(f'output mismatch: expected {expected!r}, got {result.stdout!r}')
            print('PASS', fixture.stem, flush=True)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            failed += 1
            print('FAIL', fixture.stem, exc, flush=True)
    print(f'{len(fixtures) - failed}/{len(fixtures)} passed; logs: {work}')
    return int(failed != 0)


if __name__ == '__main__':
    raise SystemExit(main())
