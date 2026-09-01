# Docker Guide

This document covers Docker configuration, image builds, service startup, verification, and shutdown for this project. Run all commands from the project root unless stated otherwise.

## 1. Prerequisites

1. Install and start Docker Desktop.
2. Confirm that the Docker Engine and Docker Compose are available:

   ```bash
   docker version
   docker compose version
   ```

3. Stop any services previously launched by `start_all.sh` to prevent conflicts on ports 8080, 8081, 8082, 8084, or 8545.

Enter the project root:

```bash
cd "/Users/cherongtian/Desktop/Projects/Blockchain Structure"
```

## 2. Docker Environment Configuration

Create the local environment files before the first run. These commands only copy the example files when the destination files do not already exist, so existing configuration will remain unchanged:

```bash
[ -f docker/docker.env ] || cp docker/docker.env.example docker/docker.env
[ -f Code/PublicChain/.env ] || cp Code/PublicChain/.env.example Code/PublicChain/.env
```

### QR Code Access URL

`CONSUMER_PUBLIC_URL` in `docker/docker.env` defines the customer page URL embedded in generated QR codes:

```dotenv
CONSUMER_PUBLIC_URL=http://192.168.1.100:8082
```

Replace the example IP address with the current LAN address of the Mac running Docker. On macOS, the Wi-Fi address can usually be found with:

```bash
ipconfig getifaddr en0
```

The phone and Mac must be connected to the same local network. After changing `docker/docker.env`, recreate the affected containers:

```bash
docker compose up -d --force-recreate consumer qr-display
```

### AI API Configuration

`Code/PublicChain/.env` stores the API settings used by the customer-facing AI assistant:

```dotenv
url=
key=
```

The Docker build excludes this file from the image. Docker Compose loads it when the `consumer` container starts. API URLs and keys should be configured and tested by the operator.

## 3. Validate the Compose Configuration

Before building, verify that Docker Compose can parse the configuration:

```bash
docker compose config --quiet
```

No output and an exit status of 0 indicate a valid configuration.

## 4. First Build and Startup

```bash
docker compose up --build -d
```

Command options:

- `up` creates and starts the services defined in Compose.
- `--build` builds the project images before starting the services.
- `-d` runs the containers in the background and returns control of the terminal.

The Docker setup uses these files:

- `docker/private.Dockerfile` builds the C++ administrator and participant service image.
- `docker/public.Dockerfile` builds the image used by Hardhat, the customer page, the QR display, and contract deployment.
- `compose.yaml` defines services, dependencies, ports, health checks, and volumes.

## 5. Services and Ports

| Compose service | Purpose | Host access |
| --- | --- | --- |
| `ipfs` | Stores and retrieves snapshot content | Internal container network only |
| `hardhat` | Local blockchain node | `http://127.0.0.1:8545` |
| `deploy` | One-time task that checks and deploys the snapshot contract | No web port |
| `control` | Administrator control service | `http://127.0.0.1:8081` |
| `participant` | Supply-chain participant service | `http://127.0.0.1:8080` |
| `consumer` | Customer verification and AI assistant service | `http://127.0.0.1:8082` |
| `qr-display` | QR page for the latest published batch | `http://127.0.0.1:8084` |

The `deploy` service normally displays `Exited (0)` after it finishes. Exit code 0 means the contract check or deployment completed successfully.

## 6. Check Service Status

Show all services, including completed one-time tasks:

```bash
docker compose ps -a
```

Expected status:

- `ipfs`, `hardhat`, `control`, `participant`, `consumer`, and `qr-display` show `Up` or `healthy`.
- `deploy` shows `Exited (0)`.

Verify that all four web services return HTTP 200:

```bash
for port in 8080 8081 8082 8084; do
  curl -sS -o /dev/null -w "$port -> %{http_code}\n" "http://127.0.0.1:$port/"
done
```

Expected output:

```text
8080 -> 200
8081 -> 200
8082 -> 200
8084 -> 200
```

## 7. View Logs

Show the most recent 100 log lines from all services:

```bash
docker compose logs --tail=100
```

Follow all service logs:

```bash
docker compose logs -f
```

Press `Ctrl+C` to stop following the logs. The containers will continue running.

Show logs for individual services:

```bash
docker compose logs --tail=100 consumer
docker compose logs --tail=100 deploy
docker compose logs --tail=100 hardhat
```

## 8. Routine Startup, Restart, and Rebuild

When the images are already built and the source code has not changed:

```bash
docker compose up -d
```

Restart existing services:

```bash
docker compose restart
docker compose ps -a
```

Rebuild and start after changing source files or a Dockerfile:

```bash
docker compose up --build -d
```

Build without using the Docker build cache:

```bash
docker compose build --no-cache
docker compose up -d
```

## 9. Stop and Resume

Temporarily stop the existing containers while keeping them available:

```bash
docker compose stop
```

Resume those containers:

```bash
docker compose start
```

Stop and remove the containers and Compose network:

```bash
docker compose down
```

Create and start them again:

```bash
docker compose up -d
```

`docker compose down` removes the Hardhat container, so the in-memory Hardhat chain resets on the next startup. The deployment task checks the new chain and deploys the contract again when required. `Storage/` is a host bind mount, so a regular `down` command keeps its data. The `ipfs-data` and `public-deployments` named volumes are also retained.

Use the following command only when intentionally performing a full Docker data cleanup:

```bash
docker compose down -v
```

The `-v` option deletes the `ipfs-data` and `public-deployments` named volumes. Confirm that this Docker data can be discarded before running the command.

## 10. Troubleshooting

### Port Already in Use

If Compose reports that port 8080, 8081, 8082, 8084, or 8545 cannot be bound, inspect the corresponding port:

```bash
lsof -nP -iTCP:8080 -sTCP:LISTEN
lsof -nP -iTCP:8081 -sTCP:LISTEN
lsof -nP -iTCP:8082 -sTCP:LISTEN
lsof -nP -iTCP:8084 -sTCP:LISTEN
lsof -nP -iTCP:8545 -sTCP:LISTEN
```

A common cause is a local process that was previously started by `start_all.sh`.

### Container Does Not Become Healthy

Check the current state and then inspect the affected service logs:

```bash
docker compose ps -a
docker compose logs --tail=100 service-name
```

For example, inspect the customer service with:

```bash
docker compose logs --tail=100 consumer
```

### Phone Cannot Open the QR Code URL

Confirm the following:

1. `docker/docker.env` contains the Mac's current LAN IP address rather than `127.0.0.1`.
2. The phone and Mac are connected to the same local network.
3. The `consumer` container is `healthy`.
4. The phone can directly open `http://MAC_LAN_IP:8082/`.
5. The `consumer` and `qr-display` containers were recreated after changing the address.
