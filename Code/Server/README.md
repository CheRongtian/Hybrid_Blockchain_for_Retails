# Standalone C++ HTTP Server

This directory contains an independent, standalone C++ HTTP server. It was
derived from the earlier HTML demo server and can now evolve separately.

There is currently no MerkleTree, smart-contract, or blockchain integration.
The original project under `Projects/demo projects/HTML demo` remains separate.

## Current capabilities

- Serves static files with `GET` and `HEAD`.
- Listens on `127.0.0.1:8080` by default.
- Accepts configurable port, static directory, and SQLite path.
- Initializes a local SQLite `messages` table.
- Writes request records to `server.log`.
- Handles partial socket writes.
- Rejects unsupported HTTP methods with `405`.
- Prevents static-file paths from escaping the configured directory.
- Returns a custom `404.html` when available.

`POST` forms, JSON APIs, Merkle proofs, and blockchain calls remain outside the
current scope.

## Files

```text
Server/
├── CMakeLists.txt
├── server.cpp
├── db_utils.hpp
├── db_utils.cpp
├── log_utils.hpp
├── log_utils.cpp
├── static/
│   ├── Home.html
│   ├── 404.html
│   └── css/style.css
└── README.md
```

This directory has its own minimal static page and does not depend on the
original HTML demo's `static` directory.

## Requirements

- CMake 3.10 or newer
- A C++17 compiler
- SQLite3 development files
- macOS or another POSIX-compatible system

## Build

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure/Code/Server"
cmake -S . -B build
cmake --build build
```

## Run

```text
./server [port] [static_directory] [database_path]
```

Example:

```bash
./build/server
```

Then open:

```text
http://127.0.0.1:8080/
```

Defaults:

- Port: `8080`
- Static directory: the copied `build/static` directory
- Database: `message.db`
- Request log: `server.log`

The CMake configuration copies `Server/static` into `build/static`, so running
`./build/server` works without a static-directory argument. A custom absolute
directory can still be supplied as the second argument.

## Static routing

- `/` resolves to `Home.html`.
- `/photo` checks `photo`, followed by `photo.html`.
- Query strings are ignored during static-file lookup.
- Known extensions receive matching `Content-Type` headers.
- Missing resources use `404.html` when present.
- Only files inside the configured static directory can be served.

## Current boundary

The server does not include or link anything from `Code/MerkleTree`. A later
integration can add explicit API routes while keeping this standalone server and
the original HTML demo isolated from each other.
