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
        +-- batch master data and event data -> SQLite
        +-- large file upload -> local IPFS -> CID
        +-- batch data, event data, CID references, and parent link -> MerkleTree
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
│   │   ├── auth_utils.cpp/.hpp    # Password hashing and tokens
│   │   ├── db_utils.cpp/.hpp      # SQLite persistence
│   │   ├── static/                # User page
│   │   └── control_static/        # Control page
│   ├── Database/                  # Local SQLite database location
│   ├── MemoryPool/                # Memory-pool experiments
│   ├── ConMemPool/                # Concurrent allocator experiments
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

The control server owns authentication, SQLite, the in-memory Merkle Tree,
IPFS upload forwarding, block creation, and the control page. The user server
serves the user page and does not own the database.

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
    batch_id
    inherited batch fields
    event_data
    canonical_record
    root_hash
    proof
    verified
    block_hash
    chain_status
    created_at

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
```

The canonical Merkle input contains the batch master data, role-specific event
data, sorted CID references, the parent block ID/hash, and authenticated
identity fields. The existing Code/MerkleTree library is used without
modification.

## Control page

The control page has:

- administrator login;
- a Canvas view of the fixed route;
- a block flow for each batch;
- role, organization, inherited batch data, event data, and CID references;
- stored verification status and block connections.

Root and Proof remain server-side verification snapshots. A detailed click-open
verification dialog and red error path are reserved for a later UI iteration.

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
option. The control server stores salted PBKDF2 password hashes and keeps
Bearer sessions in memory.

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
- `MemoryPool` and `ConMemPool` remain standalone experiments.
