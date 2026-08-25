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
                -> Snapshot preview and Publish action
                -> PublicChain service :8082/api/publish
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

From the project root, start each business-facing service independently:

```bash
./start_user_server.sh
```

```bash
./start_control_server.sh
```

Open:

```
User page:    http://127.0.0.1:8080/
Control page: http://127.0.0.1:8081/
```

The root scripts locate the binaries under `Code/build/Server` and ensure the
Homebrew Kubo service is running on IPFS API port 5002. They do not start the
Hardhat node or the customer trace service. Direct binary execution remains
available for low-level development:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/build"
./Server/control_server
./Server/user_server
```

The local application ports are:

```text
8080  participant submission page
8081  administrator control page
8082  customer trace page and PublicChain publication API
8545  local Hardhat JSON-RPC node
5002  local Kubo IPFS API
```

The default database path is:

```
Storage/Database/supply_chain.db
```

The control server also accepts:

```
./control_server [port] [static_directory] [database_path]
```

Set `SUPPLY_CHAIN_STORAGE_ROOT` to override the repository-level `Storage/`
root. Passing an explicit database path keeps its Snapshot archive beside that
custom database.

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

## Configurable workflow

The initial route template is:

```
Supplier -> Logistics -> Warehouse -> Supermarket
```

The administrator can edit this template or select an existing batch and edit
a batch-specific route from the control Canvas. Canvas changes are synchronized
automatically as route revisions, including temporarily incomplete drafts. A
valid route is a connected sequence that starts with Supplier and ends with
Supermarket. It may contain repeated Logistics and Warehouse nodes, or connect
Supplier directly to Supermarket. The server accepts a new block only when the
authenticated role and username match the next node on that batch's active
route revision. Each route node is assigned to one active account whose role
matches the node type. The same account cannot occupy multiple nodes in one
route.

The Canvas supports the following editing actions:

- drag a route node to set its position;
- drag the output handle on one node to the input handle on another node;
- click a connection and choose **Remove connection**, or press Delete;
- drag the empty Canvas background or use a two-finger scroll to pan the route;
- use trackpad pinch, **+**, **-**, or the wheel to zoom, and **Fit route** to recenter it;
- add an unconnected Logistics or Warehouse node in an available area of the
  current viewport, assign an unused matching account, then drag and connect it
  manually; existing connections, node positions, canvas size, pan, and zoom
  remain unchanged;
- use **Undo** and **Redo**, including Command/Ctrl+Z and Command/Ctrl+Y;
- choose **Auto arrange** to rebuild a compact left-to-right layout; Canvas
  edits synchronize automatically, including temporarily incomplete drafts.

The Canvas is a lightweight static SVG/DOM editor. It does not add a frontend
framework or a separate graph database. Node positions and connections continue
to use the existing `/api/workflow` endpoint and SQLite records. Every route
node has a stable ID. Adding, deleting, or reconnecting a node creates or
reuses a route revision and invalidates the current Snapshot immediately;
already-created Blocks remain immutable historical records. A draft may contain
an unconnected node while the administrator finishes the route. Such a node is
excluded from the lower chain preview. After it is connected into a complete
Supplier-to-Supermarket path, it appears as a pending stage; preview arrows
appear only after both endpoint events have verified Blocks. Auto arrangement
and **Fit route** are explicit view-changing actions. New nodes are
intentionally free-positioned so the administrator can place them without
forcing the route into a single horizontal row.

The browser reports duplicate connections, invalid endpoints, cycles, and
disconnected nodes immediately. Draft synchronization validates node, account,
and edge references. The server requires one complete Supplier-to-Supermarket
path before a participant can submit an event or a Snapshot can be generated.
If a route changes after publication, the old Snapshot is immediately hidden
from the current customer trace until the route is restored exactly or a new
Snapshot is published.

Supplier creates the batch master data. The server generates the batch ID from
the normalized product name and a product-specific four-digit sequence. Later
stages select an existing batch and inherit its product, harvest date, farm
location, certificate ID, and generated batch ID. Supermarket completes the
route.

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

The user page derives shipment, vehicle/container, storage-lot, and zone/rack
identifiers from the matching role's order within the selected batch route. The
first Logistics stage receives `SHIP-0001` and `VEHICLE-0001`, the second
receives `SHIP-0002` and `VEHICLE-0002`, and so on. These route-controlled
values are verified again by the server when the event is submitted.

The same shipment ID can appear in multiple batch records when one shipment
carries multiple batches. The ID format is validated by the server; shipment
and storage identifiers are not encoded with a batch ID.

The route topology is separate from transport event facts. Logistics records
shipment and vehicle identifiers as event data. Its delivery location is
derived from the next configured route node. The same shipment or vehicle can
be linked to records from multiple batches.

## Role-specific event fields

The current user page presents fields for the authenticated role:

| Role | Fields |
| --- | --- |
| Supplier | Harvest Date, Farm Location, Certificate ID |
| Logistics | Shipment ID, Pickup Location, Delivery Location, Departure/Arrival Time, Temperature/Humidity Summary, Vehicle/Container ID |
| Warehouse | Storage Lot ID, Inbound/Outbound Time, Temperature/Humidity Summary, Storage Zone/Rack ID |
| Supermarket | Shelf Placement Date, Expiration/Sell-by Date, Store Location ID |

The current route editor supports Supplier, Logistics, Warehouse, and
Supermarket. It does not yet add the Inspection Agency role from the appendix.

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

Kubo is shared by both private-chain servers and does not require another
terminal. If Homebrew reports `launchctl bootstrap ... exit 5`, inspect the
service and listener before restarting it:

```bash
brew services list
ps aux | rg '[i]pfs daemon'
lsof -nP -iTCP:5002 -sTCP:LISTEN
```

If a Kubo daemon is already listening on port 5002, reuse it. The launch scripts
check the Homebrew service state before starting, so a stale service entry can
still produce a `launchctl bootstrap ... exit 5` message. In that case, inspect
the listener first; do not start a second daemon on the same API port:

```bash
lsof -nP -iTCP:5002 -sTCP:LISTEN
ipfs id
```

If `ipfs id` works and port 5002 is listening, leave that daemon running and
start the requested C++ server directly. If no daemon is listening, repair the
Homebrew service registration with `brew services restart kubo`, then confirm
port 5002 before starting the server.

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

The root user and control launch scripts check the Kubo service before starting
their C++ server. This project does not implement the IPFS protocol.

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

GET /api/confirmation-policy returns policies for the connected nodes on the
current route. The control panel edits the policy by route node and assigned
account, with one row per connected node. Every connected node must keep at
least one method enabled; Typed Name is selected by default. An authenticated
route user receives the policy for the exact route node assigned to that user.

Typed-name confirmation is implemented in this demo. Handwritten and face
confirmation can be enabled as configuration options, but their capture and
verification flows remain deferred.

GET /api/confirmation/challenge creates a short-lived, single-use challenge
for the authenticated user.

### Batch selection

GET /api/batches returns batch master data, the next required route stage, its
stable route-node ID, the active route revision, and the username assigned to
that stage. The user page only offers a batch when the role, assigned username,
route revision, and authenticated account match.

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

Every connected route node must select a method enabled by its node policy. The
typed-name demo path checks the entered name against the account display name,
verifies the signed payload and one-time challenge with OpenSSL, and only then
creates the Merkle block. The server rechecks the route ID, stable node ID,
role, account, next-stage position, and route-controlled identifiers at commit
time.

The response includes the new block ID, verification status, batch ID, next
stage, and CID count.

### Control endpoints

GET /api/workflow returns the default route or the route selected by batchId.

POST /api/workflow validates and stores the Canvas node/edge sequence. The
control page sends `draft=true` while the administrator is editing, so node
addition, deletion, reconnection, and incomplete intermediate states can be
synchronized automatically. Without that flag, the complete route rules are
required. An empty batchId updates the default route; a batchId creates or
updates that batch's active route revision. Semantic changes invalidate the
current Snapshot and publish a live `route_changed` event.

GET /api/events keeps a server-sent event stream open. It reports route changes,
new verified batch records, and successful Snapshot publication through
`route_changed`, `batch_changed`, `snapshot_checked`, and `snapshot_published`
messages. The
control page uses this stream directly. The PublicChain customer service proxies
it through port 8082 so phone clients receive automatic updates without direct
access to port 8081 or a full page refresh.

GET /api/chains returns saved records and block edges for the administrator
control page.

GET /api/records returns saved verification records for the administrator.

GET /api/snapshot/eligible-batches returns completed batches whose configured
route, parent links, Merkle results, and signatures pass the Snapshot
eligibility policy.

GET /api/snapshot/status?batchId=... is an internal token-protected endpoint.
It returns the current active Snapshot metadata and the latest local verification
time/status used by the public customer service.

POST /api/snapshot/preview accepts a batch ID and optional allowlisted CID
selection. It returns a consumer-safe Manifest, independent Public Merkle Root,
final private block hash, private-data exclusion summary, and a self-contained
publication candidate. The preview is stored as a local lifecycle revision and
is not published until the publish endpoint succeeds.

POST /api/snapshot/publish is restricted to the administrator. It forwards an
exact publication candidate to the independent PublicChain service. The
service revalidates every identifier hash, the canonical Manifest hash, and
the Public Merkle Root before submitting a transaction. The default internal
endpoint is:

```text
http://127.0.0.1:8082/api/publish
```

Optional environment variables are:

```text
PUBLIC_CHAIN_SERVICE_URL=http://127.0.0.1:8082
PUBLIC_CHAIN_PUBLICATION_TOKEN=local-publication-demo-token
```

The independent C++ SnapshotScheduler runs one check immediately after
control-server startup and then waits for the nearest due time from
SnapshotStorage. Each product uses a separate Snapshot refresh policy from the
`Consumer Data Preview` form; the default product period is 3600 seconds
(1 hour), and the editor accepts whole minutes, hours, or days. The same compact
row requires a batch `Available from` and `Available until` value at
whole-minute precision. Route, block, publication, and schedule changes wake
the scheduler so it can recalculate the due time. Before the start, the batch is
hidden from customers; at the end, no further refresh is scheduled and the last
published Snapshot remains visible as a frozen off-shelf record. Administrators
can preview and publish before the start time.
Unchanged source hashes update the hot verification status and append an audit
history row without creating a new Snapshot.
Changed completed source data creates and publishes a new immutable revision.
An invalidated route publishes its replacement after every connected stage has
a verified Block.

Use the same non-default token in both the control-server and PublicChain
service environments when overriding the local demonstration value.

## SQLite schema

The control server creates or upgrades these tables:

The v8 schema stores batch master data, blocks, edges, Merkle leaves,
attachments, users, persistent sessions, route-node confirmation policies,
route definitions, route nodes, route edges, and transport-to-batch links.
The legacy role-level `confirmation_policy` table remains for defaults and
backward-compatible migration; active route validation uses
`route_node_confirmation_policy`.

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
    route_id

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
    route_id
    route_node_id
    route_step_index
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

route_node_confirmation_policy
    route_id
    node_id
    node_label
    role
    username
    typed_name
    handwritten
    face
    updated_by_uid
    updated_at

route_definitions
    route_id
    batch_id
    name
    is_default
    created_at
    updated_at

route_nodes
    route_id
    node_id
    node_type
    label
    role
    username
    position_x
    position_y
    step_index

route_edges
    route_id
    from_node_id
    to_node_id

transport_batch_links
    shipment_id
    vehicle_container_id
    batch_id
    block_id
```

