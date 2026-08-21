import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import "dotenv/config";
import {
  listPublishedBatches,
  publishCandidate,
  traceBatch,
  traceSnapshot,
} from "./scripts/publication.js";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const staticRoot = path.join(projectDirectory, "consumer");
const qrRoot = path.join(projectDirectory, "public-qrcodes");
const publicManifestRoot = path.join(projectDirectory, "public-manifests");
const port = Number(process.env.CONSUMER_PORT ?? "8082");
const host = process.env.CONSUMER_HOST || "0.0.0.0";

function detectLanHost() {
  const preferred = [];
  const fallback = [];
  for (const [interfaceName, addresses] of Object.entries(os.networkInterfaces())) {
    for (const address of addresses ?? []) {
      if (address.family !== "IPv4" || address.internal) continue;
      const target = interfaceName === "en0" || interfaceName === "en1"
        ? preferred
        : fallback;
      target.push(address.address);
    }
  }
  const privateAddress = [...preferred, ...fallback].find((address) =>
    /^(10\.|192\.168\.|172\.(1[6-9]|2\d|3[01])\.)/.test(address));
  return privateAddress ?? preferred[0] ?? fallback[0] ?? "127.0.0.1";
}

const defaultPublicHost = detectLanHost();
const consumerPublicUrl = (
  process.env.CONSUMER_PUBLIC_URL || `http://${defaultPublicHost}:${port}`
).replace(/\/+$/, "");
const qrGeneratorBinary = path.resolve(
  process.env.QR_GENERATOR_BINARY ??
    path.join(projectDirectory, "..", "SnapshotQRCode", "build", "snapshot_qr"),
);
const maximumBodySize = 2 * 1024 * 1024;
const publicationToken = process.env.PUBLIC_CHAIN_PUBLICATION_TOKEN ??
  "local-publication-demo-token";
const privateControlUrl = (
  process.env.PRIVATE_CONTROL_SERVER_URL || "http://127.0.0.1:8081"
).replace(/\/+$/, "");
const qrGenerationCache = new Map();

const contentTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "application/javascript; charset=utf-8"],
  [".json", "application/json; charset=utf-8"],
  [".svg", "image/svg+xml"],
]);

function send(response, status, body, contentType = "application/json; charset=utf-8") {
  response.writeHead(status, {
    "Content-Type": contentType,
    "Content-Length": Buffer.byteLength(body),
    "Access-Control-Allow-Origin": "http://127.0.0.1:8081",
    "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
    "Access-Control-Allow-Headers": "Content-Type",
    "Cache-Control": "no-store",
  });
  response.end(body);
}

function json(response, status, value) {
  send(response, status, JSON.stringify(value));
}

async function readJsonBody(request) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > maximumBodySize) throw new Error("The publication candidate is too large.");
    chunks.push(chunk);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new Error("The request body must be valid JSON.");
  }
}

function serveStatic(response, pathname) {
  const relative = pathname === "/" ? "index.html" : pathname.slice(1);
  const resolved = path.resolve(staticRoot, relative);
  if (!resolved.startsWith(`${staticRoot}${path.sep}`) || !fs.existsSync(resolved) ||
      !fs.statSync(resolved).isFile()) {
    json(response, 404, { error: "Not found" });
    return;
  }
  const contentType = contentTypes.get(path.extname(resolved)) ?? "application/octet-stream";
  send(response, 200, fs.readFileSync(resolved), contentType);
}

function qrFileName(batchId) {
  if (typeof batchId !== "string" ||
      !/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$/.test(batchId)) {
    throw new Error("A valid Batch ID is required for QR Code generation.");
  }
  return `${batchId}.png`;
}

function snapshotTraceUrl(batchId) {
  const url = new URL(consumerPublicUrl);
  url.pathname = "/";
  url.search = "";
  url.searchParams.set("batch", batchId);
  url.searchParams.set("view", "verification");
  return url.toString();
}

function ensureSnapshotQr({ batchId }) {
  const fileName = qrFileName(batchId);
  const outputPath = path.join(qrRoot, fileName);
  const traceUrl = snapshotTraceUrl(batchId);
  fs.mkdirSync(qrRoot, { recursive: true });

  if (qrGenerationCache.get(fileName) !== traceUrl || !fs.existsSync(outputPath)) {
    const generated = spawnSync(
      qrGeneratorBinary,
      [traceUrl, outputPath],
      { encoding: "utf8", maxBuffer: 1024 * 1024 },
    );
    if (generated.error) {
      throw new Error(`QR Code generator could not start: ${generated.error.message}`);
    }
    if (generated.status !== 0 || !fs.existsSync(outputPath)) {
      const details = generated.stderr?.trim() || generated.stdout?.trim() ||
        `exit status ${generated.status}`;
      throw new Error(`QR Code generation failed: ${details}`);
    }
    qrGenerationCache.set(fileName, traceUrl);
  }

  return {
    traceUrl,
    qrImageUrl: `${consumerPublicUrl}/qrcodes/${fileName}`,
  };
}

function generateSnapshotQr(publication) {
  return ensureSnapshotQr(publication.candidate);
}

