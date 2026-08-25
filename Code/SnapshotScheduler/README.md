# Snapshot Scheduler

`SnapshotScheduler` is a small independent C++17 timer module. It owns the
worker thread, waits for a caller-provided next due time, and invokes a refresh
callback. Snapshot construction, local storage, and public-chain publication
remain outside this module.

The control server supplies the next due time from SnapshotStorage. Product-level
refresh periods are managed per product and default to 3600 seconds (1 hour).
SnapshotStorage also supplies each batch's availability window: the next wait
can end at the listing time, and no refresh is scheduled after the delisting
time.
The scheduler can also be woken explicitly when a route, block, publication, or
schedule change occurs, so it does not depend on an arbitrary global polling
period.

The scheduler invokes the callback immediately after startup and then waits for
the next due time. `wake()` interrupts that wait so the next due time can be
recalculated. `stop()` wakes the worker and joins it during server shutdown.
Exceptions from one callback cycle are contained so they do not stop the control
server.
