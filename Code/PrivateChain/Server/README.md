# Supply Chain User and Control Servers

The Server directory contains two independent C++17 HTTP servers:

```
User browser -> user_server :8080
             -> control_server :8081/api/auth/login
             -> control_server :8081/api/records
             -> control_server worker pool
             -> ECDSA P-256 signature verification
             -> MerkleTree append and proof verification
             -> SQLite block, batch, attachment, and edge records

Control browser -> control_server :8081
                -> preset workflow Canvas
                -> saved batch chains and CID references
```

user_server serves the user-facing static page. control_server owns
authentication, SQLite, ECDSA P-256 verification, one independent Merkle Tree
per block, IPFS forwarding, block creation, the worker pool, and the control
page.

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
Code/PrivateChain/Database/supply_chain.db
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

The per-block Merkle build, block numbering, parent selection, and database
write path remain serialized. Static requests and independent IPFS requests can
use different workers. Large file bodies continue through the normal IPFS
upload path.

The control-server build must include:

```
PrivateChain/Server/thread_pool.cpp
PrivateChain/MemoryPool/mempool.cpp
PrivateChain/ConMemPool/concurrency_mempool.cpp
```

The standalone allocator tests remain outside the control-server target.

## Fixed workflow

The current route is fixed:

```
Supplier -> Logistics -> Warehouse -> Supermarket
```

The server accepts a new block only when the authenticated role is the next
stage for that batch. Supplier creates the batch master data. The server
generates the batch ID from the normalized product name and a product-specific
four-digit sequence. Later stages select an existing batch and inherit its
product, harvest date, farm location, certificate ID, and generated batch ID.
Supermarket completes the route.

Examples:

```
BATCH-POTATO-0001
BATCH-POTATO-0002
BATCH-EGGPLANT-0001
```

Event identifiers use short fixed prefixes with four-digit numeric suffixes:

```
CERT-0001       SHIP-0001       STORAGE-0001
VEHICLE-0001    CONTAINER-0001  ZONE-0001
RACK-0001       STORE-0001
```

The same shipment ID can appear in multiple batch records when one shipment
carries multiple batches. The ID format is validated by the server; shipment
and storage identifiers are not encoded with a batch ID.

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

POST /api/auth/login accepts username, password, and an optional `remember`
field. It returns a Bearer token with the authenticated UID, role, and
organization.

When `remember=true`, the server stores only a SHA-256 hash of the token in
`auth_sessions` for 30 days. Without it, the session remains in memory for up
to eight hours and is lost when the control server stops.

GET /api/auth/me validates a token.

POST /api/auth/logout invalidates a token.

### Confirmation policy

GET /api/confirmation-policy returns all four role policies to an administrator.
An authenticated route user receives only the policy for that user's role.

POST /api/confirmation-policy is restricted to the administrator and accepts
role-prefixed `TypedName`, `Handwritten`, and `Face` fields for Supplier,
Logistics, Warehouse, and Supermarket. At least one confirmation method must
remain enabled for each role.

GET /api/confirmation/challenge creates a short-lived, single-use challenge
for the authenticated user.

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

- product for a new Supplier batch;
- batchId for Logistics, Warehouse, and Supermarket continuation events;
- the role-specific event fields;
- confirmed=true;
- optional ipfsRefs;
- confirmationMethod, confirmationName, and confirmationChallenge;
- ECDSA-P256-SHA256 signature, public key, and signed payload;
- an Authorization Bearer token.

The server derives the stage, UID, confirmer, organization, and a new Supplier
batch ID from the authenticated session and submitted product. The browser
cannot choose the stage or create a batch ID.

Every route role must select a method enabled by its own policy. The typed-name
demo path checks the entered name against the account display name, verifies
the signed payload and one-time challenge with OpenSSL, and only then creates
the Merkle block.

The response includes the new block ID, verification status, batch ID, next
stage, and CID count.

### Control endpoints

GET /api/workflow returns the fixed route for the Canvas.

GET /api/chains returns saved records and block edges for the administrator
control page.

GET /api/records returns saved verification records for the administrator.

## SQLite schema

The control server creates or upgrades these tables:

The v6 schema stores batch master data, blocks, edges, Merkle leaves,
attachments, users, persistent sessions, and per-role confirmation policies.
The v5 global confirmation policy is migrated to separate Supplier, Logistics,
Warehouse, and Supermarket rows without changing stored business records.

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
    inherited batch data
    event_data
    canonical_record
    root_hash
    verified
    block_hash
    chain_status
    confirmation_method
    confirmation_name
    signature_algorithm
    signature
    signature_public_key_hash
    signed_payload_hash
    signature_verified
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

users
    uid
    username
    display_name
    public_key
    role
    organization_id

confirmation_policy
    role
    typed_name
    handwritten
    face
    updated_by_uid
    updated_at
```

Every block has its own Merkle Tree. Its canonical input covers the batch master
data, role event data, sorted CID references, the parent block ID/hash, and
authenticated identity fields and signature metadata. The block root authenticates the internal tree;
the block hash includes that root and the parent block hash, creating the outer
linked block chain.

## Authentication

The five local demonstration accounts are documented in the repository-level
README. Passwords are stored as salted PBKDF2 hashes. Both pages provide logout
and an unchecked remember-me option. Persistent sessions store token hashes in
SQLite and survive control-server restarts for 30 days; temporary sessions
remain in memory for up to eight hours.

## Scope limits

- The active route is fixed to four stages.
- ECDSA P-256 typed-name confirmation is implemented. Handwritten capture, face
  capture, Inspection Agency, and third-party verification are deferred.
- Public snapshot generation, public-chain anchoring, cross-chain relaying,
  and external gateway work are outside this module.
- MerkleTree is used as an existing library and remains unchanged.
