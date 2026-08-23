# Hybrid-Chain Supply Chain Traceability Prototype

This repository is a C++17 supply-chain traceability prototype organized into
three application modules, two independent infrastructure modules, and several
standalone supporting tools:

```text
Private Chain -> Public Snapshot -> Public Chain -> Customer Verification
```

The private-chain module, consumer-safe snapshot gateway, local EVM
publication service, and customer trace page are connected in one local
prototype. Each module remains independently owned and documented.

## Current architecture

```text
Authenticated participant
          |
          | role-specific event and optional IPFS file
          v
PrivateChain
  +-- configurable linear supply-chain route
  +-- linked block chain per product batch
  +-- independent Merkle Tree inside every block
  +-- ECDSA P-256 confirmation
  +-- SQLite structured state
  +-- IPFS CID references
          |
          | approved public fields only
          v
Snapshot
  +-- canonical public manifest
  +-- independent public root
  +-- selected existing evidence CIDs
          |
SnapshotStorage
  +-- SQLite hot lifecycle index
  +-- in-memory cache for the active Snapshot
  +-- latest local verification status
  +-- local cold archive for historical revisions
          |
SnapshotScheduler
  +-- configurable automatic refresh interval
  +-- unchanged-source verification or changed-source publication
          |
          v
PublicChain
  +-- Hardhat EVM node
  +-- SnapshotGateway contract
  +-- administrator publication service
  +-- public Manifest storage and verification
  +-- full customer trace page :8082
  +-- one stable batch QR display :8084
  +-- compact verification-only scan view
```

Private operational records and public consumer data have separate Merkle
roots. Removing private fields from a public view requires generating a new
canonical public manifest and root.

## Repository layout

```text
Blockchain Structure/
├── Architecture/                    # Architecture diagrams
├── Code/
│   ├── PrivateChain/                # Runnable private-side prototype
│   │   ├── Server/                  # User/control servers, APIs, and pages
│   │   ├── MerkleTree/              # Reusable Merkle Tree and CLI
│   │   ├── DigitalSignature/         # ECDSA P-256 adapter
│   │   ├── MemoryPool/               # Fixed-block allocator
│   │   ├── ConMemPool/               # Concurrent allocator
│   │   └── Database/                 # Local SQLite data
│   ├── Snapshot/                    # Public snapshot preview module
│   ├── SnapshotStorage/              # Snapshot lifecycle, hot index, and archive
│   ├── SnapshotScheduler/            # Independent C++ automatic refresh timer
│   ├── PublicChain/                 # EVM gateway, publisher, and customer page
│   ├── MerkleTreeNTree/              # Standalone N-ary Merkle Tree visualizer
│   ├── SnapshotQRCode/               # Independent QR generator integration copy
│   ├── CMakeLists.txt               # Central C++ build entry
│   ├── PrCsample.sol                # Historical Solidity sample
│   ├── SNsample.sol                 # Early snapshot contract sample
│   └── QRCodeExample.html           # Historical QR sample
├── server_concurrency_test.py       # Allocator benchmark
├── start_user_server.sh             # User submission service :8080
├── start_control_server.sh          # Administrator control service :8081
├── start_customer_server.sh         # Customer trace :8082 and QR display :8084
└── README.md
```

Generated build output remains under `Code/build`. The private SQLite database
is stored at `Code/PrivateChain/Database/supply_chain.db` and is ignored by
Git.

Snapshot lifecycle records use a separate `snapshot_storage` SQLite table in
that database. The current active record is also held in a small in-memory
cache, while each preview and publication revision is written to the local
`Code/PrivateChain/Database/snapshot-archive/` cold archive.

## Module documentation

- [Private-chain overview](Code/PrivateChain/README.md)
- [User and control servers](Code/PrivateChain/Server/README.md)
- [Merkle Tree library and CLI](Code/PrivateChain/MerkleTree/README.md)
- [Database notes](Code/PrivateChain/Database/README.md)
- [Public snapshot design](Code/Snapshot/README.md)
- [Snapshot lifecycle storage](Code/SnapshotStorage/README.md)
- [Snapshot automatic scheduler](Code/SnapshotScheduler/README.md)
- [Public-chain design](Code/PublicChain/README.md)
- [N-ary Merkle Tree and live visualizer](Code/MerkleTreeNTree/README.md)
- [Snapshot QR Code integration](Code/SnapshotQRCode/README.md)

