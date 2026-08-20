# Public Chain and Customer Trace

This module owns the local EVM gateway, publication boundary, persisted public
Manifests, and customer-facing trace page. It does not read the private SQLite
database or apply private disclosure policy. It asks the private control server
for the current route fingerprint so an old publication cannot remain visible
after the private route changes.

The Snapshot module supplies one self-contained publication candidate. This
module independently verifies that candidate before submitting an EVM
transaction.

## Runtime flow

```text
Administrator :8081
        |
        | Publish to Local Public Chain
        v
PrivateChain administrator API
        |
        | internal publication token
        v
PublicChain service :8082/api/publish
        |
        +-- verify raw IDs and Keccak hashes
        +-- verify exact canonical Manifest hash
        +-- rebuild SHA-256 Public Merkle Root
        +-- compare the candidate route fingerprint with the current private route
        +-- submit SnapshotGateway transaction
        +-- save public Manifest beside chain metadata
        +-- generate a QR Code for the exact published Snapshot
        v
QR display :8084/
        |
        +-- show only one QR image for the current active Snapshot
        +-- expose no batch selector, link, metadata, or extra controls
        +-- refresh the QR image automatically
        v
Customer :8082/
        |
        +-- normal page: choose a published product batch and view the full trace
        +-- QR URL: ?snapshot=<id>&view=verification
        +-- QR view: Verification Result and compact selectable Trace Route
        +-- read active snapshot from SnapshotGateway
        +-- load matching public Manifest
        +-- compare the publication route fingerprint with the current private route
        +-- repeat all candidate and chain checks
        +-- display the normal public route and evidence CIDs
```

The customer page cannot access the private database, participant credentials,
signatures, private Merkle leaves, or excluded attachment metadata.

## Layout

```text
PublicChain/
├── contracts/
│   ├── SnapshotGateway.sol
│   └── SnapshotGateway.t.sol
├── consumer/
│   ├── css/style.css
│   ├── js/main.js
│   └── index.html
├── qr-display/                     # QR display page served on :8084
│   ├── css/style.css
│   ├── js/main.js
│   └── index.html
├── deployments/                    # Generated local deployment records
├── public-manifests/               # Generated customer-safe publications
├── public-qrcodes/                 # Generated Snapshot QR Code PNG files
├── scripts/
│   ├── deploy.js
│   ├── publication.js
│   ├── publish_snapshot.js
│   ├── query_snapshot.js
│   └── runtime.js
├── test/
│   ├── Publication.test.js
│   └── SnapshotGateway.test.js
├── consumer_server.js
├── qr_display_server.js
├── hardhat.config.js
├── package.json
└── README.md
```

Generated deployment records, public Manifest files, and QR Code PNG files are
ignored by Git. They remain local runtime state.

`SnapshotQRCode` contains the independent generator source and build target.
Generated PNG files are written to `PublicChain/public-qrcodes/`; they are not
expected inside `SnapshotQRCode`.

## Apple Silicon environment

The current development machine is an Apple M3 Mac with 18 GB memory. This is
more than sufficient for the local Hardhat node, publisher, customer page, and
two C++ servers. Hardhat is CPU-only for this workflow.

Use an ARM64 build of Node.js 22 LTS. Existing global Hardhat installations are
unnecessary because the project pins a local version.

With Homebrew:

```bash
brew install node@22
echo 'export PATH="/opt/homebrew/opt/node@22/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
node --version
npm --version
```

## Install, compile, and test

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/PublicChain"
npm install
npm run compile
npm test
```

The test suites cover contract permissions and lifecycle behavior plus
publication-candidate hashes, canonical Manifest verification, and the
duplicate-last Public Merkle Tree rule.

## Run the integrated local flow

After installing dependencies, start the customer-facing service from the
project root:

```bash
./start_customer_server.sh
```

The script builds the independent `Code/SnapshotQRCode` C generator, checks
port 8545, starts a Hardhat node in the background when needed, deploys
SnapshotGateway after a fresh node start, starts the QR display service on
port 8084, and then starts the independent customer/publication service on
port 8082. Existing Hardhat and QR display processes are reused.

The customer page is available at:

```text
http://127.0.0.1:8082/
```

The QR display page is available at:

```text
http://127.0.0.1:8084/
```

Keep the C++ control server on port 8081 using `./start_control_server.sh`. Log in as the administrator,
generate a snapshot preview for a completed batch, and select **Publish to
Local Public Chain**. A successful response includes the EVM block number.
Open `http://127.0.0.1:8084/` to display one QR Code for the active published
Snapshot. Scanning it opens the verification result section on the customer
service at `http://127.0.0.1:8082/`; the normal customer page remains available
at the same address for full route and evidence details. The QR display page
itself contains only the QR image.

