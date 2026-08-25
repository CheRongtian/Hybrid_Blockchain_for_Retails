# Snapshot Gateway

This module is the disclosure boundary between the private supply-chain
implementation and the PublicChain module. It has two responsibilities:

1. C++ selects consumer-safe fields from one completed private batch and
   builds a canonical Public Manifest with an independent Public Merkle Root.
2. C++ converts that preview into a compact Gateway Payload for the Solidity
   contract owned by `Code/PublicChain`.

It also serializes a publication candidate containing the exact canonical
Manifest and its ordered public fields. This lets the independent PublicChain
service rebuild and verify every hash before a transaction is submitted.

This module does not deploy contracts or submit transactions.

## Module layout

```text
Snapshot/
├── examples/
│   └── gateway_payload.example.json
├── include/
│   ├── gateway_payload.hpp
│   ├── snapshot.hpp
│   └── snapshot_policy.hpp
├── src/
│   ├── gateway_payload.cpp
│   ├── snapshot.cpp
│   └── snapshot_policy.cpp
├── tests/
│   └── gateway_payload_test.cpp
├── CMakeLists.txt
└── README.md
```

The C++ library has no dependency on the HTTP server or SQLite structures. A
private-chain adapter converts server records into `BatchInput` values.

## End-to-end boundary

```text
Completed and verified private batch
        |
        v
C++ disclosure policy
        |
        +-- canonical Public Manifest
        +-- SHA-256 Public Merkle Root
        +-- selected evidence CIDs
        v
C++ gateway payload
        |
        +-- Keccak-256 identifiers and Manifest hash
        +-- final private block hash
        +-- source network, destination chain, and nonce
        v
SnapshotGateway admission checks
        |
        +-- authorized publisher
        +-- protocol and schema version
        +-- destination chain
        +-- source-network nonce replay protection
        +-- snapshot and batch lifecycle
v
Public-chain customer application
```

## Public snapshot eligibility

A batch can enter the snapshot layer only when:

- the active route is one connected linear path from Supplier to Supermarket;
- every node on that current path has a matching verified private Block;
- each Block points to the preceding private Block in the current path;
- each stage passes Merkle and digital-signature verification; and
- all required consumer-facing fields are available.

The route may contain repeated Logistics or Warehouse stages. Each private
Block carries its stable route-node ID and route revision. Historical Blocks
from deleted or superseded route nodes remain available to the private chain,
but they are excluded from the current Snapshot. The preview includes a
deterministic `routeFingerprint`; publication and customer lookup require that
fingerprint to match the current private route. A route change therefore makes
an older Snapshot unavailable until the exact route is restored or a new
Snapshot is published.

The first protocol is `Supermarket-Trace-v1`, with snapshot schema version `1`.

## Public Manifest

The current allowlist can publish:

- batch ID, product, fresh-produce category, and route completion status;
- harvest date, farm location, and certificate ID;
- logistics locations, local timestamps, and condition summaries;
- warehouse local timestamps and condition summaries;
- store location, shelf placement date, and sell-by date;
- aggregate verification state and the final private block hash; and
- administrator-selected, allowlisted evidence CIDs.

Participant credentials, signatures, private Merkle data, internal asset IDs,
raw sensor logs, unapproved CIDs, and attachment metadata remain private.

Transport and warehouse event times originate from `datetime-local` inputs.
Their records currently contain no time zone, so the Manifest marks the time
zone as `unspecified`. Snapshot generation time uses UTC.

## Canonical Public Root

The Manifest is expanded into a fixed ordered list. Each Merkle leaf uses:

```text
<field-path>:<UTF-8-value-byte-count>:<value>
```

Numbers are normalized, and evidence is sorted by route stage, public evidence
type, and CID. The private-chain Merkle implementation applies its
duplicate-last-hash rule when a level has an odd number of nodes.

The resulting `publicRoot` remains a SHA-256 Merkle root. It authenticates the
consumer-visible Manifest only and does not reuse a private Merkle root.

## Gateway payload

`build_gateway_payload()` converts a `Preview` and a `GatewayContext` into the
fixed values needed by `SnapshotGateway.publishSnapshot()`.

`publication_candidate_json()` packages the preview for the local publication
service. It includes the canonical Manifest, parsed Manifest, ordered public
fields, Keccak identifier hashes, Manifest hash, SHA-256 Public Root, and final
private block hash. Destination-chain settings and the publication nonce are
assigned by the PublicChain service at transaction time.

| Payload field | Source and encoding |
| --- | --- |
| `protocol` | Raw protocol name |
| `snapshotId` | Raw unique snapshot identifier |
| `batchId` | Raw private batch identifier |
| `protocolHash` | Keccak-256 of `protocol` |
| `snapshotIdHash` | Keccak-256 of `snapshotId` |
| `batchIdHash` | Keccak-256 of `batchId` |
| `publicRoot` | Existing SHA-256 root normalized to `bytes32` hex |
| `manifestHash` | Keccak-256 of the exact canonical Manifest UTF-8 bytes |
| `sourceBlockHash` | Final private block hash normalized to `bytes32` hex |
| `routeFingerprint` | SHA-256 fingerprint of the active route nodes and edges |
| `sourceNetworkId` | Keccak-256 of the configured private-network name |
| `destinationChainId` | Intended EVM destination chain ID |
| `nonce` | Unique positive value within one source network |
| `snapshotVersion` | Public Manifest schema version |