The module READMEs own implementation details, APIs, schemas, privacy rules,
and planned responsibilities. This README remains the project entry point.

## Implemented private-chain flow

The default route template is:

```text
Supplier -> Logistics -> Warehouse -> Supermarket
```

The administrator edits the default template or a batch-specific route from
the control Canvas. Canvas changes synchronize automatically, including
temporarily incomplete drafts. A route may contain repeated Logistics and
Warehouse stages or connect Supplier directly to Supermarket. Supplier remains
the required first stage and Supermarket remains the required final stage.
Each later participant selects an existing batch when that participant's role
is the next required stage. Batch master data is inherited along the route.

Route nodes have stable IDs. A semantic route change creates a new active route
revision, immediately invalidates the current public Snapshot, and keeps
already-created Blocks as historical records. An unconnected draft node remains
Canvas-only. Once the node is connected into a complete path, it appears in the
administrator preview as pending; its Block and preview arrows appear after
the assigned participant submits and passes verification. A restored route
with the exact previous route fingerprint can use its matching publication
again; a new route requires a new completed Snapshot before publication. Route
state changes are pushed to the open pages through server-sent events, so a
manual page refresh is unnecessary.

Identifiers follow the stage occurrence within the active route: Logistics 1
uses `SHIP-0001` and `VEHICLE-0001`, Logistics 2 uses `SHIP-0002` and
`VEHICLE-0002`; Warehouse 1 uses `STORAGE-0001` and `ZONE-0001`, Warehouse 2
uses `STORAGE-0002` and `ZONE-0002`.

Each supply-chain event creates one block with its own Merkle Tree:

```text
Block 0 ----------------> Block 1 ----------------> Block 2
Supplier                 Logistics                 Warehouse
   |                         |                         |
   +-- Merkle Tree 0         +-- Merkle Tree 1         +-- Merkle Tree 2
       +-- leaves                +-- leaves                +-- leaves
       +-- root                  +-- root                  +-- root
```

The block hash includes the block's Merkle root and the previous block hash.
This gives the prototype an outer linked block chain and an independently
verifiable tree for each event.

## Implemented features

- independent user and administrator HTTP servers;
- account authentication, logout, and optional persistent sessions;
- role-specific forms for Supplier, Logistics, Warehouse, and Supermarket;
- administrator-defined default and per-batch route-order enforcement;
- generated and validated identifiers;
- route-stage account assignment with node-specific confirmation policies;
- typed-name confirmation with browser ECDSA P-256 signing and C++ OpenSSL
  verification;
- per-block Merkle leaves, roots, proofs, and verification;
- linked parent block IDs and hashes;
- SQLite persistence for users, sessions, batches, blocks, leaves, edges,
  signatures, confirmation policies, and CID metadata;
- local Kubo/IPFS upload integration through returned CIDs;
- administrator chain and Merkle Tree visualization;
- completed-batch public Manifest, Public Root, and Gateway Payload;
- administrator-triggered publication to the local Hardhat EVM gateway;
- Snapshot preview, active, superseded, and invalidated lifecycle states with
  per-batch revisions;
- SQLite hot Snapshot index, active-record memory cache, and local historical
  Snapshot archive;
- a C++ scheduler that waits for each batch's stored product due time, records
  unchanged verification times locally, and publishes a new immutable revision
  when completed source data changes;
- independently revalidated publication candidates and public Manifests;
- customer published-batch selection with public route and chain verification
  details;
- one stable batch QR Code displayed after successful publication; the QR page
  contains only that image, while scanning resolves the current active Snapshot
  and opens its Verification Result and Trace Route view;
- an independent N-ary Merkle Tree CLI with runtime arity, proofs, NDJSON
  events, and a live SSE visualizer; it is separate from the production
  per-block binary Merkle Tree;
- live route, block, Snapshot, and customer-page synchronization through
  server-sent events;
- bounded server worker pool using selected `MemoryPool` and `ConMemPool`
  components.

