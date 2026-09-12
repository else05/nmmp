# Seed3 startup connection interruption

Seven complete valid warmup attempts precede A round 1. That attempt reached
ThisTime 2294 ms / PID 25413 / the drawn main window, but the required activity
check could not connect to the local ADB server. The incomplete sample is invalid
and will be repeated; its timing is not included in the gate.

On recheck the server was available at the existing PID 5972 (start time Sep 9),
with SDK adb 37.0.0-14910828 and the same device. Thus the client error does not
establish that the server process died or restarted. No server kill/restart was
requested. The transient connection failure's cause is unresolved.

Read-only follow-up confirms the same app PID, resumed MainActivity and drawn,
visible window. Epoch process logs contain no current crash marker. This does
not retroactively validate the incomplete sample. Follow-up dumpsys commands
initially omitted `shell`, produced no device query, and were corrected before
saving the actual window/activity evidence.

Resume preserves the full ledger and repeats A round 1 with the same APK hashes,
device, protocol, fixed seed and variant order. No app data is cleared.
