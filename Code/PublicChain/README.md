# Public Chain Prototype

This module runs the first local EVM publication loop for approved public
snapshots. It owns the Solidity gateway, Hardhat network configuration,
contract deployment, snapshot submission, and on-chain queries.

The Snapshot module remains responsible for disclosure filtering, canonical
Manifest generation, and the independent Public Merkle Root.

## Current flow

```text
Code/Snapshot gateway payload
        |
        v
Local relayer script
        |
        v
SnapshotGateway.sol on Hardhat EVM
        |
        +-- transaction hash and block number
        +-- current snapshot per batch
        +-- ordered snapshot history
        +-- recall and revocation state
```

The local scripts do not send data to a public testnet or mainnet.

## Layout

```text
PublicChain/
├── contracts/
│   ├── SnapshotGateway.sol
│   └── SnapshotGateway.t.sol
├── deployments/
│   └── .gitkeep
├── scripts/
│   ├── deploy.js
│   ├── publish_snapshot.js
│   ├── query_snapshot.js
│   └── runtime.js
├── test/
│   └── SnapshotGateway.test.js
├── .env.example
├── .gitignore
├── hardhat.config.js
├── package.json
└── README.md
```

`SnapshotGateway.sol` has one source location in this module. The historical
`Code/PrCsample.sol` and `Code/SNsample.sol` files remain reference examples.

## Apple Silicon environment

The current development machine is an Apple M3 Mac with 18 GB memory. This is
more than sufficient for one local Hardhat node and the two C++ demo servers.
Hardhat is CPU-only for this workflow and does not use the Apple GPU.

Use an ARM64 build of Node.js 22 LTS. Node.js 25 is outside Hardhat 3's
supported LTS range and produces a compatibility warning.

With Homebrew:

```bash
brew install node@22
echo 'export PATH="/opt/homebrew/opt/node@22/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
node --version
npm --version
```

The expected Node version begins with `v22`. Existing global Hardhat installs
are unnecessary because this module uses the version pinned in `package.json`.

## Install project dependencies

From this directory:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/PublicChain"
npm install
```

This creates `node_modules` and a package lock. Copy `.env.example` to `.env`
only when overriding defaults. Generated Hardhat artifacts, cache, deployments,
`.env`, and dependencies are ignored where appropriate.

## Compile and test

```bash
npm run compile
npm test
```

The JavaScript integration tests cover publication, permissions, duplicate
snapshot IDs, nonce replay, input validation, replacement history, recall,
revocation, and pause behavior. The `.t.sol` file keeps the self-contained
Solidity test harness beside the contract.

## Run the local EVM loop

Use three terminal sessions in `Code/PublicChain`.

Terminal 1 starts the local chain and must remain open:

```bash
npm run node
```

Terminal 2 deploys the contract, enables the source network, grants an optional
relayer, and writes `deployments/31337.json`:

```bash
npm run deploy:local
```

Terminal 2 can then publish the example Gateway Payload:

```bash
npm run publish:local
```

Terminal 3 queries the current snapshot and batch history:

```bash
npm run query:local
```

The example payload targets chain ID `31337` and can be published once per
fresh local-chain deployment. Repeating it correctly triggers duplicate ID or
nonce protection.

## Configuration

Optional settings are documented in `.env.example`:

| Variable | Default purpose |
| --- | --- |
| `PUBLIC_CHAIN_RPC_URL` | Local JSON-RPC endpoint |
| `PUBLIC_CHAIN_ID` | Expected local destination chain |
| `SOURCE_NETWORK_NAME` | Name hashed into the approved source network ID |
| `RELAYER_PRIVATE_KEY` | Optional signer key; local unlocked account by default |
| `SNAPSHOT_PAYLOAD_PATH` | Gateway Payload JSON to publish |
| `QUERY_BATCH_ID` | Raw batch ID to query |
| `QUERY_SNAPSHOT_HASH` | Optional exact snapshot hash to query |

Hardhat automatically exposes funded development accounts on its local node.
With an empty `RELAYER_PRIVATE_KEY`, deployment and publication use its first
unlocked account. Any configured key becomes the actual transaction signer and
the deployer receives publisher permission in the contract constructor. Local
development accounts and their private keys are public test fixtures and must
never be used on a real network.

## Contract boundary

The contract stores compact verification anchors:

- snapshot and batch identifier hashes;
- Public Merkle Root;
- exact Manifest hash;
- final private block hash;
- source network, destination chain, nonce, schema version, and publisher;
- Active, Superseded, Recalled, or Revoked state.

It validates authorized publishers, approved source networks, protocol/version,
target chain, unique snapshot IDs, and source-network nonces. Public Manifest
field selection stays in C++ so the on-chain gateway has one clear admission
responsibility.

## Deferred work

- control-server relayer endpoint and durable nonce allocation;
- control-page publication status and transaction details;
- wallet and production key custody;
- public testnet deployment;
- consumer query API and QR page; and
- optional cross-chain messaging protocol adapters.
