# Supply Chain Merkle Server

This C++17 HTTP server provides a minimal in-memory supply-chain verification
flow:

```text
Browser confirmation form
    -> POST /api/records
    -> MerkleTree::Append
    -> generate proof
    -> verify proof
    -> return JSON result
```

The current stage has no SQLite, file upload, smart-contract, or IPFS support.
All Merkle blocks are cleared when the server exits.

## Build from Code

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code"
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/Server/server
```

Then open:

```text
http://127.0.0.1:8080/
```

Optional arguments:

```text
./server [port] [static_directory]
```

## API

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
  "rootHash": "...",
  "proof": "...",
  "verified": true
}
```
