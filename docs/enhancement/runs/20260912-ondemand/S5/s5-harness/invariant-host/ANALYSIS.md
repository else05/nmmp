# Isolated per-call invariant experiment

Prepared 2026-09-13. The generated source copies are frozen and ready for the main
thread's separately coordinated API26/O-MVLL build and ART verification.

## Artifacts and one-variable scope

Paths below are relative to `s5-harness/`:

| Variant | Original | Isolated generated copy | Evidence |
| --- | --- | --- | --- |
| L | `current-legacy/build/dex2c` | `opt-invariant-L/build/dex2c` | `opt-invariant-L/invariant.patch`, `opt-invariant-L/manifest.json` |
| C | `../s5/seed1/hotspot-C/build/dex2c` | `opt-invariant-C/build/dex2c` | `opt-invariant-C/invariant.patch`, `opt-invariant-C/manifest.json` |

L copied 72 files; C copied 73. Each copy changes only `vm/InterpC-portable.cpp`.
All other files, including the eight generated native methods, module/codec metadata,
opcode permutation, CMake files and VmReader.h, remain byte-identical to their own
source. This experiment does not use the opt-word copy and contains no probe hooks.
The preparation script is `prepare-opt-invariant.py`. It rejects existing targets.
The complete unified patches pass reverse `git apply --check`; manifests contain
every original/copied file's SHA-256 and patch hash.

At vmInterpretReader entry the only new state is:

```cpp
const bool checkedRegisters = code->reader != nullptr;
const uint32_t registerCapacity = checkedRegisters ? code->registerCapacity : 0;
```

28 boolean reader uses and four capacity uses in the macros/body now use these locals.
Capacity remains conditionally read. A legacy caller does not need to initialize or
provide an accessible capacity field when its reader pointer is null. The original
short-circuit register bounds, wide-register width, object reference, invocation range,
unsupported-opcode and exception-adapter checks remain in place. The vmInterpret
adapter is byte-identical; actual reader pointer use and the `inputReader` reference
are unchanged. No checks or cleanup exits were removed.

## Invariance evidence and its scope

Evidence locations in the original generated trees (L/C share this interpreter):

- `vm/Codec.cpp:81`: legacy vmExecute constructs a local **const vmCode runtimeCode**,
  calls vmInterpret synchronously at line 90, then frees decoded spans after return.
  Registers/flags point to wrapper arrays, not the vmCode object. The reader is null.
- `vm/Module.cpp:215`: vmExecuteToken constructs a local **const vmCode frame** with
  the validated register capacity and local reader, then directly calls vmInterpretReader
  at line 217. No frame pointer is published into the module/context.
- `vm/Demand.cpp:87`: the separate S2 fixture entry also constructs a const local frame
  and synchronously calls vmInterpretReader at line 88. It verifies the supplied capacity
  against the method record before constructing the frame.
- `vm/InterpC-portable.cpp:1102`: the unchanged adapter reads the frame and passes it
  synchronously; it either uses its caller's reader or a new local legacy reader.
- `vm/InterpC-portable.cpp:1116`: vmInterpretReader takes `const vmCode *`. Inspection
  of every `code` occurrence found field reads, with no assignments, writable aliases,
  publication or callback argument carrying the vmCode address. JNI/resolver calls
  receive JNIEnv, IDs, objects and argument values, not this frame descriptor. Nested
  Java-to-native calls create their own descriptors and may modify their own registers.
- Repository development callers in `nmmvm/nmmvm/src/main/cpp/test.cpp`,
  `temp_functions.c`, and `src/test/semantic/SemanticBridge.cpp` likewise use local const
  aggregate descriptors. The module host test's vmInterpretReader is a stub, not another
  production caller. No legitimate in-call writes to these fields were found.

The proof is based on concrete ownership/call sites, not solely on a const pointer
parameter: an arbitrary external alias could otherwise mutate a non-const descriptor.
It does not cover future callers that intentionally change frame metadata mid-execution,
malformed out-of-bounds legacy execution, or debugger/memory corruption. Such behavior
cannot be treated as part of this experiment's valid lifecycle. Uninitialized legacy
capacity is explicitly handled by the conditional read above, not assumed away.

## Host semantic validation

Command (WSL, no Android toolchain or ADB):

```sh
python3 /mnt/e/OtherProject/safe-toolchain/nmmp/nmm-protect/build/on-demand-20260912/s5-harness/invariant-host/run-host.py
```

All eight executable runs passed with g++ `-O2` and real OpenJDK 25 JNI, using
`-Xcheck:jni`:

| Actual interpreter source | Existing semantic checks | Invariant checks |
| --- | ---: | ---: |
| L original | 1338 | 1616 |
| L optimized | 1338 | 1616 |
| C original | 1351 | 1616 |
| C optimized | 1351 | 1616 |

The existing suite covers arithmetic, branches, wide values, switch/array payloads,
caught/uncaught Java exceptions and object JNI. C additionally uses the original
Demand encoder/record fixture and rejection scenarios with its actual VmCodec/Demand
code; it is not a mock interpreter. The copied SemanticBridge is adapted only to assign
resolver callbacks by field name because the production generator randomizes layout;
`semantic-fixture.patch` records that host-test-only adaptation. No source in the
repository semantic suite or the generated runtime trees was changed for the tests.

InvariantMain changes checked mode/capacity between calls, checks scalar/wide bounds,
invalid object-reference flags and invoke-range overflow, performs JNI -> Java -> JNI
reentry with different inner frame modes, and runs 1200 checks across four threads.
A legacy fixture places the capacity field on a PROT_NONE page while earlier descriptor
fields remain readable. The complete descriptor is legally constructed before protecting
that page; successful execution proves the tested legacy path does not access capacity.
This is an inaccessible-field check, not a claim of MemorySanitizer coverage.

Host logging alone is stubbed in `include/android/log.h`; Java/JNI behavior is real.
The OpenJDK 25 native-access warning is retained in the log and is not a test failure.
Commands/output and structured results are in `host-tests.log` and `host-results.json`.
All source/copy file hashes matched their frozen manifests after host testing.

## Limits and performance hypothesis

No performance measurement, Android build, ADB action or ART validation was performed
by this worker. No production native files/JARs were edited. Host success does not
substitute for the main thread's API26/O-MVLL build and API27 device semantics.

The hypothesis is that explicit local values reduce repeated alias-sensitive metadata
loads. Compiler inlining/hoisting may already eliminate some loads, or extra live locals
may increase register pressure and spills; O-MVLL can change either outcome. The source
change therefore proves neither fewer ARM64 instructions nor a speedup. Compare each
optimized artifact only with its matching L/C input, with identical toolchain/config
and input, before attributing any measured change to these invariants.
