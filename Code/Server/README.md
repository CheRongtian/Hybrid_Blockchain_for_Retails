# Supply Chain User and Control Servers

The Server directory contains two independent C++17 HTTP servers:

```
User browser -> user_server :8080
             -> control_server :8081/api/auth/login
             -> control_server :8081/api/records
             -> control_server worker pool
             -> MerkleTree append and proof verification
             -> SQLite block, batch, attachment, and edge records

Control browser -> control_server :8081
                -> preset workflow Canvas
                -> saved batch chains and CID references
```

user_server serves the user-facing static page. control_server owns
authentication, the in-memory Merkle Tree, SQLite, IPFS forwarding, block
creation, the worker pool, and the control page.

## Build

The project is configured from the Code directory:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

## Run

Start each server in its own terminal:

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

The default database path is:

```
Code/Database/supply_chain.db
```

The control server also accepts:

```
./control_server [port] [static_directory] [database_path]
```

## Worker pool and allocators

The control server accepts sockets on its main thread and dispatches request
handling to a bounded worker pool. The selected allocator components are used
at separate layers:

```
socket -> ThreadPool queue -> MemoryPool task node
                             -> ConMemPool callable storage
                             -> serialized SQLite and Merkle commit
```

The Merkle append, block numbering, parent selection, and database write path
remain serialized. Static requests and independent IPFS requests can use
different workers. Large file bodies continue through the normal IPFS upload
path.

The control-server build must include:

```
Server/thread_pool.cpp
MemoryPool/mempool.cpp
ConMemPool/concurrency_mempool.cpp
```

The standalone allocator tests remain outside the control-server target.

## Fixed workflow

The current route is fixed:

```
Supplier -> Logistics -> Warehouse -> Supermarket
```

The server accepts a new block only when the authenticated role is the next
stage for that batch. Supplier creates the batch master data. Later stages
select an existing batch and inherit its product, harvest date, farm location,
and certificate ID. Supermarket completes the route.

The route topology is separate from transport event facts. Logistics records
pickup and delivery locations inside its own event data; those fields do not
change the preset route.

## Role-specific event fields

The current user page presents fields for the authenticated role:

| Role | Fields |
| --- | --- |
| Supplier | Harvest Date, Farm Location, Certificate ID |
| Logistics | Shipment ID, Pickup Location, Delivery Location, Departure/Arrival Time, Temperature/Humidity Summary, Vehicle/Container ID |
| Warehouse | Storage Lot ID, Inbound/Outbound Time, Temperature/Humidity Summary, Storage Zone/Rack ID |
| Supermarket | Shelf Placement Date, Expiration/Sell-by Date, Store Location ID |

The active four-stage route does not yet add the Inspection Agency role from
the appendix.

## IPFS API

The server uses an existing local IPFS/Kubo node and writes the integration
layer in server.cpp. It does not implement the IPFS protocol.

The default local API endpoint is:

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

Use `IPFS_API_URL` only when a deployment uses a different IPFS host or port.

The user page uploads a selected file to POST /api/ipfs/files. The control
server forwards the file to the IPFS /api/v0/add API and returns the CID.
The following record submission sends the CID reference with the structured
event:

```
POST /api/records
Content-Type: application/x-www-form-urlencoded
Authorization: Bearer <token>
```

The file body is held by IPFS. SQLite stores the CID and file metadata. The
current local demo accepts files up to 30 MB per upload.

The IPFS daemon must be started separately. This project does not run or test
an external IPFS service automatically.

## API

All API endpoints below are provided by control_server on port 8081.

### Authentication

POST /api/auth/login accepts username and password and returns a temporary
Bearer token with the authenticated UID, role, and organization.

GET /api/auth/me validates a token.

POST /api/auth/logout invalidates a token.

### Batch selection

GET /api/batches returns batch master data and the next required route stage.
The user page uses this endpoint for Logistics, Warehouse, and Supermarket
selection.

### File upload

POST /api/ipfs/files accepts a multipart form with:

- category
- file

The response contains:

```json
{
  "category": "harvestPhotos",
  "cid": "bafy...",
  "filename": "harvest.jpg",
  "contentType": "image/jpeg",
  "size": 12345
}
```

### Record submission

POST /api/records requires:

- batchId;
- the role-specific event fields;
- confirmed=true;
- optional ipfsRefs;
- an Authorization Bearer token.

The server derives the stage, UID, confirmer, and organization from the
authenticated session. The browser cannot choose the stage.

The response includes the new block ID, verification status, batch ID, next
stage, and CID count.

### Control endpoints

GET /api/workflow returns the fixed route for the Canvas.

GET /api/chains returns saved records and block edges for the administrator
control page.

GET /api/records returns saved verification records for the administrator.

## SQLite schema

The control server creates:

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
    inherited batch data
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

The canonical Merkle record covers batch master data, role event data, sorted
CID references, the parent block ID/hash, and authenticated identity fields.

## Authentication

The five local demonstration accounts are documented in the repository-level
README. Passwords are stored as salted PBKDF2 hashes. Both pages provide
logout and an unchecked remember-me option.

## Scope limits

- The active route is fixed to four stages.
- Inspection Agency, digital signatures, and third-party verification are
  deferred.
- Public-chain, private-chain, cross-chain, and external gateway work is
  outside this stage.
- MerkleTree is used as an existing library and remains unchanged.
