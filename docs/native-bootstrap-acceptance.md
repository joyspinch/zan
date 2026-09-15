# Native bootstrap acceptance

## Verified checkpoint

On macOS arm64, the compiler reached a native object fixed point without LLVM,
clang, or project C runtime objects in the generation chain.

- Artifact directory: `build/native-bootstrap/run.nYdDdi/`
- `stage2.o` and `stage3.o` compare byte-for-byte equal.
- SHA-256 for both objects: `4160ae57f77bb684a8b72590e818b1bf947707172853091934800d2551d0abd8`
- `otool -L stage2` reports only `/usr/lib/libSystem.B.dylib`.
- `RT_OBJS` was empty. The linker was `/usr/bin/ld`.
- Native-born `stage2` passed the full default regression set
  (27 kernels plus Dictionary, extern, List, lexical, frame, dict-out-address,
  numeric, string, host-args, generic-overload and float-ABI fixtures:
  **39/39 exact goldens**) and 15 extra integration fixtures (user-defined
  conversions, delegates/lambdas, explicit layout, raw C-string reads, packed
  `int[]` TryGetValue out writes, int keys, Keys/Values chains, and integer
  narrowing boundaries incl. field initializers): **15/15**.
- `host_intrinsics` matches its golden when run with `alpha beta` in a fresh
  working directory (the batch runner passes no arguments by design).
- The seed used to start this checkpoint was built by the old C compiler. The
  resulting native stage2 can now be used as the seed instead. The old compiler
  remains a parity reference; it is not needed to execute the native compiler.
- New in this checkpoint: `var` local and foreach item type inference,
  Dictionary operand/type validation, `d.Keys`/`d.Values` element typing with
  chained `.Count`, width-correct TryGetValue writes into packed 4-byte array
  elements (miss leaves the destination unchanged), reference-exact
  `Remove`/`ContainsKey`/`TryGetValue` return-type modeling, and int32
  narrowing at every assignment boundary (locals, args, returns, array/List
  element stores, static/instance field initializers; `int[]` reads use
  ldrsw sign extension).
- New in this checkpoint: parser/language core -- multi-declarator fields,
  locals, statics and for-statements (`int a, b = 7, c;`), `do {} while();`,
  prefix `++x`/`--x` (new-value semantics), `>>>` and `>>>=` (always-logical
  64-bit shift; an int target narrows at the store, matching the reference),
  octal `0o17L` radix literals with suffixes, and scientific-notation float
  literals (`1e21`, `1.5E-3`).
- New in this checkpoint: codegen/runtime -- `++`/`--` on members, statics,
  fields and array elements through single-evaluation addresses, compound
  assignment single-evaluation of effectful targets via hidden frame slots
  (`box.data[Next()] += 5` calls `Next()` once), `Dictionary<string,double>`
  values (bit-pattern slots + ConvI2D on store), `File.Exists`/`File.Delete`/
  `File.WriteAllText` on the libc surface (access/remove/fopen/fwrite), and
  `double.TryParse(text, out v)` with endptr-based success.
- Full parity sweep on the previous checkpoint: 132/549 pass (baseline
  recorded in docs/native-parity-baseline.json).

The source hashes recorded in `run.nYdDdi/sources.sha256` define this checkpoint.
Later workspace changes must be verified again; the checkpoint does not certify
unrelated edits or full language compatibility.

## What was repaired

- Dictionary search ABI, loop-state preservation, capacity growth, index return,
  condition encoding, TryGetValue load addressing and miss semantics.
- Lexical declaration slots, nested scopes, instance-method lookup priority,
  out argument address evaluation and large-frame FP addressing.
- Native extern signatures and fixed integer/pointer C ABI calls.
- File, argv, executable-directory, glob and StringBuilder runtime emitters.
- Native floating formatting/parsing and soft/hard guard runtime emitters,
  replacing the project C helpers with libSystem calls.
- String Replace/IndexOf/store support and source literal escape decoding.
- Interface default method flags, ternary floating promotion, float cast class,
  recursive generic type names and open generic static initializer suppression.

## Reproduction

```sh
SEED="$PWD/build/native-bootstrap/run.nYdDdi/stage2" RT_OBJS='' \
  bash scripts/native_bootstrap.sh
python3 scripts/native_regression.py \
  --seed "$PWD/build/native-bootstrap/run.nYdDdi/stage2" --runtime ''
```

The source manifest is `scripts/selfhost_sources.txt`. Each bootstrap run uses a
fresh artifact directory and records seed/source/runtime hashes. Existing seed
artifacts are never deleted by the script.

## Compatibility boundary

A fixed point proves reproducible self-compilation, not full C-host parity.
The stage-boundary full parity sweep uses `scripts/native_parity.py --all` and
records failed reference builds separately, saves stderr, and distinguishes
matching nonzero exits from successful execution. Advanced language/runtime
families still require implementation and regression work. Do not retire the
C reference compiler or delete the LLVM emitter based on this checkpoint alone.

System libSystem and the system linker remain platform dependencies. “No project
C runtime objects” is not a claim that the operating system contains no C code.