The control panel requires Typed Name confirmation for every connected route
node, with Typed Name selected by default. This is the confirmation method
implemented and exposed by the current demo. Handwritten and face confirmation
are not part of the current user flow.
Inspection Agency, production key custody, durable relayer jobs, and
public-testnet deployment are pending.

## Requirements

- CMake 3.15 or newer;
- a C++17 compiler;
- OpenSSL;
- SQLite3;
- local Kubo/IPFS when file upload is used.
- Python 3 for the standalone N-ary Merkle Tree visualizer.

The independent PublicChain prototype additionally requires Node.js 22 LTS
and npm. An Apple M3 Mac with 18 GB memory is sufficient for the local Hardhat
node and C++ demo servers. See
[Public-chain setup](Code/PublicChain/README.md#apple-silicon-environment) for
the Homebrew commands and local EVM workflow.

The current local Kubo API configuration expects:

```text
http://127.0.0.1:5002
```

## Local services and ports

| Service | Address | Started by |
| --- | --- | --- |
| Participant submission page | `http://127.0.0.1:8080/` | `start_user_server.sh` |
| Administrator control page | `http://127.0.0.1:8081/` | `start_control_server.sh` |
| Customer trace page and publication API | `http://127.0.0.1:8082/` | `start_customer_server.sh` |
| Stable batch QR display | `http://127.0.0.1:8084/` | `start_customer_server.sh` |
| Hardhat JSON-RPC node | `http://127.0.0.1:8545` | `start_customer_server.sh` |
| Kubo IPFS API | `http://127.0.0.1:5002` | Homebrew service |

Kubo is one background service shared by the participant and administrator
servers. It does not need a dedicated terminal after it has been configured.
The three project scripts remain separate because they represent three
different application roles. A port error usually means that the corresponding
service is already running; close the old process or reuse the existing page
instead of starting a second copy.

The administrator workflow editor is a lightweight static SVG/DOM Canvas. It
supports node dragging, output-to-input handle connections, connection
selection and deletion, background panning, trackpad pinch zoom, zoom controls,
fit-to-route, explicit left-to-right auto-arrangement, and Undo/Redo. Adding a
node creates an unconnected, free-positioned stage without changing the current
route, pan, or zoom. The administrator assigns an unused active account with the
matching role, positions the node, and connects it manually. Semantic route
edits are autosaved as revisions and invalidate the current Snapshot
immediately. Only connected nodes appear in the route preview. A connected node
without a submitted Block has no preview arrow; the arrow appears after the
assigned participant submits and passes verification. The editor continues using
the existing workflow API and SQLite data; no new frontend framework or graph
database is required.

### One-time Kubo setup on macOS

```bash
brew install ipfs
ipfs init
ipfs config Addresses.API /ip4/127.0.0.1/tcp/5002
brew services start kubo
```

If Homebrew reports a `launchctl bootstrap ... exit 5` error, inspect the
service state before changing anything:

```bash
brew services list
ps aux | rg '[i]pfs daemon'
lsof -nP -iTCP:5002 -sTCP:LISTEN
```

An already-running Kubo daemon can be left in place. If the Homebrew service
entry is stale, restart that one service with `brew services restart kubo`.
The application expects the API on port 5002; changing the Kubo API port
requires updating `IPFS_API_URL` for the private-chain server as well.

## Build

Configure from the central `Code` directory. Reconfigure after pulling the
directory migration so CMake discovers the new source paths.

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

## Run the three business services

After the one-time CMake build and PublicChain npm setup, run these three
scripts from the project root. Each script owns one business-facing service:

```bash
./start_user_server.sh
```

```bash
./start_control_server.sh
```

```bash
./start_customer_server.sh
```

The pages are:

```text
User submission page: http://127.0.0.1:8080/
Control page:         http://127.0.0.1:8081/
Customer trace page:  http://127.0.0.1:8082/
QR display page:      http://127.0.0.1:8084/
```

The user and control scripts check that the Homebrew Kubo service is available
on IPFS API port 5002. The customer script starts a local Hardhat node on port
8545 in the background when no process is listening, deploys SnapshotGateway
after that fresh start, starts the QR display page on port 8084, and then
starts the customer service on port 8082. Existing Hardhat and QR display
processes are reused.

The three project scripts remain independent. The customer script owns both
customer-facing ports 8082 and 8084. Hardhat is local in-memory state, so a
newly started node requires a new deployment;
the private SQLite database is unaffected. IPFS runs as a Homebrew background
service and does not require an additional terminal. If a start script reports
`launchctl bootstrap ... exit 5`, check whether an IPFS daemon already owns port
5002 before restarting Homebrew:

```bash
lsof -nP -iTCP:5002 -sTCP:LISTEN
ipfs id
```

An active listener can be reused. If there is no listener, repair the service
with `brew services restart kubo` and confirm port 5002 before retrying.

In the administrator control page, generate a completed-batch snapshot and
select **Publish to Local Public Chain**. The control server forwards the
verified publication candidate to the independent PublicChain service. The
control page reports the publication block number. The independent customer
page then loads the published product-batch selection list. Open it separately
at `http://127.0.0.1:8082/` when customer-facing verification is needed. Clicking
a batch loads its active contract record, matching public Manifest, and verifies
the identifiers, Manifest hash, Public Merkle Root, and source block anchor.
The customer does not type a Batch ID. The normal customer page remains the
full route and evidence view.

The QR display page at `http://127.0.0.1:8084/` contains only one QR image for
the most recently published batch. Its URL includes
`batch=<batch-id>&view=verification`. Scanning it resolves the batch's current
active Snapshot and opens its Verification Result and Trace Route on port 8082.
Public evidence and technical verification details are hidden in this QR view.
A route change temporarily makes the scan result unavailable while the QR image
stays unchanged; after a replacement Snapshot is published, the same QR Code
opens the new active revision.

The control server stores a separate refresh period for each product. A product
without a saved policy uses the default period of 3600 seconds (1 hour). The
scheduler waits for each batch's next due time and wakes immediately when a
route, block, publication, or product policy changes. When the source block hash
and route fingerprint are unchanged, only the local latest-verification time is
updated. A changed completed source creates and publishes a new immutable
Snapshot revision.

## Demonstration accounts

| Role | Username | Password |
| --- | --- | --- |
| Supplier | `supplier01` | `supplier123` |
| Logistics | `logistics01` | `logistics123` |
| Logistics | `logistics02` | `logistics123` |
| Logistics | `logistics03` | `logistics123` |
| Warehouse | `warehouse01` | `warehouse123` |
| Warehouse | `warehouse02` | `warehouse123` |
| Warehouse | `warehouse03` | `warehouse123` |
| Supermarket | `supermarket01` | `supermarket123` |
| Administrator | `admin01` | `admin123` |

These credentials are local demonstration data and must not be reused in a
deployed environment.

## Standalone Merkle Tree CLI

The CLI remains available inside the private-chain module:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/PrivateChain/MerkleTree"
./run_merkle.sh
```

`inp.txt` is used only by this CLI demonstration. Server records are supplied
through the HTTP/API flow and stored in SQLite.

## Standalone N-ary Merkle Tree visualizer

The independent `Code/MerkleTreeNTree` tool accepts an arity `N >= 2`, builds
SHA-256 trees, verifies proofs, emits NDJSON events, and serves a live browser
visualizer over SSE. It is a learning and inspection tool; the supply-chain
servers continue to use the existing binary Merkle Tree implementation.

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/MerkleTreeNTree"
./run_visualizer.sh
```

The visualizer is available at `http://127.0.0.1:8765/`.

## Solidity samples and next stages

`Code/PrCsample.sol` is a historical generic record-storage experiment.
`Code/SNsample.sol` is an early snapshot-anchor sketch. Both remain unchanged
at the `Code` root for reference. The production snapshot format is implemented
in `Code/Snapshot`, and the public gateway is implemented in
`Code/PublicChain`; neither historical sample is part of the runtime.

The production gateway contract now lives only at
`Code/PublicChain/contracts/SnapshotGateway.sol`. The local administrator
publication and customer query flow is implemented without using either
historical sample. The next architectural milestones are durable publication
job storage, production key custody, and public-testnet deployment.