The payload includes raw IDs for contract events and their Keccak-256 hashes
for indexing. The contract recomputes the raw ID hashes and never accepts a
caller-provided ID hash as authoritative.

The Solidity contract does not parse or repeat the C++ field-selection policy.
It admits the exact Manifest digest after validating publisher authority,
protocol metadata, provenance, replay protection, and lifecycle rules. Public
clients later verify downloaded Manifest bytes against `manifestHash` and its
fields against `publicRoot`.

`examples/gateway_payload.example.json` documents the serialized boundary
between a future relayer and the contract client. Its `manifestHash` uses the
minimal example bytes `{"example":true}`. Production values must always come
from the exact Manifest bytes passed to `build_gateway_payload()`.

The payload builder requires a caller-supplied source network name,
destination chain ID, and nonce. These values belong to the future relayer or
publication service and are deliberately absent from snapshot field policy.

## Public-chain gateway boundary

`../PublicChain/contracts/SnapshotGateway.sol` consumes this module's Gateway
Payload. Its publication checks cover:

- authorized publisher accounts;
- an administrator-approved source network;
- an exact supported protocol hash and snapshot version;
- nonzero Public Root, Manifest hash, source block hash, and source network;
- `destinationChainId == block.chainid`;
- one-time use of each `(sourceNetworkId, nonce)` pair;
- unique snapshot IDs; and
- a permanent publication stop for recalled batches.

The administrator manages publishers, recall managers, source-network
allowlisting, pause state, and admin transfer. Contract deployment grants the
deployer all three initial account roles. At least one source network must be
explicitly enabled with `setSourceNetwork()` before publication.

### Lifecycle

```text
Active --new snapshot for same batch--> Superseded
Active --recall manager-------------> Recalled
Any stored state --administrator----> Revoked
```

Publishing a newer snapshot for an active batch keeps the old record in batch
history and marks it `Superseded`. Recall marks the current record `Recalled`
and prevents any later snapshot for that batch. Revocation removes a revoked
record from the current pointer while preserving its immutable history entry.

The contract exposes individual snapshot lookup, current snapshot lookup by
raw batch ID, and ordered batch history.

### Events

The gateway emits events for:

- publisher, recall-manager, admin, and pause changes;
- snapshot publication with raw IDs and compact hash references;
- replacement of the current batch snapshot;
- batch recall; and
- administrative snapshot revocation.

The original batch ID and snapshot ID are emitted for public consumers. The
stored record uses `bytes32` hashes for compact indexing.

## C++ tests

`tests/gateway_payload_test.cpp` checks standard Ethereum Keccak-256 vectors
and the `bytes32` normalization performed by the C++ payload builder. It is an
optional target:

```bash
cmake -S Code/Snapshot -B build-snapshot -DSNAPSHOT_BUILD_TESTS=ON
cmake --build build-snapshot
ctest --test-dir build-snapshot
```

Solidity and relayer tests are documented in `../PublicChain/README.md`.

## Control-server integration

The current administrator preview endpoints remain:

```text
GET  /api/snapshot/eligible-batches
POST /api/snapshot/preview
POST /api/snapshot/publish
```

The preview endpoint generates the Manifest and publication candidate through
this module. The control server then records the preview revision through the
sibling `Code/SnapshotStorage` module. Preview generation does not publish to
the public chain.

The publish endpoint is administrator-only and forwards the candidate to the
independent PublicChain service. After a successful publication, the control
server records the active revision, transaction hash, and publication response
through SnapshotStorage. Snapshot field selection and Manifest creation remain
owned by this module.

SnapshotStorage keeps `preview`, `active`, `superseded`, and `invalidated`
states. SQLite is the hot lifecycle index, the current active record has an
in-memory cache, and each revision is archived locally for historical lookup.
The Public Manifest can include a batch availability window. Its start and end
are public Merkle fields, so the window is covered by the Public Root and
Manifest hash. Both values must be present and the end must follow the start.
Legacy inputs may omit the window.
The control server's independent `Code/SnapshotScheduler` waits for the nearest
due time recorded by SnapshotStorage for active and previously invalidated
batches. Unchanged source data updates local verification status only; changed
source data produces a new candidate for publication. The Snapshot library
itself remains a pure builder and does not own timers, HTTP requests, or EVM
transactions.

## Deferred work

- durable relayer job and nonce persistence;
- wallet and key custody;
- transaction confirmation and retry handling;
- optional cross-chain messaging protocol adapters.
