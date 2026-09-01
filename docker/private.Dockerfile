FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        build-essential \
        ca-certificates \
        cmake \
        libsqlite3-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /source
COPY Code/ /source/

RUN cmake -S /source -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DMERKLE_TREE_BUILD_CLI=OFF \
    && cmake --build /build --target control_server user_server -j2

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends -y \
        ca-certificates \
        libsqlite3-0 \
        libssl3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/Server/control_server /app/bin/control_server
COPY --from=builder /build/Server/user_server /app/bin/user_server
COPY --from=builder /build/Server/control_static /app/control_static
COPY --from=builder /build/Server/user_static /app/user_static

EXPOSE 8080 8081
