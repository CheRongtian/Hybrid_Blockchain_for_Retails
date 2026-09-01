FROM node:22-bookworm AS builder

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        ca-certificates \
        cmake \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app/Code/PublicChain

COPY Code/PublicChain/package.json Code/PublicChain/package-lock.json ./
RUN npm ci --no-audit --no-fund

COPY Code/PublicChain/ ./
COPY docker/ensure-deployment.mjs ./scripts/docker_ensure_deployment.mjs
RUN npm run compile

COPY Code/SnapshotQRCode/ /app/Code/SnapshotQRCode/
RUN cmake -S /app/Code/SnapshotQRCode -B /qr-build \
    && cmake --build /qr-build --target snapshot_qr -j2

FROM node:22-bookworm-slim

WORKDIR /app/Code/PublicChain

COPY --from=builder /app/Code/PublicChain/ ./
COPY --from=builder /qr-build/snapshot_qr /usr/local/bin/snapshot_qr
COPY Code/Snapshot/examples/ /app/Code/Snapshot/examples/

ENV QR_GENERATOR_BINARY=/usr/local/bin/snapshot_qr

EXPOSE 8082 8084 8545
