# Pure S1 stage comparator

Base commit: `18dc5b1799e1177f0f5a2a1488fa8f2917d64696` (18dc5b1).
The complete 934-file commit was exported with git archive to `s1-stage-snapshot.tar`
and extracted into `s1-stage-snapshot/`. No current runtime files were copied into it.

Only three existing Java sources changed: ProtectionContext, RandomInstructionRewriter
and CmakeUtils. The shared GeneratorRandom.java was added byte-identically to the
current helper. The changes are equivalent to `s5-baseline-randomness.patch`, applied
against S1 context; all S1 differences remain intact. Git's Windows autocrlf conversion
was normalized back to each original source's newline convention before recording
the exact S1 diff. No C/C++, API-level source, codec, loader, or template edits occurred.

Exact patches:

- Existing baseline14bc959 patch: `s5-baseline-randomness.patch`, unchanged, SHA256
  `a62091e731150f27b090dcbe064d7d83981cb7d3ac9ea2f61782b527c0357b41`.
- S1 patch: `s1-stage-randomness.patch`; reverse applicability checked against the snapshot.
- Full archive/source/patch SHA-256 inventories: `s1-stage-source-manifest.json`.

All 122 native runtime/template source files remain byte-equal to the S1 archive.
The embedded vmsrc.zip also remains byte-equal to the committed S1 resource; it was
not regenerated. Some zip source entries use CRLF while git source entries use LF;
the runtime content was compared with that difference accounted for. The packaged
CMake files are correctly compared with S1 mksrc templates, not the Android test
project's different CMake files.

## Randomness compatibility

Property `nmmp.testSeed` takes precedence over environment `NMMP_TEST_SEED`; an
explicit invalid property fails rather than falling back. Exactly 16 ASCII hex digits
are required. With neither setting, GeneratorRandom uses SecureRandom. Purpose scopes
remain context, opcode, resolver-layout and jni-layout; the helper is shared with the
previously tested production/baseline implementation.

The actual S1 JAR passed SeedCompatibility for all three seeds. Context root/buildId/
seedData, opcode/goto and resolver/JNI layout results exactly match both frozen JARs.
Raw output: `s5-harness/stage-S1/seed-compatibility/s1.txt`.
This does not claim deterministic O-MVLL output or control Python loader randomness.

## Java build

Windows Corretto17: `E:/Scoop/apps/corretto17-jdk/current`.
Gradle cache: `D:/Gradle/.gradle`.
Working directory: `s1-stage-snapshot/nmm-protect`.
Command: `gradlew.bat --offline --no-daemon --max-workers=5 test jar`.
Result: exit 0, apkprotect 32/32 and arsc 16/16 tests passed.
Log: `s1-stage-java-build.log`. The existing Gradle implicit jar dependency warning
was retained in the log; no build-file changes were made.

Built JAR: `s1-stage-snapshot/nmm-protect/build/libs/vm-protect-2026-09-13-0244.jar`.
Isolated copy: `s5-harness/stage-S1/vm-protect.jar`.
SHA256: `4a35f3eface61c248c0d5ccd298cbb3c9a6fd2c5722e473e46c9d5606650b5f6`.

## Android harness

The current production-API GenerateBench.java was copied unchanged into stage-S1 and
compiled against the S1 JAR with Windows JDK17. Its eight-method assertion passed.
Input remains `s5-harness/impl-dex/classes.dex`; input and source hashes are recorded.
The separate stage-S1/tools cache contains only this S1 JAR's original template.

Exact invocation/environment: `s5-harness/stage-S1/build-harness.sh`.
It calls GenerateBench with seed `0123456789abcdef`, API26, ARM64, Release, private OFF,
NDK26.3.11579264 and O-MVLL1.8 using the repository `scripts/wsl/omvll-config.py`.
Build completed with exit 0. All compile commands were checked for the ARM64 API26
target and O-MVLL plugin. No current-legacy or later init/native-VM runtime is substituted.

49 generated template files remain byte-identical to the S1 zip. Only the six expected
production generator outputs differ: opcode header, JNI/resolver layouts, codec config
and two CMake files. In particular VmReader.h, InterpC-portable.cpp, Codec.cpp and
VmCodec.cpp remain byte-identical to the archived S1 template. There are exactly eight
vmExecute calls and no vmExecuteToken calls in the generated wrappers.

Stripped library: `s5-harness/stage-S1/build/obj/strip/arm64-v8a/libc++_en.so`.
Size: 394656 bytes. SHA256:
`b7c821c5447b0aedaead2cd665a47550d573662140bbec595848da9311285483`.
Unstripped companion is under `build/obj/sym/arm64-v8a/`.
Log: `s5-harness/stage-S1/build.log`.
Complete build/input/generated-source/library metadata: `s1-stage-build-results.json`
and identical `s5-harness/stage-S1/manifest.json`.

Snapshot source hashes and both frozen matrix JAR hashes were rechecked after all
builds. No production file edits or commits were made. No ADB, ART execution or
performance sampling was performed; this is a ready comparator fixture, not a device
correctness or performance result.
