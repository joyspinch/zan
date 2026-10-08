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
        ROOT / 'tests/selfhost/native_generic_overload_fit.zan', ROOT / 'tests/selfhost/native_float_return_arg.zan',
        ROOT / 'tests/selfhost/native_float_shapes.zan',
        ROOT / 'tests/selfhost/native_varargs.zan', ROOT / 'tests/selfhost/native_varargs_elf.zan',
        ROOT / 'tests/selfhost/native_fnptr.zan', ROOT / 'tests/selfhost/native_sync.zan',
        ROOT / 'tests/selfhost/native_spans.zan', ROOT / 'tests/selfhost/native_memwide.zan',
        ROOT / 'tests/selfhost/native_digests.zan',
        ROOT / 'tests/selfhost/native_retarget.zan',
        # 第十九批:cstring 读面(getenv/strchr 头部扫描约定)进电池
        ROOT / 'tests/selfhost/native_cstring_read.zan',
        # 第二十批:原"排除席"三人转正——golden_native 驱动库解析 +
        # harness 复刻(_scratch/数据链)后三例原生稳过金档,与 darwin
        # 电池链路(runtime_core.o 单对象)3/3 实测兼容。
        ROOT.parent / 'zan-lang/tests/conformance/struct_arc_lifetime.zan',
        ROOT.parent / 'zan-lang/tests/conformance/dictionary_wide_values.zan',
        ROOT.parent / 'zan-lang/tests/conformance/int_format_boundaries.zan',
        # 第十八批:ELF 车道普查(55/71 绿)后收编的九席——async 族首次进
        # 电池(redis_client 崩溃所在引擎的常备门),外加 int/dict 面。
        ROOT / 'tests/selfhost/json_ignore_ok.zan',
        ROOT / 'tests/selfhost/native_async_basic.zan', ROOT / 'tests/selfhost/native_async_detach.zan',
        ROOT / 'tests/selfhost/native_async_locals.zan', ROOT / 'tests/selfhost/native_async_throw.zan',
        ROOT / 'tests/selfhost/native_dict_packed_out.zan', ROOT / 'tests/selfhost/native_int_fieldinit.zan',
        ROOT / 'tests/selfhost/native_int_narrow.zan', ROOT / 'tests/selfhost/native_int_narrow2.zan',
        # b45:socket 族收编——16 件 conformance fixture 拷入 selfhost
        # (zan-lang/tests/conformance 原件双车道 32/32 实证后固化;http/
        # redis 客户端 + 代理链 + 并发压力服务器 + TLS,自含 mock server,
        # 无外网依赖)。http_client_keepalive 与 redis_client 不收:seed
        # ngen_async 并发/this 捕获双车道可复现(writer_route.md b44)。
        ROOT / 'tests/selfhost/http_client_redirect.zan', ROOT / 'tests/selfhost/http_client_binary.zan',
        ROOT / 'tests/selfhost/http_client_timeout.zan', ROOT / 'tests/selfhost/http_client_cookies.zan',
        ROOT / 'tests/selfhost/redis_pool.zan', ROOT / 'tests/selfhost/redis_tls.zan',
        ROOT / 'tests/selfhost/http_forwarder_keepalive.zan', ROOT / 'tests/selfhost/http_forwarder_stream.zan',
        ROOT / 'tests/selfhost/http_forwarder_tunnel.zan', ROOT / 'tests/selfhost/http_server_stress.zan',
        ROOT / 'tests/selfhost/http_framing.zan', ROOT / 'tests/selfhost/http_parser_hardening.zan',
        ROOT / 'tests/selfhost/http_smuggling.zan', ROOT / 'tests/selfhost/http_upload_bytes.zan',
        ROOT / 'tests/selfhost/http_bytes_redirect.zan', ROOT / 'tests/selfhost/http_chunk_len_overflow.zan',
        # b46:fail-soft 诊断面(共识子集:出货 oracle 二进制早于 rt_timer.c
        # 的 ARC 头 scratch 重设计,探针只断言二进制与源码一致的面)。
        ROOT / 'tests/selfhost/softdiag.zan']
    failed = 0
    # b45:stdlib 的 TLS/DB 族经 DllImport 引 OpenSSL/unixODBC/libpq——
    # golden_native 链接时按前缀探测补库(见其 extra_libs),这里同样补齐,
    # 否则 socket 族 16 件在链接期未解析(EVP_*/SQLAllocHandle/PQ*)。
    extra_libs = []
    homebrew = Path('/Users/qq/.homebrew') if Path('/Users/qq/.homebrew/opt').exists() \
        else Path('/opt/homebrew')
    for prefix_dir in (homebrew / 'opt/openssl@3', homebrew / 'opt/openssl'):
        if (prefix_dir / 'lib/libssl.dylib').exists():
            extra_libs += ['-L' + str(prefix_dir / 'lib'), '-lssl', '-lcrypto']
            break
    for prefix_dir in (homebrew / 'opt/unixodbc', Path('/opt/homebrew/opt/unixodbc')):
        if (prefix_dir / 'lib/libodbc.dylib').exists():
            extra_libs += ['-L' + str(prefix_dir / 'lib'), '-lodbc']
            break
    for prefix_dir in (homebrew / 'opt/libpq', Path('/opt/homebrew/opt/libpq')):
        if (prefix_dir / 'lib/libpq.dylib').exists():
            extra_libs += ['-L' + str(prefix_dir / 'lib'), '-lpq']
            break
    for i, fixture in enumerate(fixtures):
        fixture = fixture.resolve()
        prefix = work / f'{i}-{fixture.stem}'
        commands = [
            [str(seed), str(prefix) + '.o', str(fixture)],
            ['/usr/bin/ld', '-arch', 'arm64', '-e', '_main', '-platform_version', 'macos', '11.0', version,
             '-syslibroot', sdk, '-L' + sdk + '/usr/lib', '-o', str(prefix), str(prefix) + '.o', *runtime,
             *extra_libs, '-lSystem'],
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
