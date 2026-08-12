# Public Chain Integration

This directory is reserved for anchoring approved public snapshots to an EVM
network and exposing consumer trace information. The module is planned and has
no deployable contract or client integration yet.

## Purpose

```text
Public Snapshot
      |
      | transaction
      v
EVM snapshot contract
      |
      +-- immutable root and publication metadata
      +-- transaction hash and block timestamp
      +-- consumer query reference
      |
      v
QR trace page -> manifest CID -> recomputed root -> on-chain comparison
```

The public blockchain should store compact verification anchors. Detailed
consumer-visible content belongs in the public IPFS manifest referenced by the
contract.

## Planned layout

```text
PublicChain/
├── contracts/    # Versioned Solidity snapshot anchor
├── deployment/   # Test-network deployment configuration and scripts
├── abi/          # Generated contract interfaces
├── consumer/     # Consumer QR trace and verification page
└── README.md
```

## Planned contract data

The first production-oriented contract is expected to anchor:

- snapshot ID;
- snapshot version or nonce;
- public snapshot root;
- public manifest CID;
- public batch reference;
- publisher address;
- on-chain timestamp.

Historical versions should remain queryable. Repeated, stale, or out-of-order
snapshot submissions must be rejected or handled explicitly.

## Consumer verification

The consumer page will resolve a QR URL or snapshot ID, read the public
manifest, reconstruct its root, and compare it with the root recorded by the
contract. Internal private-chain records will not be exposed directly.

## Solidity samples

`Code/SNsample.sol` is an early snapshot-anchor sketch that may inform the new
contract, but it is not deployable in its current form. `Code/PrCsample.sol` is
an older generic record-storage experiment and is outside the current runtime
architecture. Both files remain in the `Code` root as historical samples.

## Implementation status

Planned. Contract rewrite, EVM test-network deployment, transaction submission,
cross-chain relaying, and the consumer QR page have not been implemented.