Every block has its own Merkle Tree. Its canonical input covers the batch master
data, role event data, sorted CID references, the parent block ID/hash, and
authenticated identity fields and signature metadata. The block root authenticates the internal tree;
the block hash includes that root and the parent block hash, creating the outer
linked block chain.

## Authentication

The local demonstration accounts are documented in the repository-level
README. Passwords are stored as salted PBKDF2 hashes. Both pages provide logout
and an unchecked remember-me option. Persistent sessions store token hashes in
SQLite and survive control-server restarts for 30 days; temporary sessions
remain in memory for up to eight hours.

## Scope limits

- Route editing currently supports a linear per-batch sequence with Supplier
  and Supermarket as required endpoints. Route revisions may be added,
  removed, or reconnected after Blocks exist; historical Blocks remain
  immutable and the active Snapshot is invalidated until the new route is
  complete and republished.
- ECDSA P-256 typed-name confirmation is implemented. Handwritten capture, face
  capture, Inspection Agency, and third-party verification are deferred.
- Public snapshot preview generation is provided through the sibling Snapshot
  library. The administrator publish endpoint forwards the verified candidate
  to PublicChain for local publication; durable relayer jobs, public-chain
  anchoring beyond the local gateway, cross-chain relaying, and external
  gateway work remain outside this module.
- MerkleTree is used as an existing library and remains unchanged.
