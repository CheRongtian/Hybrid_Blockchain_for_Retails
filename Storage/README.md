# Runtime Storage

This directory isolates generated application data from source code.

```text
Storage/
├── Database/          # Private SQLite database
├── Snapshots/         # Historical Snapshot revisions by batch
├── PublicManifests/   # Customer-safe published Manifests
└── QRCodes/           # Stable batch QR images
```

The default database is `Database/supply_chain.db`. Snapshot lifecycle metadata
remains in SQLite, while each preview or publication revision is archived under
`Snapshots/<batch-id>/`.

SQLite keeps both the latest verification status for fast customer reads and an
append-only `snapshot_verification_history` audit trail for every refresh check.

The source modules remain under `Code/`. Set `SUPPLY_CHAIN_STORAGE_ROOT` before
starting the services to use another absolute storage root.

Runtime database, Manifest, QR, and Snapshot files are ignored by Git. One
stable Snapshot revision remains as a repository example; newer revisions are
local runtime data.
