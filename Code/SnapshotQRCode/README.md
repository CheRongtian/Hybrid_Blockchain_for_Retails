# Snapshot QR Code Generator

This directory is an independent integration copy of the C QR generator. The
original learning project under `Code/QR Code` remains unchanged.

The public Snapshot service passes two arguments to this executable:

1. the exact customer URL containing `?snapshot=<snapshot-id>`;
2. the destination PNG path under `Code/PublicChain/public-qrcodes/`.

The output includes a four-module quiet zone and standard black modules on a
white background so phones can scan it reliably.

## Build

```bash
cmake -S . -B build
cmake --build build --target snapshot_qr
```

## Run directly

```bash
./build/snapshot_qr \
  "http://127.0.0.1:8082/?snapshot=SNAP-BATCH-EXAMPLE" \
  snapshot.png
```

`start_customer_server.sh` builds this target and exports its absolute path as
`QR_GENERATOR_BINARY` before starting the public customer service.
