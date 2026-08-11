# Supply Chain User and Control Servers

The project builds two independent C++17 HTTP servers:

```text
User browser -> user_server :8080
             -> POST control_server :8081/api/records
             -> MerkleTree::Append
             -> generate and verify proof
             -> Code/Database/supply_chain.db

Control browser -> control_server :8081
                -> GET /api/records
                -> read SQLite records
```

`user_server` only serves the user confirmation page. `control_server` owns the
Merkle Tree, SQLite writes, verification API, and control page. On startup, the
control server reads records in `block_id` order and rebuilds the in-memory
Merkle Tree.

## Build from Code

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

## Run

Start the control server first, then the user server in a second terminal:

```bash
./build/Server/control_server
./build/Server/user_server
```

Open:

```text
User page:    http://127.0.0.1:8080/
Control page: http://127.0.0.1:8081/
```

Optional arguments:

```text
./control_server [port] [static_directory] [database_path]
./user_server [port] [static_directory]
```

The default database path is `Code/Database/supply_chain.db`, independent of
the terminal working directory. Files ending in `.db`, `.db-wal`, and `.db-shm`
are ignored by Git.

## Database

The `supply_chain_records` table contains:

- `id`
- `block_id`
- `batch_id`
- `product`
- `origin`
- `stage`
- `confirmed_by`
- `canonical_record`
- `root_hash`
- `proof`
- `verified`
- `created_at`

`root_hash` and `proof` are snapshots captured when each record is submitted.
As later blocks are appended, the current Merkle root can change while these
historical snapshots remain stored.

## API

The API is provided by `control_server` on port `8081`.

`POST /api/records` accepts `application/x-www-form-urlencoded` fields:

- `batchId`
- `product`
- `origin`
- `stage`
- `confirmedBy`
- `confirmed=true`

Successful response:

```json
{
  "blockID": 0,
  "verified": true
}
```

`GET /api/records` returns all stored records for the control page, including
the submitted fields, block ID, root hash, proof, verification status, and
submission time.
