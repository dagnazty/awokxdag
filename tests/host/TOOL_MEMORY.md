# Tool memory checks

The lifecycle change covers Fleet row buffers, Wi-Fi 6 Intel, BLE Intel,
Topology Map, and Deauth Forensics. Persistent scan results, saved networks,
shared wardrive BLE hits, and always-on Link/bridge transport queues retain
their existing ownership. No shared union is used: Fleet can remain a session
participant independently of an analysis tool.

`tool_memory.h` allocates only when a tool starts. Results prefer PSRAM and
fall back to internal RAM; radio callback queues use internal RAM exclusively.
There are no lazy allocations in `push`, `pop`, or `generation`. Start failures
release any partial allocation. Stop pauses producers, drains accepted hits,
attempts the existing SD export, and releases results and their String members.
An inactive export does not overwrite the last CSV with an empty table.
As with any failed SD export, observations are not saved if the card is absent
or the write fails; after stop these tables are no longer retained in RAM.

The previous C5 Touch map contains 34,432 bytes (33.625 KiB) across the converted
arrays. This is an estimate of formerly permanent storage, not a measured new
heap gain: the new owners have small pointer/lock/index overhead, and active
tools still allocate their storage. Compare the new map and board logs after
building. These changes do not prove the cause of a particular BLE init failure.

## Host tests

Run explicitly when compilation is allowed:

```sh
bash tests/host/run_tool_memory.sh
```

This compiles the actual buffer/queue header with mock heap capabilities and
mutex-backed critical sections, using ASan/UBSan. It covers constructors and
destructors, PSRAM fallback, allocation failure/unwind, ring wrap/full behavior,
draining after pause, late callbacks, and two producers racing stop/restart.
It does not validate ESP-IDF radio behavior or the firmware's integration.
The test was added without compiling it, per the no-build instruction.

## Board checks after the user's firmware build

1. Record `ready`, `before BLE init`, and `after BLE init`/`BLE init failed`
   memory lines on C5. Start ordinary wardriving and verify both Wi-Fi and BLE
   observations with a GPS fix. Keep the full initialization error if BLE fails.
2. Start/stop each analysis tool repeatedly with radio traffic present, then
   start wardriving. Check the `buffers released` memory lines for growing loss;
   check the saved CSV before leaving each tool. Repeat with SD unavailable.
3. Check Fleet coordinator, Wi-Fi worker, and BLE worker; stop/restart a drive,
   leave/rejoin, and change worker roles through the roster. Verify coordinator
   aggregation and BLE observations. The inactive role's ring should be absent.
4. Preview/download a wardrive CSV above 100 KiB over website Bluetooth and
   compare all bytes and the two header lines. The existing Node download
   regressions remain runnable without compiling firmware.
