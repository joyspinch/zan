#!/usr/bin/env python3
"""Standalone macOS/arm64 guard parity test; only patches temporary copies.

Run: python3 tests/native_guard_test.py [--zanc /path/to/gen0] [--keep]
C is used only for the independent caller and the exact reference oracle.
The generated guard object links directly to libSystem, with no custom C RT.
"""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LANG = ROOT.parent / "zan-lang"


def run(args, **kw):
    return subprocess.run([str(x) for x in args], check=True, **kw)


def digest(paths):
    return {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


def normalize(data):
    return re.sub(rb"\d{4}-\d\d-\d\d \d\d:\d\d:\d\d soft runtime error:",
                  b"<timestamp> soft runtime error:", data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--zanc", type=Path, default=LANG / "build/zanc")
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        parser.error("requires a real macOS arm64 host")
    watched = list((ROOT / "src/selfhost").glob("*.zan")) + list((ROOT / "scripts").glob("*"))
    watched = [p for p in watched if p.is_file()]
    before = digest(watched)
    work = Path(tempfile.mkdtemp(prefix="zan-guard-"))
    print(f"temporary integration: {work}", flush=True)
    try:
        src = work / "src"
        shutil.copytree(ROOT / "src/selfhost", src)
        names = (ROOT / "scripts/selfhost_sources.txt").read_text().split()
        # Concurrent integration may list a partial not delivered yet. Only
        # include existing files; real references to missing hooks still fail.
        names = [n for n in names if (src / (n + ".zan")).is_file()]
        if "ngen_guard" not in names:
            names.append("ngen_guard")
        ngen = src / "ngen.zan"
        text = ngen.read_text()
        assert text.count("        EmitRtCore();") == 1
        if not any(re.search(r'^\s*EmitGuardRuntime\(\);', p.read_text(), re.M)
                   for p in src.glob("ngen*.zan")):
            text = text.replace("        EmitRtCore();", "        EmitRtCore();\n        EmitGuardRuntime();")
        ngen.write_text(text)
        changed = 0
        for p in src.glob("ngen*.zan"):
            text = p.read_text()
            # Replace actual calls, not the new partial's integration comments.
            count = len(re.findall(r'^\s*BLExtern\("_zan_rt_guard_fail3"\);', text, re.M))
            if count:
                text = text.replace('BLExtern("_zan_rt_guard_fail3");',
                                    'BLInternal("_zan_rt_guard_fail3");')
                p.write_text(text)
                changed += count
        assert changed or any('BLInternal("_zan_rt_guard_fail3");' in p.read_text()
                              for p in src.glob("ngen*.zan")), "no native guard callsites found"
        # The integer-only integration fixture needs neither double formatter.
        # Omit these two otherwise-unconditional core emitters IN THE COPY:
        # Mach-O currently groups the whole text as one dead-strip atom, so
        # unused double helpers would still demand the unrelated C dbl_str.
        objfile = src / "ngen_obj.zan"
        text = objfile.read_text()
        text = text.replace("        RtEmitDbl2Str();", "")
        text = text.replace("        RtEmitPrintDouble();", "")
        objfile.write_text(text)
        print(f"temporary hook: {changed} guard callsites now internal", flush=True)
        # One compiler supports both actual full native integration and an
        # isolated runtime object for direct ABI/oracle tests, with no dead
        # functions or unrelated native core C dependencies in that object.
        mainfile = src / "main.zan"
        text = mainfile.read_text()
        assert text.count("List<int> obj = ng.Gen(unit);") == 1
        mainfile.write_text(text.replace("List<int> obj = ng.Gen(unit);",
                                       "List<int> obj = ng.GuardTestGen(unit);"))
        extra = src / "guard_test_entry.zan"
        extra.write_text('''using System;
using System.Collections;
namespace Zan;
partial class Ngen {
    public List<int> GuardTestGen(Node unit) {
        if (srcFile.IndexOf("guard-runtime-only.zan") < 0) { return Gen(unit); }
        CollectClasses(unit);
        EmitGuardRuntime();
        Patch();
        return BuildMachO();
    }
}
''')
        compiler = work / "zanc-guard"
        env = dict(os.environ, ZAN_RT_HARD="1")
        run([args.zanc, *[src / (n + ".zan") for n in names], extra,
             "-o", compiler], cwd=work, env=env)
        fixture = work / "guard-runtime-only.zan"
        fixture.write_text("class Probe { static void Main() {} }\n")
        obj = work / "guard.o"
        run([compiler, obj, fixture], cwd=work, env=env)
        undef = subprocess.check_output(["nm", "-u", str(obj)], text=True)
        assert "zan_rt_" not in undef and "zan_guard_" not in undef, undef
        native = work / "native-probe"
        run(["clang", "-arch", "arm64", "-O2", ROOT / "tests/native_guard_driver.c", ROOT / "tests/native_guard_abi.S",
             obj, "-o", native], cwd=work)
        # Compile the exact relevant source slice, not a reimplementation.
        reference = (LANG / "src/runtime/rt_timer.c").read_text()
        start = reference.index("#define ZAN_SOFT_MAX_SITES 256")
        end = reference.index("typedef enum zan_timer_kind", start)
        oracle = work / "oracle.c"
        oracle.write_text('''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void timer_lock(void) { pthread_mutex_lock(&lock); }
static void timer_unlock(void) { pthread_mutex_unlock(&lock); }
''' + reference[start:end])
        ref = work / "reference-probe"
        run(["clang", "-arch", "arm64", "-O2", ROOT / "tests/native_guard_driver.c", ROOT / "tests/native_guard_abi.S",
             oracle, "-o", ref], cwd=work)
        base_env = {k: v for k, v in os.environ.items()
                    if k not in ("ZAN_RT_HARD", "ZAN_LOG_DIR", "ZAN_RUNTIME_ERRORS")}
        base_env["TZ"] = "UTC"
        cases = [("basic", {}, "default"), ("basic", {"ZAN_LOG_DIR": "custom"}, "custom"),
                 ("basic", {"ZAN_LOG_DIR": ""}, "empty-dir"),
                 ("basic", {"ZAN_LOG_DIR": "x" * 4096}, "overlong-dir"),
                 ("basic", {"ZAN_LOG_DIR": "x" * 4095}, "path-overflow"),
                 ("basic", {"ZAN_LOG_DIR": "missing/child"}, "no-recursive-mkdir"),
                 ("basic", {"ZAN_LOG_DIR": "blocked"}, "failed-open"),
                 ("basic", {"ZAN_RUNTIME_ERRORS": "hard"}, "ignored-runtime-errors")]
        for val in ("", "0", "00", "0hard", "1", "soft", "false", "hard"):
            cases.append(("basic", {"ZAN_RT_HARD": val}, "env-" + (val or "empty")))
        for mode in ("strict", "cached-strict", "cached-env", "identity", "overflow", "long",
                     "lengths", "threads", "threads-distinct", "logdir-cache", "chdir", "abi"):
            cases.append((mode, {}, mode))
        cases += [("strict", {"ZAN_RT_HARD": "0"}, "strict-opt-out"),
                  ("strict", {"ZAN_RT_HARD": ""}, "strict-empty-opt-out"),
                  ("cached-hard", {"ZAN_RT_HARD": "1"}, "cached-hard")]
        for mode, settings, name in cases:
            observations = []
            for binary, side in ((native, "native"), (ref, "reference")):
                cwd = work / name / side
                cwd.mkdir(parents=True)
                (cwd / "second").mkdir()
                (cwd / "blocked").write_text("not a directory")
                result = subprocess.run([binary, mode], cwd=cwd, env=base_env | settings,
                                        capture_output=True, timeout=30)
                logs = {str(p.relative_to(cwd)): normalize(p.read_bytes())
                        for p in cwd.rglob("*.log")}
                # Concurrent distinct sites have nondeterministic arrival order.
                stderr = result.stderr
                if mode == "threads-distinct":
                    stderr = b"\n".join(sorted(stderr.splitlines()))
                    # Reference header creation is explicitly best-effort:
                    # simultaneous fopen/ftell may emit it more than once.
                    # Compare report multiset, while requiring a valid header.
                    for v in logs.values():
                        assert b"==== ZAN RUNTIME (soft) ====\nexe=<unknown>\n" in v
                    logs = {p: b"\n".join(sorted(line for line in v.splitlines()
                                                if b"soft runtime error:" in line))
                            for p, v in logs.items()}
                observations.append((result.returncode, result.stdout, stderr, logs))
            assert observations[0] == observations[1], (name, observations)
            code, stdout, stderr, logs = observations[0]
            assert b"atexit\n" in stdout, name
            if code == 70:
                assert not stderr and not logs and b"continued" not in stdout, name
            else:
                assert code == 0 and b"continued\n" in stdout, name
            if mode == "overflow":
                assert stderr.count(b"runtime error:") == 260
            if mode == "threads":
                assert stderr.count(b"runtime error:") == 1
            if mode == "identity":
                assert stderr.count(b"runtime error:") == 5
            if mode == "long":
                assert len(stderr) == 2798
            print(f"PASS parity {name}", flush=True)
        # Append across processes: one header, two independently deduplicated notes.
        cwd = work / "append"
        cwd.mkdir()
        for _ in range(2):
            result = subprocess.run([native], cwd=cwd, env=base_env, capture_output=True, timeout=30)
            assert result.returncode == 0
        data = next(cwd.rglob("*.log")).read_bytes()
        assert data.count(b"==== ZAN RUNTIME (soft) ====") == 1
        assert data.count(b"soft runtime error:") == 2
        print("PASS cross-process append/header", flush=True)
        # Real Zan guard callsites, with the unrelated unconditional double
        # emitters omitted from the temporary core as described above.
        fixture = work / "integration.zan"
        fixture.write_text('''using System;
class GuardIntegration {
    static int Divide(int value) { return 42 / value; }
    static void Main() {
        int i = 0;
        while (i < 3) { Console.WriteLine(Divide(0)); i = i + 1; }
        Console.WriteLine(123);
    }
}
''')
        obj = work / "integration.o"
        run([compiler, obj, fixture], cwd=work, env=env)
        full = work / "integration"
        run(["clang", obj, "-Wl,-dead_strip", "-o", full], cwd=work)
        result = subprocess.run([full], cwd=work, env=base_env, capture_output=True, timeout=30)
        assert result.returncode == 0 and result.stdout == b"42\n42\n42\n123\n", result
        assert result.stderr.count(b"runtime error:") == 1, result
        result = subprocess.run([full], cwd=work, env=base_env | {"ZAN_RT_HARD": "1"},
                                capture_output=True, timeout=30)
        assert result.returncode == 70 and result.stderr == b"", result
        print("PASS actual Zan internal guard callsites, libSystem-only link", flush=True)
        print(f"PASS {len(cases)} oracle cases; no existing source/scripts changed", flush=True)
    finally:
        assert digest(watched) == before, "existing files changed during test"
        if args.keep:
            print(f"kept artifacts: {work}")
        else:
            shutil.rmtree(work)


if __name__ == "__main__":
    main()
