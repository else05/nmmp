# Large-short calibration exceeded the driver's watchdog budget

The first eight scenarios completed with all prescribed samples. Their original
logs, ledgers, hashes and summaries remain unchanged.

For largeShort, the 10,000-call calibration yielded approximately 4.31 seconds
for A, 4.41 seconds for B, 9.2 ms for C and 9.3 ms for D. The frozen fastest-target
rule selected 1,610,081 identical iterations. Linear projections from the measured
calibration are A 694.71 s, B 712.47 s, C 1.50 s and D 1.54 s per round. The driver
has a 600-second no-output watchdog, so normal old-mode rounds exceed its budget.
The full 3-seed scenario would take approximately 35 hours at these rates.

Before accepting any largeShort measured result, the coordinator, collector and
current ADB client were identified by exact command lines and stopped. The
confirmed host PIDs were 24304, 29096 and 6080. This interrupted a warmup; no
largeShort measurement is accepted. The ADB server and business app were not
stopped. A subsequent process check found no remaining benchmark process.

This is a sampling-budget/driver limitation, not an application correctness
failure or a speed-based exclusion. A user preference question is pending about
strict prolonged sampling versus explicitly shorter, nonconforming-duration
diagnostic samples. Remaining ordinary scenarios in seed2/seed3 can proceed
independently; the largeShort calibration and interrupted log remain evidence.
