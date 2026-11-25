# Fixed-game C bootstrap

`fixed-to-c.py` preserves the semantic approach used to bootstrap M2's C
methods: Clang resolves implicit receivers, inherited members, references,
default arguments, vector operators and casts; the tool then emits explicit
receiver C99 functions. It is an original MIT tool, not copied game code.
Generated functions carry the pinned OpenLara BSD notice.

This is a bootstrap aid for M3's `src/fixed/enemy.h` and `object.h`, **not** a
build step. The committed `game/*.c` files are maintained by hand. Do not
regenerate over reviewed game code. The tool does not supply object layouts,
constructors, virtual dispatch, native resource adapters, allocation, or
platform services. Those require a grounded manual port. Unsupported C++
syntax fails; it never emits a placeholder method.

Use the pinned oracle export (`test/oracle/build.sh`) as the compilation
context. Preprocess its `oracle.cpp` with the oracle's `-m32`, include path
and `-D__TR64_ORACLE__`, then dump the **same preprocessed file's** Clang AST:

```sh
rtk clang++ -m32 -std=c++11 -Wno-invalid-constexpr -Xclang -ast-dump=json -fsyntax-only $TR64_WORK/tmp/Game/oracle.pp.cpp > $TR64_WORK/tmp/Game/oracle.ast.json
rtk python3 tools/port/fixed-to-c.py --preprocessed $TR64_WORK/tmp/Game/oracle.pp.cpp --ast $TR64_WORK/tmp/Game/oracle.ast.json --class Enemy --method collide --source-ref src/fixed/enemy.h --out $TR64_WORK/tmp/Game/enemy-collide.c
```

Capture the AST command's stdout directly to the named scratch file; it can
be large, so delete superseded ASTs after use. The method selector accepts
multiple `--method` arguments. The AST and source must have identical bytes
and offsets. Keep all scratch outside the repository.

The bootstrap deliberately wraps uncertain signed 32-bit arithmetic and
left shifts through `uint32`. During the manual port, restore the original
named constants/macros and remove wrappers only where operand ranges prove
the arithmetic cannot overflow. Keep wrapping where it can, and never
reintroduce negative signed left shifts. Restore each omitted subsystem call
or stop with a named unsupported-operation error. Validate the resulting C
against unchanged oracle traces and the streamed placed-input regression
in `test/game/random-inputs.py`; a compiler success is not parity.