The QR display page loads the current active Snapshot QR Code from the PublicChain
service and refreshes it automatically. The QR link remains locked to its
Snapshot ID, so a route edit or a newer published Snapshot shows the old link as
inactive. A scanned QR link uses the verification-only customer view; it shows
the Verification Result, a compact selectable route overview with numbered
stages, and the selected stage details. Public evidence and technical
verification details stay hidden from the QR view. The normal customer page
continues to show the full route and public evidence.
The customer page listens to the private control server's server-sent event
stream and updates this state without a full page refresh.

For a phone on the same local network, run the same customer start command.
The service listens on the LAN by default and automatically detects a private
IPv4 address for the QR URL:

```bash
./start_customer_server.sh
```

Open `http://<Mac-LAN-IP>:8084/` to display the QR Code. The encoded customer
URL uses port 8082 and opens the verification result view. If the computer has
multiple network interfaces, override
the detected address with `CONSUMER_PUBLIC_URL=http://<Mac-LAN-IP>:8082`.

The low-level `publish:local` and `query:local` commands remain contract smoke
tools. The integrated customer flow should use the administrator Publish button
so the exact public Manifest is retained for later verification.

## Configuration

Copy `.env.example` to `.env` only when overriding defaults.

| Variable | Default purpose |
| --- | --- |
| `PUBLIC_CHAIN_RPC_URL` | `http://127.0.0.1:8545` local JSON-RPC |
| `PUBLIC_CHAIN_ID` | `31337` expected destination chain |
| `SOURCE_NETWORK_NAME` | Approved private source-network name |
| `RELAYER_PRIVATE_KEY` | Optional signer; first local account by default |
| `CONSUMER_PORT` | `8082` customer and publication service |
| `CONSUMER_HOST` | `0.0.0.0` bind address |
| `CONSUMER_PUBLIC_URL` | Optional public base URL encoded into generated QR Codes; auto-detected when omitted |
| `QR_GENERATOR_BINARY` | Optional path to the compiled C QR generator |
| `QR_DISPLAY_PORT` | `8084` QR display page port |
| `QR_DISPLAY_HOST` | `0.0.0.0` QR display bind address |
| `CONSUMER_INTERNAL_URL` | `http://127.0.0.1:8082` internal QR display-to-customer URL |
| `PRIVATE_CONTROL_SERVER_URL` | `http://127.0.0.1:8081` current private-route state used to validate public snapshots |
| `IPFS_API_URL` | `http://127.0.0.1:5002` private-chain file-upload API |
| `PUBLIC_CHAIN_PUBLICATION_TOKEN` | Internal control-to-publisher token |
| `SNAPSHOT_PAYLOAD_PATH` | Low-level Gateway Payload smoke-test input |
| `QUERY_BATCH_ID` | Low-level query Batch ID |
| `QUERY_SNAPSHOT_HASH` | Optional exact snapshot hash |

If `PUBLIC_CHAIN_PUBLICATION_TOKEN` is changed, launch `control_server` with
the same value. The token protects the internal local publication endpoint from
ordinary browser requests. Production deployment requires proper service
authentication and secret management.

Hardhat exposes funded development accounts on its local node. These accounts
and private keys are public test fixtures and must never be reused on a real
network.

## Verification boundary

Before publication, `scripts/publication.js` verifies:

- protocol, snapshot, and batch ID hashes;
- exact canonical Manifest bytes and Keccak-256 Manifest hash;
- all ordered public fields and the SHA-256 Public Merkle Root;
- the Manifest route fingerprint and its match with the current private route;
- bytes32 formatting for the source private-block anchor; and
- the deployed gateway source-network allowlist.

The Solidity gateway then validates publisher authority, protocol and schema
version, destination chain, source network, nonzero anchors, unique snapshot
IDs, source-network nonce replay, pause state, and recalled-batch state.

Customer lookup repeats the candidate checks and compares the Manifest anchors
against the active on-chain record. A trace is marked Verified only when every
check passes and the contract status is Active.

## Contract boundary

The contract stores compact verification anchors:

- snapshot and batch identifier hashes;
- Public Merkle Root;
- exact Manifest hash;
- final private block hash;
- source network, destination chain, nonce, schema version, and publisher;
- Active, Superseded, Recalled, or Revoked state.

Public Manifest field selection remains in the C++ Snapshot module. The EVM
contract never receives participant passwords, private signatures, private
Merkle leaves, or raw files.

## Deferred work

- durable publication jobs and nonce allocation;
- wallet and production key custody;
- retry and transaction-receipt persistence;
- public testnet deployment;
- optional cross-chain messaging protocol adapters.
