# Public Snapshot Module

This directory is reserved for the boundary between the private supply-chain
system and a public blockchain. The module is planned and has no runtime source
code yet.

## Purpose

The private system contains operational data that should not automatically be
published. This module will select an approved consumer-visible subset and
produce a versioned, independently verifiable public snapshot.

```text
Private batch chain
        |
        | public-field policy
        v
Public Manifest
        |
        +-- Public Snapshot Root
        +-- Manifest CID
        +-- Snapshot ID and version
        +-- final private block hash reference
```

## Planned output

The first snapshot format is expected to contain:

- snapshot ID;
- snapshot version;
- batch ID or a public batch reference;
- product and approved trace fields;
- public manifest CID;
- public snapshot Merkle root;
- final private block hash reference;
- creation time;
- publication status.

The public root must be computed from the public manifest fields. It should not
reuse the internal block root when private leaves have been removed.

## Planned responsibilities

```text
Snapshot/
├── include/     # Snapshot data structures and public interfaces
├── src/         # Manifest selection, canonicalization, and root generation
└── README.md
```

The module will eventually:

1. load a completed batch from the private-side API or persistence layer;
2. apply an explicit public-field allowlist;
3. create a canonical public manifest;
4. upload that manifest to IPFS;
5. calculate an independent public Merkle root;
6. return a snapshot object for public-chain anchoring.

## Privacy rule

CID publication is a disclosure decision. A CID must not be copied into the
public snapshot unless the referenced content is intended for public access.
Internal signatures, operator details, sensor logs, and commercial data remain
private unless the publication policy explicitly includes them.

## Implementation status

Planned. Snapshot source files, persistence, API endpoints, and public-field
configuration have not been implemented.
