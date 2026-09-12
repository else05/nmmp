# S5 baseline randomness patch

Base: 14bc959. Snapshot: baseline-14bc959/. Exact patch: s5-baseline-randomness.patch.

Only three existing Java sources changed: ProtectionContext, RandomInstructionRewriter,
and CmakeUtils. GeneratorRandom.java is added identically to current production source.
No baseline C/C++, loader, codec, template, method conversion, or runtime behavior is patched.
The original opcode/layout selection algorithms are retained; each now receives one RNG.

Settings: system property nmmp.testSeed overrides environment NMMP_TEST_SEED, including
an explicitly empty/invalid property (which fails). Exactly 16 ASCII hexadecimal digits
are required, case-insensitive; no prefix, sign, whitespace or implicit default seed.
With neither setting, each RNG is SecureRandom. Existing explicit-seed constructors retain
their original behavior and are not overridden by the setting.

Test streams: SHA-256 of UTF-8 "nmmp-test-random-v1", followed by the seed and identity as
two big-endian u64 values, followed by UTF-8 purpose. The first eight digest bytes, read
big-endian, seed java.util.Random (its standard 48-bit internal state). This is a test-only
deterministic stream, not a production cryptographic RNG.

Shared purposes: context, opcode, resolver-layout, jni-layout (identity zero).
Current-only purposes: demand (moduleId), native-root (buildId).
Bound contexts consume root then buildId from the context stream; unbound contexts consume
only root and retain buildId zero. Added consumers do not shift other purposes' streams.

Compatibility: Java 8 API/source compatible. Both tools must have separate JAR/template
directories so cached tools/vmsrc.zip cannot cross the baseline/current boundary.
This controls Java generator randomness only; Python loader key/nonce/build ID/stage0,
O-MVLL randomness and other toolchain variation are not claimed reproducible.

Validation: both actual root JARs produced identical context root/buildId/seedData, opcode/goto,
resolver layout and JNI layout outputs for all three fixed seeds. Raw output is in
seed-compatibility/current.txt and baseline.txt. All 121 baseline runtime/template files
were byte-compared against git archive 14bc959 and match. JAR hashes and test counts are in
s5-java-build-results.json. Current build log: s5-current-java-build-2.log; baseline build log:
s5-baseline-java-build.log. The initial Windows temporary-file cleanup test failure is retained
in s5-current-java-build.log; the test now defers deletion until JVM exit.
