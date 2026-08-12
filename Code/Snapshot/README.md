# Public Snapshot Module

This module converts one completed, verified private supply-chain batch into a
consumer-safe public preview. It owns public-field selection, privacy policy,
canonical Manifest generation, and the independent Public Merkle Root.

It does not publish to IPFS or a blockchain in the current stage.

## Module layout

```text
Snapshot/
├── include/
│   ├── snapshot.hpp          # Independent snapshot input and output types
│   └── snapshot_policy.hpp   # Public evidence and exclusion policy
├── src/
│   ├── snapshot.cpp          # Validation, Manifest, and Public Root
│   └── snapshot_policy.cpp   # Central disclosure allowlist
├── CMakeLists.txt
└── README.md
```

The module has no dependency on the HTTP server or SQLite structures. The
private server converts its records into `BatchInput` through a separate
adapter.

## Current flow

```text
Completed private batch
        |
        +-- four route stages present
        +-- parent block links consistent
        +-- every stage Merkle-verified
        +-- every stage signature-verified
        v
Consumer-safe field policy
        |
        +-- structured fresh-produce trace fields
        +-- administrator-selected allowlisted CIDs
        v
Canonical Public Manifest
        |
        +-- Public Merkle Root
        +-- final private block hash reference
        +-- explicit private-data exclusions
```

## Public fields

The first protocol is `Schnucks-Trace-v1`. It includes:

- batch ID, product, fresh-produce category, and completion status;
- harvest date, farm location, and certificate ID;
- logistics pickup/delivery, local departure/arrival time, and condition
  summaries;
- warehouse local inbound/outbound time and condition summaries;
- store location, shelf placement date, and sell-by date;
- fixed route, aggregate Merkle/signature verification status, and final
  private block hash;
- selected public evidence CIDs.

Preview identifiers use
`SNAP-<batch-id>-<UTC-generation-time>-V0001`, so two previews with different
generation times do not reuse one snapshot identifier.

Transport and warehouse event times currently originate from `datetime-local`
inputs. Their source records contain no timezone, so the Manifest labels these
values as local time with `time_zone: "unspecified"`. Snapshot generation time
uses UTC.

## Canonicalization and Public Root

The protocol expands the Manifest into a fixed ordered list of public field
paths and values. Each Merkle leaf uses this byte sequence:

```text
<field-path>:<UTF-8-value-byte-count>:<value>
```

Numbers are normalized before entering the list. Evidence is ordered by route
stage, public evidence type, and CID. The existing Merkle library applies its
duplicate-last-hash rule when a level has an odd number of nodes. This produces
a Public Root that authenticates only the consumer-visible Manifest fields and
selected evidence CIDs; no private Merkle leaf or private Merkle root is reused.

## Evidence policy

Only categories listed by `public_evidence_policy()` can enter a snapshot.
Harvest photos, product photos/labels, and recall notices are selected by
default when present. Inspection reports and seal verification images require
an explicit administrator selection.

Raw GPS tracks, temperature logs, transport documents, energy logs, receipts,
transactions, and other operational attachments are excluded.

The preview carries selected existing CIDs as references. It does not retrieve,
copy, or upload their file bodies.

## Private data exclusions

The public Manifest excludes:

- participant UID, username, display name, and organization ID;
- signatures, public keys, challenges, and signed payloads;
- private Merkle leaves, proofs, roots, and canonical records;
- shipment, vehicle, container, storage lot, zone, and rack identifiers;
- raw operational logs and unapproved CIDs;
- attachment filenames, content types, and sizes.

## Control-server API

Both endpoints require an administrator Bearer token:

```text
GET  /api/snapshot/eligible-batches
POST /api/snapshot/preview
```

The preview request uses form fields:

```text
batchId=BATCH-ORANGES-0001
selectedEvidence=supplier|harvestPhotos|bafy...
```

The response contains the Manifest object, Public Root, final private block
hash, selected evidence count, and excluded-field summary. It is not persisted.

## Current boundary

Implemented: eligibility validation, disclosure policy, Manifest generation,
Public Merkle Root, server adapter, API, and administrator preview.

Deferred: saved snapshot versions, Manifest distribution, Solidity anchor,
public-chain transaction submission, periodic snapshots, incremental snapshots,
hot/cold data tiers, and consumer QR access.
