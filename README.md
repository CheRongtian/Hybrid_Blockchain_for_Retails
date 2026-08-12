# Hybrid-Chain Supply Chain Prototype

This repository contains a local supply-chain verification prototype built
around a C++ Merkle Tree, two HTTP servers, SQLite, and IPFS CID references.

The current runnable route is:

```
Supplier -> Logistics -> Warehouse -> Supermarket
```

The route is fixed by the control server. A browser user submits data only for
the authenticated stage. The control server validates the stage order, creates
the next Merkle block, stores the local verification snapshot, and links the
block to the previous block for the same batch.

## Data flow

```
User browser :8080
        |
        | stage event and CID references
        v
control_server :8081
        |
        +-- bounded worker ThreadPool
        |
        +-- batch master data and event data -> SQLite
        +-- large file upload -> local IPFS -> CID
        +-- each block's data, CID references, and parent link -> one Merkle Tree per block
        +-- parent block hash links the independent block trees into one chain
        +-- control workflow and chain view
```

Large files are uploaded to IPFS first. The application stores the returned
CID and file metadata in SQLite. The file body does not enter the SQLite
record or the Merkle input.

## Repository structure

```
Blockchain Structure/
├── Architecture/                 # Architecture diagrams
├── Code/
│   ├── CMakeLists.txt             # Top-level CMake entry
│   ├── MerkleTree/                # Merkle Tree library and standalone CLI
│   ├── Server/                    # User server, control server, and pages
│   │   ├── server.cpp             # Control server, API, IPFS adapter
│   │   ├── user_server.cpp        # User-facing static file server
│   │   ├── thread_pool.cpp/.hpp   # Control-server worker queue
│   │   ├── auth_utils.cpp/.hpp    # Password hashing, token hashing, and sessions
│   │   ├── db_utils.cpp/.hpp      # SQLite persistence
│   │   ├── static/                # User page
│   │   └── control_static/        # Control page
│   ├── Database/                  # Local SQLite database location
│   ├── MemoryPool/                # Fixed-block allocator and experiments
│   ├── ConMemPool/                # Concurrent allocator and experiments
│   ├── PrCsample.sol              # Solidity sample
│   ├── SNsample.sol               # Solidity sample
│   └── QRCodeExample.html         # Independent HTML demo
├── ROI/                           # Estimation documents
└── README.md
```

The database files are ignored by Git:

```
Code/Database/supply_chain.db
Code/Database/supply_chain.db-wal
Code/Database/supply_chain.db-shm
```

## Build

Run CMake from the Code directory:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

The build produces:

```
Code/build/Server/control_server
Code/build/Server/user_server
Code/build/MerkleTree/merkle_cli
```

## Run the servers

Start the control server and user server in separate terminals:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/build"
./Server/control_server
```

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/build"
./Server/user_server
```

Open:

```
User page:    http://127.0.0.1:8080/
Control page: http://127.0.0.1:8081/
```

The control server owns authentication, SQLite, one independent Merkle Tree per
block, IPFS upload forwarding, block creation, the worker pool, and the control
page. The user server serves the user page and does not own the database.

## Server concurrency and memory allocation

The control server accepts connections on the main thread and dispatches
request handling to a bounded worker pool. The current integration uses the
two allocator projects at different layers:

```
accepted socket
    -> ThreadPool task queue
    -> MemoryPool fixed task node
    -> worker executes callable stored through ConMemPool
    -> serialized SQLite and Merkle commit
```

`MemoryPool` handles fixed-size queue nodes. `ConMemPool` provides the
concurrent small-object allocation interface used by worker tasks. Large file
bodies remain in the normal request/IPFS path and are not routed through
either allocator.

Block numbering, parent selection, per-block Merkle builds, and SQLite writes
use a serialized commit section. This preserves the order of the preset route
while allowing independent HTTP requests and IPFS operations to run on
different workers.

The user server remains a small static-file server and does not use the
control-server worker pool.

## Allocator benchmark

The root-level `server_concurrency_test.py` script measures allocator
throughput without starting either HTTP server. It compiles a temporary C++
benchmark and compares:

```
new/delete  vs  MemoryPool
malloc/free vs  ConMemPool
```

Run it from the repository root:

```bash
python3 server_concurrency_test.py \
    --threads 1 8 16 \
    --allocations 100000 \
    --repeat 3
```

The benchmark uses the same call boundary for all four allocators, prevents
dead-code elimination of the memory access, and reports the median throughput
across repeated runs. A representative local run produced:

| Threads | MemoryPool vs new/delete | ConMemPool vs malloc/free |
| ---: | ---: | ---: |
| 1 | 2.05x | 1.06x |
| 8 | 0.20x | 3.96x |
| 16 | 0.28x | 3.65x |

The sample indicates that the fixed-block `MemoryPool` is effective with low
contention but is limited by its shared mutex at higher concurrency.
`ConMemPool` has a smaller single-thread advantage and scales better for
concurrent small-object allocation.

This is an allocator microbenchmark rather than an end-to-end server test.
Each benchmark worker allocates and releases on the same thread. In the
control server, a callable is allocated on the accept thread and released by a
worker thread, while a `WorkItem` follows the same queue handoff. Queue
locking, SQLite writes, Merkle commits, HTTP processing, and IPFS operations
are outside the benchmark and can dominate complete request latency.

