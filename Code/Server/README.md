# Supply Chain User and Control Servers

The project builds two independent C++17 HTTP servers:

```text
User browser -> login control_server :8081/api/auth/login
             -> POST control_server :8081/api/records
             -> MerkleTree::Append
             -> generate and verify proof
             -> save block and chain edge in Code/Database/supply_chain.db

Control browser -> control_server :8081
                -> GET /api/chains
                -> read SQLite workflow graph
```

`user_server` only serves the user confirmation page. `control_server` owns the
Merkle Tree, authentication, SQLite writes, verification API, and control page.
On startup, the control server reads records in `block_id` order and rebuilds
the in-memory Merkle Tree.

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
- `uid`
- `role`
- `organization_id`
- `canonical_record`
- `root_hash`
- `proof`
- `verified`
- `block_hash`
- `chain_status`
- `created_at`

`root_hash` and `proof` are snapshots captured when each record is submitted.
As later blocks are appended, the current Merkle root can change while these
historical snapshots remain stored.

The `block_edges` table stores the connections between blocks:

- `from_block_id`
- `to_block_id`
- `batch_id`
- `relation`

The current preset route is:

```text
Supplier -> Logistics -> Warehouse -> Supermarket
```

The server accepts a new block only when the authenticated account matches the
next stage in this route. A new batch must start at Supplier, and Supermarket is
the terminal stage. Each accepted block after the first one is connected to the
previous block for the same batch through `block_edges`.

The control page receives this route from `GET /api/workflow` and draws it on a
Canvas. Route editing, branching, and merging are reserved for a later
workflow editor.

## API

The API is provided by `control_server` on port `8081`.

### Authentication

`POST /api/auth/login` accepts:

- `username`
- `password`

The response contains a temporary Bearer token and the authenticated user's
UID, role, and organization. Tokens expire after eight hours and are held in
the control server's memory.

`POST /api/auth/logout` invalidates the current Bearer token. Both the user and
control pages expose a logout button. Both login forms include an unchecked
remember-me option; the browser stores the session persistently only when the
option is selected.

The local demo accounts and credentials are documented in the repository-level
README. These credentials are for local development only. Passwords are stored
as salted PBKDF2 hashes in SQLite.

`POST /api/records` accepts `application/x-www-form-urlencoded` fields:

- `batchId`
- `product`
- `origin`
- `confirmed=true`

It also requires an `Authorization: Bearer <token>` header. The server obtains
the confirmer, UID, role, organization, and stage from the authenticated
session. The browser cannot choose the stage.

Successful response:

```json
{
  "blockID": 0,
  "verified": true,
  "chainStatus": "in_progress",
  "stage": "supplier"
}
```

`GET /api/records` requires an admin token and returns all stored records for
the control page, including the submitted fields, identity information, block
ID, root hash, proof, verification status, and submission time.

`GET /api/chains` requires an admin token and returns the same block records
together with the `block_edges` connections used by the control workflow view.

`GET /api/workflow` requires an admin token and returns the preset route nodes
and edges used by the Canvas view.
