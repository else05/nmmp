# Collector/report local recheck

Authoritative run: `run-20260912T195618-052891Z/` (UTC directory timestamp).

Result: 12/12 review tests passed, including 16 specifically rejected negative fixtures and the collector's existing 6/6 self-tests. Three actual startup ledgers pass: each has 92 valid attempts, 20 measured attempts per variant, and 5 PSS samples per variant. All 99 watched original source/evidence files have unchanged SHA256 values across the test run.

Reproduce from the repository root:

```powershell
python -B nmm-protect/build/on-demand-20260912/s5-harness/report-review/verify_report.py
```

Each invocation creates a new run directory. The script snapshots the two reviewed sources and its own test source; it preserves real-derived startup fixtures, a complete synthetic nine-case hotspot fixture, all mutation fixtures, test logs, rejection messages, fake transport transcripts, and hash manifests. Startup fixture ancestry continues to reference the preserved original seed2 files; `source-and-evidence-sha256.json` identifies those inputs.

Verified rejection coverage:

- Deleted interrupted attempt from a resumed ledger; changed resume reason/device protocol; wrong known loader ID; zero measured startup time.
- The original internally consistent one-case/zero-warmup/two-measurement counterexample; missing scenarios; changed warmups, rounds, or ABCD order; wrong startup warmups.
- Uniformly changed ledger checksums while raw logs remain unchanged; raw log omissions, round changes, elapsed-time changes, and checksum changes. The full 9-case, 1080-record synthetic positive control passes, including batch-1 measured round adjustment and unchanged negative warmup indices.

CLI/import checks:

- Both production modules import with no CLI arguments, no process invocation, and no file writes. `-B` prevents bytecode writes.
- The actual `__main__` path runs using a mocked `subprocess.run`: two fresh plans with different package/serial/ADB settings, normal A/B then B/A order, installed hash checks, PID/window/resumed checks, epoch filtering of a historical crash, loader READY, memory output and final summary.
- A real seed2 prefix is preserved and 40 remaining attempts are simulated; the output contains 92 valid attempts plus the retained invalid attempt. Installation failure writes an invalid ledger row and produces no summary.
- Compiled closure metadata confirms `command -> adb`, `shell -> command`, `drawn -> package`; the log filtering generator captures the current `sample`. See `closure-inspection.json`. No new closure/CLI regression was found.

These are report/collector tests. All CLI transport responses and all hotspot samples are synthetic, and are **not** ART, device, or performance results. An audit hook rejects subprocess creation, shell execution, network connections, and file writes outside the current review run. No ADB command or external process was executed by the test script. Production scripts and original sampling data were not edited.

Historical fixture development results are retained:

- `run-20260912T195417-553563Z`: one assertion failure used seed1's historical plan, which has no expected `loaderId`; that fixture cannot demand exact-ID rejection. One CLI fixture also incorrectly assigned a loader ID to loader-OFF A.
- `run-20260912T195448-111813Z`: the fake transport could not distinguish A/B because both fixture variants had the same APK path. Corrected by providing distinct local fixture paths.
- `run-20260912T195507-690506Z`: all tests passed. The final run additionally preserves the original shortened-protocol counterexample with matching raw logs/ledger/summary and records closure metadata automatically.

Exact-ID rejection now deliberately uses seed2's known expected ID and a row after its frozen prefix, so the failure comes from loader validation rather than prefix mismatch. Historical seed1 can only validate READY presence without a separately supplied trusted expected loader ID; this is an evidence limitation, not a new failure introduced by `main()` wrapping.

Reviewed source SHA256:

- `measure-startup.py`: `ecfb7b4b5b4f371ff3bb02bdb5abeaa0136d19eaf572b8f362efd558bca9936f`
- `summarize-enhancement.py`: `bcaee4480ebb795705716ccfe58846adce44405f9c6730be731dee5cdaa2e324`
