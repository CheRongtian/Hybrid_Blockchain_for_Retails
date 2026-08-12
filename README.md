# Hybrid-Chain Supply Chain Traceability Prototype

This repository is a C++17 supply-chain traceability prototype organized into
three architectural modules:

```text
Private Chain -> Public Snapshot -> Public Chain -> Consumer QR Verification
```

The private-chain module is currently runnable. Consumer-safe snapshot preview
generation is implemented locally. Snapshot publication and public-chain
anchoring are the next implementation stages.

## Current architecture

```text
Authenticated participant
          |
          | role-specific event and optional IPFS file
          v
PrivateChain
  +-- fixed supply-chain route
  +-- linked block chain per product batch
  +-- independent Merkle Tree inside every block
  +-- ECDSA P-256 confirmation
  +-- SQLite structured state
  +-- IPFS CID references
          |
          | approved public fields only
          v
Snapshot                         preview implemented
  +-- canonical public manifest
  +-- independent public root
  +-- selected existing evidence CIDs
          |
          v
PublicChain                      planned
  +-- EVM snapshot anchor
  +-- immutable publication metadata
  +-- consumer QR verification
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
│   ├── PublicChain/                 # EVM anchor and QR flow (planned)
│   ├── CMakeLists.txt               # Central C++ build entry
│   ├── PrCsample.sol                # Historical Solidity sample
│   ├── SNsample.sol                 # Early snapshot contract sample
│   └── QRCodeExample.html           # Historical QR sample
├── server_concurrency_test.py       # Allocator benchmark
└── README.md
```

Generated build output remains under `Code/build`. The private SQLite database
is stored at `Code/PrivateChain/Database/supply_chain.db` and is ignored by
Git.

## Module documentation

- [Private-chain overview](Code/PrivateChain/README.md)
- [User and control servers](Code/PrivateChain/Server/README.md)
- [Merkle Tree library and CLI](Code/PrivateChain/MerkleTree/README.md)
- [Database notes](Code/PrivateChain/Database/README.md)
- [Public snapshot design](Code/Snapshot/README.md)
- [Public-chain design](Code/PublicChain/README.md)

The module READMEs own implementation details, APIs, schemas, privacy rules,
and planned responsibilities. This README remains the project entry point.

## Implemented private-chain flow

The current route is fixed:

```text
Supplier -> Logistics -> Warehouse -> Supermarket
```

The Supplier creates a product batch. Each later participant selects an
existing batch when that participant's role is the next required stage. Batch
master data is inherited along the same route instead of being entered again.
The Supermarket is the required final stage.

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
- fixed route-order enforcement;
- generated and validated identifiers;
- per-role administrator confirmation policies;
- typed-name confirmation with browser ECDSA P-256 signing and C++ OpenSSL
  verification;
- per-block Merkle leaves, roots, proofs, and verification;
- linked parent block IDs and hashes;
- SQLite persistence for users, sessions, batches, blocks, leaves, edges,
  signatures, confirmation policies, and CID metadata;
- local Kubo/IPFS upload integration through returned CIDs;
- administrator chain and Merkle Tree visualization;
- completed-batch public Manifest and Public Root preview;
- bounded server worker pool using selected `MemoryPool` and `ConMemPool`
  components.

Handwritten-signature capture and face confirmation are policy placeholders.
Inspection Agency, saved/published snapshots, EVM deployment, relaying, and the
consumer QR page are pending.

## Requirements

- CMake 3.15 or newer;
- a C++17 compiler;
- OpenSSL;
- SQLite3;
- local Kubo/IPFS when file upload is used.

The current local Kubo API configuration expects:

```text
http://127.0.0.1:5002
```

## Build

Configure from the central `Code` directory. Reconfigure after pulling the
directory migration so CMake discovers the new source paths.

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

## Run the private-chain demo

Start the two servers from `Code/build` in separate terminals:

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

The IPFS daemon remains an external local service and must already be running
when attachments are submitted.

## Demonstration accounts

| Role | Username | Password |
| --- | --- | --- |
| Supplier | `supplier01` | `supplier123` |
| Logistics | `logistics01` | `logistics123` |
| Warehouse | `warehouse01` | `warehouse123` |
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

## Solidity samples and next stages

`Code/PrCsample.sol` is a historical generic record-storage experiment.
`Code/SNsample.sol` is an early snapshot-anchor sketch. Both remain unchanged
at the `Code` root for reference. The production snapshot format and public
contract will be implemented in `Code/Snapshot` and `Code/PublicChain` without
turning either sample directly into runtime code.

The next architectural milestone is snapshot publication: persist a reviewed
preview as a versioned snapshot, decide how its public Manifest is distributed,
and prepare the resulting anchor data for an EVM contract.
