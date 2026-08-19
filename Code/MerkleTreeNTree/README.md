# Standalone N-ary Merkle Tree

This directory is an independent demonstration module. It does not change the
binary Merkle Tree used by the supply-chain application and is not included by
the parent `Code/CMakeLists.txt`.

## What it demonstrates

- A runtime-selected branching factor `N`, with `N >= 2`.
- Ordered SHA-256 hashing for leaves and parent nodes.
- Padding of an incomplete final group by duplicating its last child hash.
- N-ary Merkle proofs containing the child position and all sibling hashes at
  every level.
- Proof generation and verification.
- JSON Lines build events, flushed after each event.
- A local browser visualizer that consumes those events through Server-Sent
  Events (SSE) while the C++ tree is being built.

For a binary-compatible comparison, run the module with `N=2`. For `N>2`, a
parent is formed from up to `N` ordered children. If the final group has fewer
than `N` children, the last child is repeated until the group is full.

## Build and run the command-line demo

From this directory:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/MerkleTreeNTree"
./run_ntree.sh --arity 3 --input examples/leaves.txt --output output/tree-result.json
```

The command builds the standalone target in `build/` and writes the final
result to `output/tree-result.json`.

The program also accepts values directly:

```bash
./run_ntree.sh --arity 4 \
  --value "Supplier: oranges" \
  --value "Warehouse: cold storage" \
  --value "Supermarket: shelf A"
```

If the arity or leaves are omitted, use the interactive mode:

```bash
./run_ntree.sh --interactive
```

Useful options:

```text
--arity N                 Branching factor; required value is at least 2
--input FILE              One leaf value per non-empty line
--value TEXT              Add one leaf value; may be repeated
--proof-index INDEX       Generate and verify a proof for this zero-based leaf
--output FILE              Write the final result as JSON
--events                   Emit JSON Lines build events to stdout
--interactive              Prompt for the arity and leaf values
```

## Start the real-time visualizer

```bash
./run_visualizer.sh --arity 3 --input examples/leaves.txt
```

Open <http://127.0.0.1:8765/> in a browser. The local Python server starts the
C++ process, reads one JSON event at a time, and forwards each event to the
page immediately. The page displays:

- the tree as it grows from leaves to root;
- padding nodes in a separate visual style;
- current arity, event count, leaf count, height, and root hash;
- a live event log;
- proof generation and verification status;
- pause, resume, single-step, speed, and replay controls.

The visualizer uses the same input file and arity for the whole run. It writes
the event stream and final result under `output/`:

```text
output/tree-events.ndjson
output/tree-result.json
```

The server is local-only and listens on `127.0.0.1`. Stop it with `Ctrl-C`.

## Event protocol

The C++ executable emits JSON Lines when `--events` is supplied. The current
event types are:

```text
build_started
leaf_created
padding_added
parent_created
level_completed
root_created
build_succeeded
proof_generated
proof_verified
proof_failed
```

Each build event includes the current node snapshot, so a consumer can redraw
the complete tree after every event without depending on hidden application
state.

## Relationship to the existing application

This module shares the project's OpenSSL SHA-256 dependency and follows the
same leaf and parent hash convention for `N=2`. It has its own CMake project,
executable, visualizer server, examples, and output directory. The existing
supply-chain services continue using their current binary Merkle Tree.