async function listCurrentQrCodes() {
  if (!fs.existsSync(publicManifestRoot)) return [];
  const batches = new Map();
  for (const fileName of fs.readdirSync(publicManifestRoot)) {
    if (!fileName.endsWith(".json")) continue;
    try {
      const filePath = path.join(publicManifestRoot, fileName);
      const publication = JSON.parse(fs.readFileSync(filePath, "utf8"));
      const candidate = publication?.candidate;
      if (!candidate || typeof candidate.batchId !== "string") continue;
      const publishedOrder = Number(publication?.chain?.blockNumber ?? 0);
      const previous = batches.get(candidate.batchId);
      if (!previous || publishedOrder >= previous.publishedOrder) {
        batches.set(candidate.batchId, {
          batchId: candidate.batchId,
          product: candidate.manifest?.batch?.product_name ?? candidate.batchId,
          publishedOrder,
        });
      }
    } catch {
      // Ignore incomplete local publication files.
    }
  }

  return [...batches.values()]
    .sort((left, right) => right.publishedOrder - left.publishedOrder)
    .map((batch) => {
      try {
        return {
          ...batch,
          ...ensureSnapshotQr(batch),
        };
      } catch (error) {
        return {
          ...batch,
          qrError: error instanceof Error ? error.message : "QR Code generation failed",
        };
      }
    });
}

function serveQrCode(response, pathname) {
  const fileName = decodeURIComponent(pathname.slice("/qrcodes/".length));
  if (!/^[A-Za-z0-9][A-Za-z0-9._-]{0,127}\.png$/.test(fileName)) {
    json(response, 404, { error: "QR Code not found" });
    return;
  }
  const filePath = path.join(qrRoot, fileName);
  if (!fs.existsSync(filePath) || !fs.statSync(filePath).isFile()) {
    json(response, 404, { error: "QR Code not found" });
    return;
  }
  send(response, 200, fs.readFileSync(filePath), "image/png");
}

function proxyLiveEvents(response) {
  const upstreamUrl = new URL("/api/events", privateControlUrl);
  const upstreamRequest = http.request(upstreamUrl, {
    method: "GET",
    headers: { Accept: "text/event-stream" },
  }, (upstreamResponse) => {
    if (upstreamResponse.statusCode !== 200) {
      upstreamResponse.resume();
      json(response, 502, { error: "The private live-event stream is unavailable." });
      return;
    }
    response.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache, no-store",
      Connection: "keep-alive",
    });
    upstreamResponse.pipe(response);
    upstreamResponse.on("error", () => response.end());
  });
  upstreamRequest.on("error", () => {
    if (!response.headersSent) {
      json(response, 503, { error: "The private live-event stream is unavailable." });
    } else {
      response.end();
    }
  });
  response.on("close", () => upstreamRequest.destroy());
  upstreamRequest.end();
}

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host ?? `${host}:${port}`}`);
  if (request.method === "OPTIONS") {
    send(response, 204, "", "text/plain; charset=utf-8");
    return;
  }

  try {
    if (request.method === "GET" && url.pathname === "/api/status") {
      json(response, 200, { service: "consumer", status: "ready" });
      return;
    }
    if (request.method === "GET" && url.pathname === "/api/events") {
      proxyLiveEvents(response);
      return;
    }
    if (request.method === "GET" && url.pathname === "/api/batches") {
      json(response, 200, { batches: await listPublishedBatches() });
      return;
    }
    if (request.method === "GET" && url.pathname === "/api/qr-codes") {
      json(response, 200, { qrCodes: await listCurrentQrCodes() });
      return;
    }
    if (request.method === "POST" && url.pathname === "/api/publish") {
      if (request.headers["x-publication-token"] !== publicationToken) {
        json(response, 403, { error: "Publication authorization failed." });
        return;
      }
      const publication = await publishCandidate(await readJsonBody(request));
      let qrCode = {};
      try {
        generateSnapshotQr(publication);
      } catch (error) {
        const message = error instanceof Error ? error.message : "QR Code generation failed";
        console.error(message);
        qrCode = { qrError: message };
      }
      json(response, 201, {
        batchId: publication.candidate.batchId,
        snapshotId: publication.candidate.snapshotId,
        status: publication.chain.status,
        transactionHash: publication.chain.transactionHash,
        blockNumber: publication.chain.blockNumber,
        ...qrCode,
      });
      return;
    }
    if (request.method === "GET" && url.pathname.startsWith("/api/trace/snapshot/")) {
      const snapshotId = decodeURIComponent(
        url.pathname.slice("/api/trace/snapshot/".length),
      );
      const trace = await traceSnapshot(snapshotId);
      if (!trace) {
        json(response, 404, {
          error: "This Snapshot is inactive or no longer matches the current route.",
        });
      } else {
        json(response, 200, trace);
      }
      return;
    }
    if (request.method === "GET" && url.pathname.startsWith("/api/trace/")) {
      const batchId = decodeURIComponent(url.pathname.slice("/api/trace/".length));
      const trace = await traceBatch(batchId);
      if (!trace) json(response, 404, { error: "No public trace was found for this Batch ID." });
      else json(response, 200, trace);
      return;
    }
    if (request.method === "GET" && url.pathname.startsWith("/qrcodes/")) {
      serveQrCode(response, url.pathname);
      return;
    }
    if (request.method === "GET") {
      serveStatic(response, url.pathname);
      return;
    }
    json(response, 405, { error: "Method not allowed" });
  } catch (error) {
    console.error(error);
    const message = error instanceof Error ? error.message : "Unexpected server error";
    const unavailable = /ECONNREFUSED|ECONNRESET|ETIMEDOUT|fetch failed|socket/i
      .test(message);
    json(response, unavailable ? 503 : 422, { error: message });
  }
});

server.listen(port, host, () => {
  console.log(`Consumer trace: ${consumerPublicUrl}`);
  console.log(`Public manifests: ${path.join(projectDirectory, "public-manifests")}`);
  console.log(`Public QR Codes: ${qrRoot}`);
});