## Batch and stage behavior

Batch master data is created once for one route batch by the Supplier:

```
Batch ID
Product
Harvest Date
Farm Location
Certificate ID
```

The later stages select an existing batch. Product, harvest date, farm
location, and certificate ID are loaded by the server and shown as inherited
read-only data. The browser does not submit a new product definition for those
stages.

The active role-specific event fields follow the project appendix:

| Role | Structured event data |
| --- | --- |
| Supplier | Harvest Date, Farm Location, Certificate ID |
| Logistics | Shipment ID, Pickup Location, Delivery Location, Departure/Arrival Time, Temperature/Humidity Summary, Vehicle/Container ID |
| Warehouse | Storage Lot ID, Inbound/Outbound Time, Temperature/Humidity Summary, Storage Zone/Rack ID |
| Supermarket | Shelf Placement Date, Expiration/Sell-by Date, Store Location ID |

The route topology remains server-controlled. Pickup and delivery fields are
transport event facts and do not edit the route.

## IPFS and CID flow

The server uses an existing local IPFS/Kubo node. It does not implement the
IPFS protocol:

```
Browser file
    -> POST /api/ipfs/files
    -> control_server calls local IPFS HTTP API
    -> IPFS returns CID
    -> browser submits the CID with the stage event
    -> SQLite stores the CID and metadata
```

The default IPFS API endpoint is:

```
http://127.0.0.1:5002
```

Configure Kubo once on macOS so it uses port 5002 and runs as a background
service:

```bash
ipfs config Addresses.API /ip4/127.0.0.1/tcp/5002
brew services start kubo
```

After that setup, the control server only needs:

```bash
./Server/control_server
```

Set `IPFS_API_URL` only when a deployment uses a different IPFS host or port.

The current local demo accepts files up to 30 MB per upload. The IPFS daemon
must be started separately by the user.

## Additional control-server sources

The control-server target must include these sources when its build files are
updated:

```
Server/thread_pool.cpp
MemoryPool/mempool.cpp
ConMemPool/concurrency_mempool.cpp
```

`ConMemPool/concurrency_mempool.cpp` provides the allocator implementation for
the server build. Its standalone benchmark entry point is enabled only for
the separate allocator test build.

## SQLite tables

The control server creates these tables in Code/Database/supply_chain.db:

```
batches
    batch_id
    product
    harvest_date
    farm_location
    certificate_id
    created_by_uid
    current_stage
    status

supply_chain_records
    block_id
    parent_block_id
    parent_block_hash
    batch_id
    inherited batch fields
    event_data
    canonical_record
    root_hash
    verified
    block_hash
    chain_status
    created_at

block_merkle_leaves
    block_id
    leaf_index
    field_name
    leaf_value
    leaf_hash
    proof
    verified

record_attachments
    block_id
    category
    cid
    filename
    content_type
    size

block_edges
    from_block_id
    to_block_id
    batch_id
    relation

auth_sessions
    token_hash
    uid
    expires_at
    created_at
```

Each supply-chain block owns an independent Merkle Tree. Its leaves cover the
batch master data, role-specific event data, sorted CID references, the parent
block ID/hash, and authenticated identity fields. The block root authenticates
that tree. The block hash includes the root and the parent block hash, so the
block hashes form the outer linked chain. The existing Code/MerkleTree library
is used without modification.

## Control page

The control page has:

- administrator login;
- a Canvas view of the fixed route;
- a block flow for each batch;
- an explicit linked block chain with one independent Merkle Tree inside each block;
- expandable leaves, leaf hashes, proof paths, and Merkle roots for every block;
- role, organization, inherited batch data, event data, and CID references;
- stored verification status and block connections.

The outer arrows represent `Block 0 -> Block 1 -> ...`. The expandable Merkle
section inside each block represents that block's own leaves and root. A block's
parent hash is displayed alongside its Merkle root so the two levels remain
visibly distinct.

## Authentication

The local demonstration accounts are:

| Username | Password | Role |
| --- | --- | --- |
| `supplier01` | `supplier123` | supplier |
| `logistics01` | `logistics123` | logistics |
| `warehouse01` | `warehouse123` | warehouse |
| `supermarket01` | `supermarket123` | supermarket |
| `admin01` | `admin123` | admin |

Both pages provide logout and an unchecked `Remember me on this device`
option. Without the option, the bearer session is temporary and held in memory
for up to eight hours. With the option, the browser stores the token in
`localStorage`, while the control server stores only its SHA-256 hash in the
`auth_sessions` SQLite table for 30 days. Those persistent sessions survive a
control-server restart. Logout removes the browser token and invalidates the
server-side session.

## Standalone Merkle CLI

The Merkle Tree CLI remains independent of the HTTP servers:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/MerkleTree"
./run_merkle.sh
```

An optional `inp.txt` argument belongs only to the standalone CLI. The web
flow receives records from the user page and does not depend on that file.

## Current scope limits

- The active route has four stages and is fixed in the control server.
- Inspection Agency fields from the appendix are reserved for a later route
  extension.
- Digital signatures and third-party verification are deferred.
- Public-chain, private-chain, cross-chain, and external gateway work is
  outside this prototype stage.
- `MemoryPool` and `ConMemPool` retain their standalone experiment entry
  points, and their allocator interfaces are also used by the control-server
  ThreadPool.
