import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import "dotenv/config";
import {
  listPublishedBatches,
  publishCandidate,
  traceBatch,
} from "./scripts/publication.js";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const staticRoot = path.join(projectDirectory, "consumer");
const port = Number(process.env.CONSUMER_PORT ?? "8082");
const host = "127.0.0.1";
const maximumBodySize = 2 * 1024 * 1024;
const publicationToken = process.env.PUBLIC_CHAIN_PUBLICATION_TOKEN ??
  "local-publication-demo-token";

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
    if (request.method === "GET" && url.pathname === "/api/batches") {
      json(response, 200, { batches: await listPublishedBatches() });
      return;
    }
    if (request.method === "POST" && url.pathname === "/api/publish") {
      if (request.headers["x-publication-token"] !== publicationToken) {
        json(response, 403, { error: "Publication authorization failed." });
        return;
      }
      const publication = await publishCandidate(await readJsonBody(request));
      json(response, 201, {
        batchId: publication.candidate.batchId,
        snapshotId: publication.candidate.snapshotId,
        status: publication.chain.status,
        transactionHash: publication.chain.transactionHash,
        blockNumber: publication.chain.blockNumber,
      });
      return;
    }
    if (request.method === "GET" && url.pathname.startsWith("/api/trace/")) {
      const batchId = decodeURIComponent(url.pathname.slice("/api/trace/".length));
      const trace = await traceBatch(batchId);
      if (!trace) json(response, 404, { error: "No public trace was found for this Batch ID." });
      else json(response, 200, trace);
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
  console.log(`Consumer trace: http://${host}:${port}`);
  console.log(`Public manifests: ${path.join(projectDirectory, "public-manifests")}`);
});
