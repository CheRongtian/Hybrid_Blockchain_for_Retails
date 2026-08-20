# Private Chain Prototype

This module contains the currently runnable supply-chain system. It accepts
role-specific events, links them into a batch chain, builds an independent
Merkle Tree inside every block, stores local state in SQLite, and keeps large
files in IPFS through CID references.

## Module layout

```text
PrivateChain/
├── Server/             # User server, control server, APIs, and web pages
├── MerkleTree/         # Reusable C++ Merkle Tree and standalone CLI
├── DigitalSignature/   # OpenSSL ECDSA P-256 verification adapter
├── MemoryPool/         # Fixed-block allocator
├── ConMemPool/         # Concurrent allocator
├── Database/           # Local SQLite database location
└── README.md
```

Detailed references:

- [Server](Server/README.md)
- [MerkleTree](MerkleTree/README.md)
- [Database](Database/README.md)

## Configurable route

```text
Supplier -> Logistics -> Warehouse -> Supermarket
```

This sequence is the initial route template. The control Canvas can edit the
default route or a route for one batch. Repeated Logistics and Warehouse stages
and direct Supplier-to-Supermarket delivery are supported. Canvas changes are
synchronized automatically, while semantic route changes are stored as route
revisions, including incomplete drafts. The Supplier creates a new product
batch, later roles follow that batch's active route, and the Supermarket
completes it.

Each route node has a stable ID. Adding, deleting, or reconnecting a node keeps
existing Blocks as historical records, switches the batch to the new route
revision, and invalidates the current public Snapshot. An unconnected node is
excluded from the current chain preview. A connected node appears as pending
until its assigned participant submits a verified event. The matching route
fingerprint is required when a Snapshot is previewed, published, or displayed
on the customer page. Route changes update the open administrator and customer
pages through server-sent events; a manual page refresh is not required.

The route preview contains only connected nodes. A connected node without a
Block has no preview arrow. The arrow appears after the assigned participant
submits the event and passes Typed Name verification. Confirmation policies are
therefore configured only for connected nodes.

Each batch is represented as an outer linked block chain:

```text
Block 0 ------------> Block 1 ------------> Block 2
   |                     |                     |
   +-- Merkle Tree 0     +-- Merkle Tree 1     +-- Merkle Tree 2
```

Each block hash includes its own Merkle root and the previous block hash. The
Merkle leaves cover inherited batch data, role event fields, authenticated
identity data, signature metadata, CID references, and the parent link.

## Runtime data flow

```text
User browser :8080
        |
        | authenticated role event, confirmation, and CID references
        v
control_server :8081
        |
        +-- validates route order and field formats
        +-- verifies ECDSA P-256 confirmation
        +-- builds and verifies a per-block Merkle Tree
        +-- commits batches, blocks, leaves, edges, and CIDs to SQLite
        +-- forwards selected files to the local IPFS API
        +-- previews and publishes consumer-safe public snapshots for completed
            batches through PublicChain
        +-- serves the administrator control page
```

The user-facing static page is served by `user_server`. Authentication, block
creation, persistence, IPFS forwarding, and the administrator page are owned
by `control_server`.

## Build and run

Configure the project from the top-level `Code` directory:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

Start both servers from `Code/build` in separate terminals:

```bash
./Server/control_server
```

```bash
./Server/user_server
```

Open:

```text
User page:    http://127.0.0.1:8080/
Control page: http://127.0.0.1:8081/
```

The source directories moved under `PrivateChain`, while the output paths stay
under `Code/build/Server` and `Code/build/MerkleTree`.

The root launch scripts also check the shared Kubo service. They do not create
a second IPFS daemon for each server.

## Persistence and IPFS

The default SQLite database is:

```text
Code/PrivateChain/Database/supply_chain.db
```

The database stores structured records, hashes, Merkle proofs, block links,
identity metadata, and CID metadata. Large file bodies stay in IPFS.

The local Kubo API is expected at:

```text
http://127.0.0.1:5002
```

Kubo is an external local background service. Configure it once, then the
participant and administrator launch scripts reuse it:

```bash
brew install ipfs
ipfs init
ipfs config Addresses.API /ip4/127.0.0.1/tcp/5002
brew services start kubo
```

If `brew services start kubo` reports a `launchctl bootstrap` error, check
`brew services list` and `lsof -nP -iTCP:5002 -sTCP:LISTEN`. An existing Kubo
daemon can be reused; a stale Homebrew entry can be repaired with
`brew services restart kubo`.

## Identity confirmation

The administrator configures confirmation methods for each connected route
node. Every connected node must have at least one enabled method, and a user
can select only a method enabled for the current route node.

Typed-name confirmation is currently implemented end to end. The browser signs
the canonical confirmation payload with ECDSA P-256, and the C++ control server
verifies it through OpenSSL before saving the block. Typed Name is the current
confirmation method exposed by the user flow; handwritten and face confirmation
are not part of the current UI.

## Concurrency and allocation

The control server dispatches accepted sockets through a bounded worker pool.
`MemoryPool` supplies fixed task nodes, while `ConMemPool` supplies concurrent
callable storage. Block numbering, parent selection, Merkle construction, and
the final SQLite commit remain serialized.

The repository-level `server_concurrency_test.py` benchmarks allocator
throughput independently from HTTP, SQLite, Merkle, and IPFS work.

## Current boundary

This module is the private-side prototype. The sibling `Snapshot` module
generates a consumer-safe Manifest and Public Root preview from a completed
current route. The control server forwards an administrator-approved
publication candidate to the independent PublicChain service, which submits
the local EVM transaction, generates a Snapshot-specific customer QR Code, and
serves the full customer page on `:8082` and the QR-only display page on `:8084`.
Scanning the QR Code opens the compact Verification Result and Trace Route view.
Wallet custody, production chains, and cross-chain relaying remain outside the
current prototype.
