# Deferred/combined report review

Local-only verification of current source snapshots; no ADB or external process. Existing source and evidence hashes remained unchanged. See `results.json`, `input-sha256.json`, saved source copies and `verify_combined.py`.

Verified:

- A complete synthetic nine-case fixture split into ordinary eight plus largeShort combines without replacing ordinary results. With `largeShortIterations` present, largeShort is duration-limited and `durationProtocolMet=false`, even when every synthetic round exceeds one second. Overall performance remains false.
- Separate API, ABI, runner hash, library hash and serial mismatches all reject. Existing overlapping largeShort results reject rather than being replaced.
- The exact collector scheduling AST was executed with a fake `run` and no device setup. Deferring a middle and last case preserves original case-index rotation and every remaining case's iterations/order. The fixed-iteration field affects only largeShort.
- Calibration-based timeout calculations produced the expected 2250s, 15000s and 600s floor in synthetic cases. This verifies arithmetic and call parameters; it does not wait for real watchdog expiration.
- Missing independent startup batches prevent completion/performance pass.

One material finding remains at `summarize-enhancement.py:200` and `:212`: `startupIndependent` is validated internally but not against its original seed batch. Copying the original three batches into the independent directories yields `startupNoiseReviewComplete=true`. Changing the copied seed1 A hash in both plan and records, plus changing its device API, still yields true. Fixtures and aggregate JSON outputs preserve both reproductions. The existing diagnostic duration gate still prevents overall performance pass in these examples.

Minimal fix: compare the independent batch's frozen protocol/APK identities and device identity with the original batch; verify that its root batch and accepted launches are distinct, including resumed ancestry. Resuming an interrupted independent batch is valid; reusing the original batch is not. Do not infer independence from a directory name. Once a future formal-duration hotspot run replaces the diagnostic gap through an explicit reporting decision, this missing check must not permit reused startup evidence to pass noise review.

`complete` for combined hotspots currently means all nine scenarios have data, not that duration protocol passed. Keep reporting the separate duration flag. The fixed 10000-iteration diagnostic is not user approval of a runtime budget and does not relax the 10% gate.

Scope: aggregate tests use actual startup parsing on isolated copies; only `combined_hotspots` is stubbed there with the separately validated diagnostic result. No formal hotspot data was read or modified. These checks are not device or performance measurements.
