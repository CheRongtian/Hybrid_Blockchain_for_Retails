# Public Chain and Customer Trace

This module owns the local EVM gateway, publication boundary, persisted public
Manifests, and customer-facing trace page. It does not read the private SQLite
database or apply private disclosure policy.

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
        +-- submit SnapshotGateway transaction
        +-- save public Manifest beside chain metadata
        v
Customer :8082/
        |
        +-- choose a published product batch
        +-- read active snapshot from SnapshotGateway
        +-- load matching public Manifest
        +-- repeat all candidate and chain checks
        +-- display public product route and evidence CIDs
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
├── deployments/                    # Generated local deployment records
├── public-manifests/               # Generated customer-safe publications
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
├── hardhat.config.js
├── package.json
└── README.md
```

Generated deployment records and public Manifest files are ignored by Git.
They remain local runtime state.

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

The script checks port 8545, starts a Hardhat node in the background when
needed, deploys SnapshotGateway after a fresh node start, and then starts the
independent customer/publication service. If a Hardhat node is already
running, it is reused without another deployment.

The customer page is available at:

```text
http://127.0.0.1:8082/
```

Keep the C++ control server on port 8081 using `./start_control_server.sh`. Log in as the administrator,
generate a snapshot preview for a completed batch, and select **Publish to
Local Public Chain**. A successful response includes the EVM block number. The
customer page remains an independent service at `http://127.0.0.1:8082/` and is
opened separately when needed.

The customer page loads its published-batch selection list from the PublicChain
service. The customer clicks a product batch, and the page loads that batch's
public trace. Batch IDs are not typed into the customer page. QR-code behavior
is outside the current scope.

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
- QR-code entry into the existing customer page; and
- optional cross-chain messaging protocol adapters.
