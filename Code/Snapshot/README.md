# Snapshot Gateway

This module is the disclosure and admission boundary between the private
supply-chain implementation and a future public-chain implementation. It has
two separate responsibilities:

1. C++ selects consumer-safe fields from one completed private batch and
   builds a canonical Public Manifest with an independent Public Merkle Root.
2. Solidity validates a compact publication request and records its hashes,
   provenance, lifecycle, and publisher on the destination chain.

No public-chain deployment, wallet integration, or transaction submission is
included at this stage.

## Module layout

```text
Snapshot/
├── contracts/
│   ├── SnapshotGateway.sol
│   └── test/
│       └── SnapshotGatewayTest.sol
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
Future public-chain consumer application
```

## Public snapshot eligibility

A batch can enter the snapshot layer only when:

- the supplier, logistics, warehouse, and supermarket stages are present;
- each block points to the preceding private block;
- each stage passes Merkle and digital-signature verification;
- the supermarket stage completes the preset route; and
- all required consumer-facing fields are available.

The first protocol is `Schnucks-Trace-v1`, with snapshot schema version `1`.

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

## Solidity gateway

`SnapshotGateway.sol` is dependency-free Solidity. Its publication checks
cover:

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

## Events

The gateway emits events for:

- publisher, recall-manager, admin, and pause changes;
- snapshot publication with raw IDs and compact hash references;
- replacement of the current batch snapshot;
- batch recall; and
- administrative snapshot revocation.

The original batch ID and snapshot ID are emitted for public consumers. The
stored record uses `bytes32` hashes for compact indexing.

## Tests

`contracts/test/SnapshotGatewayTest.sol` is a self-contained Solidity test
harness with no external testing-library import. It covers:

- valid publication and queries;
- unauthorized publishers;
- duplicate snapshot IDs and nonce replay;
- empty hash fields, unsupported versions, and wrong destination chains;
- replacement and ordered history;
- recall and revocation; and
- role permissions and pause behavior.

`tests/gateway_payload_test.cpp` checks standard Ethereum Keccak-256 vectors
and the `bytes32` normalization performed by the C++ payload builder. It is an
optional target:

```bash
cmake -S Code/Snapshot -B build-snapshot -DSNAPSHOT_BUILD_TESTS=ON
cmake --build build-snapshot
ctest --test-dir build-snapshot
```

The commands are documented for later use; this change does not deploy a
contract or contact an external chain.

## Existing control-server preview

The current administrator preview endpoints remain:

```text
GET  /api/snapshot/eligible-batches
POST /api/snapshot/preview
```

They still generate an in-memory Manifest preview and do not publish it. A
future relayer endpoint can call `build_gateway_payload()`, assign the source
network and nonce, sign a transaction, and call the Solidity gateway.

## Deferred work

- relayer persistence and nonce allocation;
- wallet and key custody;
- contract deployment and network configuration;
- transaction confirmation and retry handling;
- public consumer API and QR-code pages;
- periodic and incremental snapshot policies; and
- optional cross-chain messaging protocol adapters.
