# Snapshot Storage

`SnapshotStorage` is the local lifecycle layer for generated public Snapshots.
It is intentionally independent from the HTTP handlers and the PublicChain
Node service.

## Responsibilities

- SQLite stores Snapshot metadata, per-batch revisions, and lifecycle state.
- A small in-memory map caches the current `active` Snapshot for each batch.
- `snapshot_verification_status` stores the latest local verification time,
  status, and message without changing the published Snapshot.
- Every preview and publication is also written to a local archive directory.
- A semantic route change marks that batch's `preview` and `active` records as
  `invalidated`.
- A successful publication marks the new record `active` and the previous
  active record `superseded`.

The archive is cold local storage. The SQLite table is the source of truth for
state; the memory cache is only a fast read path for the current active record.

The control server creates the archive beside the configured SQLite database in
`snapshot-archive/`. Historical revisions stay available there for local review.

## Automatic refresh

The control server uses the independent `Code/SnapshotScheduler` module to wait
for the nearest due time stored in `snapshot_refresh_schedule`. The actual
refresh period is configured per product in the control panel and defaults to
3600 seconds (1 hour) when no product policy has been saved. The compact editor
inside `Consumer Data Preview` accepts whole minutes, hours, or days.

Each batch keeps its next due time in `snapshot_refresh_schedule`. Saving a
product policy resets the next due time for that product so the new period takes
effect without a page reload.

Each scheduled cycle checks batches that already have a published or invalidated
Snapshot:

- unchanged source blocks and route fingerprints update only the hot verification
  status; no public-chain publication is created;
- changed source blocks create and publish a new immutable Snapshot revision;
- a completed route after an invalidation can publish its replacement revision;
- the previous revision remains in SQLite and the cold archive for history.

The QR URL remains stable because it identifies the batch. A scan resolves the
current active Snapshot at scan time.
