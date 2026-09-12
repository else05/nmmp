# Isolated word key-block experiment

Prepared 2026-09-13. This is hypothesis 2 only, not a formal timing artifact.

## Exact input and change

- Source: `../../s5/seed1/hotspot-C/build/dex2c` (relative to this directory).
- Experiment: `build/dex2c`.
- All 73 source files were copied. Only `vm/include/VmReader.h` differs.
- The other 72 files, including generated method bodies, DEX files, module blob,
  opcode tables, root program, CMake options and remaining native runtime, are byte-identical.
- `generated/classes_native_functions.c` retains exactly eight `vmExecuteToken` calls.
  No benchmark body, native signature, input, wrapper or registration changes were made.
- `opt-word.patch` is the complete generated-source change, with paths relative to dex2c.
- `source-manifest.json` contains both complete SHA-256 inventories and the patch hash.
- Preparation script: `../prepare-opt-word.py`; deliberately rejects an existing destination.

## Hypothesis and implementation

If two byte-level decode calls in `word()` cause redundant cache tag/valid-bit work,
using one key-block lookup for their shared block may reduce execution cost. It does
not reduce the number of necessary mix operations on domain/window changes.

`keyBlock(pos, domain)` extracts the existing window/tag/valid-bit/mix logic without
changing it. Byte `decode()` uses that helper and retains its plaintext-mode bypass.
`word()` retains its original negative-PC, null-pointer and complete two-byte range
check. It then obtains one block for encoded mode, shifts to the relevant 16 bits,
XORs low and high source bytes separately and combines them explicitly little-endian.
Plaintext mode uses a zero mask and does not touch the key cache.

Proof of the common block: after the unchanged bounds check, `pos = 2 * pc` is even.
Its offset modulo eight is 0, 2, 4 or 6; `pos + 1` therefore belongs to the same
8-byte block and the same 64-byte window. The largest shift is 48 bits, so extracting
16 key bits cannot cross the block or require a shift by 64. No unaligned u16 load
or pointer cast is introduced.

The existing 64-byte cache, domain/window replacement, lazy 8-byte fills, method
identity through per-reader seed/lifetime, destructor volatile wiping, boundary
checks and opcode-row translation are unchanged. Payload byte and tries operations
retain their public paths and behavior; payload16 naturally uses the optimized word.
No retained plaintext, extra cache, record-validation change or instrumentation is added.

## Host validation

Run from WSL with:

```sh
python3 /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness/opt-word-C/run-host.py
```

Six executable runs passed using host g++: original reader vectors, optimized reader
vectors, and the original/optimized differential test, each in Release (`-O2`) and
ASan+UBSan (`-O1`, leak detection and sanitizer failure enabled) configurations.
Compilation also uses `-Wall -Wextra -Werror`.

Each differential run passed 1,312,133 comparisons across 64 deterministic test seeds.
It checks returned values, failure flags, seed, cache domain/window/valid bits and all
64 cache bytes after public operations. It also checks independently encoded expected
word values and randomized opcode-row translation. Coverage includes all even block
alignments, 8/64-byte transitions, reverse and randomized access, all five reader
domains, odd-position tries reads, payload byte/word reads, truncated/odd code lengths,
invalid domains/operand prefixes, null inputs, negative/oversized PCs and plaintext mode.
The existing vector test additionally exercises LEB128 success and malformed encodings.
Reader object size and the 64-byte cache size are compile-time checked against the original.

Commands and outputs are retained in `host-tests.log`; structured results and hashes
are in `host-results.json`. Both generated-tree inventories were verified again after
testing. The production reader header and the frozen 0136 production/hotspot JARs
were also SHA-256 checked before and after testing and remained unchanged.

## Interpretation and handoff

Ready for a separately coordinated Android build from this experiment directory.
No Android build, ADB action or benchmark timing was performed during preparation.
The copied harness still has the original eight methods; these host tests validate
reader behavior, not ART execution of the full eight-method/nine-case harness.

The compiler may already remove some duplicated checks or inline the helper
differently, especially with O-MVLL. Consequently semantic equality is demonstrated,
but no speedup, regression-gate result or bottleneck attribution is claimed. Compare
the isolated output against the unchanged seed1 C input with the same API26/ARM64,
O-MVLL settings and balanced timing procedure; do not substitute probe artifacts.
