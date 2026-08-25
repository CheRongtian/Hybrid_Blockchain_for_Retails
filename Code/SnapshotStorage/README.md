# Snapshot Storage

`SnapshotStorage` is the local lifecycle layer for generated public Snapshots.
It is intentionally independent from the HTTP handlers and the PublicChain
Node service.

## Responsibilities

- SQLite stores Snapshot metadata, per-batch revisions, and lifecycle state.
- A small in-memory map caches the current `active` Snapshot for each batch.
- `snapshot_verification_status` stores the latest local verification time,
  status, and message without changing the published Snapshot.
- `snapshot_verification_history` appends every scheduled or manual verification
  result for audit without replacing earlier checks.
- Every preview and publication is also written to a local archive directory.
- A semantic route change marks that batch's `preview` and `active` records as
  `invalidated`.
- A successful publication marks the new record `active` and the previous
  active record `superseded`.

The archive is cold local storage. The SQLite table is the source of truth for
state; the memory cache is only a fast read path for the current active record.

By default, the control server stores the SQLite database in
`Storage/Database/` and the cold archive in `Storage/Snapshots/`. Historical
revisions stay available there for local review. An explicitly supplied custom
database path keeps its archive beside that database.

## Automatic refresh

The control server uses the independent `Code/SnapshotScheduler` module to wait
for the nearest due time stored in `snapshot_refresh_schedule`. The actual
refresh period is configured per product in the control panel and defaults to
3600 seconds (1 hour) when no product policy has been saved. The compact editor
inside `Consumer Data Preview` accepts whole minutes, hours, or days. Each batch
also has an `Available from` / `Available until` window, stored at whole-minute
UTC precision.

Each batch keeps its next due time in `snapshot_refresh_schedule`. Saving a
product policy resets the next due time for that product so the new period takes
effect without a page reload. A future window waits until its start time. At the
end time the batch stops refreshing and its final published Snapshot remains
available to customers as a frozen record. Publishing before the start time is
allowed, so the public record can be ready before the batch is listed.

Each scheduled cycle checks batches that already have a published or invalidated
Snapshot:

- unchanged source blocks and route fingerprints update only the hot verification
  status; no public-chain publication is created;
- changed source blocks create and publish a new immutable Snapshot revision;
- a completed route after an invalidation can publish its replacement revision;
- the previous revision remains in SQLite and the cold archive for history.

The latest verification remains a single fast-read row per batch. The same
transaction also appends an immutable audit row, so the current status and its
history cannot diverge. Existing databases are upgraded automatically; the
latest pre-upgrade status is copied into the history once, and subsequent checks
produce one history row each. History can be inspected directly with:

```bash
sqlite3 -header -column Storage/Database/supply_chain.db \
  "SELECT id, batch_id, snapshot_id, checked_at, status, message FROM snapshot_verification_history ORDER BY id DESC LIMIT 50;"
```

The QR URL remains stable because it identifies the batch. A scan resolves the
current active Snapshot at scan time. Before the window it remains hidden;
during the window it is refreshed; after the window its final revision remains
readable. Existing Snapshots created before availability windows were added
remain readable.
