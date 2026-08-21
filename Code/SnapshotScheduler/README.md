# Snapshot Scheduler

`SnapshotScheduler` is a small independent C++17 timer module. It owns the
interval and worker thread, then invokes a caller-provided refresh callback.
Snapshot construction, local storage, and public-chain publication remain
outside this module.

The control server starts one scheduler with a default interval of 120 seconds.
Set `SNAPSHOT_AUTO_REFRESH_INTERVAL_SECONDS` to change it for local testing.
A positive integer is required; invalid values use the 120-second default.

The scheduler invokes the callback immediately after startup and then waits for
the configured interval. `stop()` wakes the worker and joins it during server
shutdown. Exceptions from one callback cycle are contained so they do not stop
the control server.
