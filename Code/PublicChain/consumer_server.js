import fs from "node:fs";
import http from "node:http";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import "dotenv/config";
import {
  listPublishedBatches,
  publishCandidate,
  traceBatch,
  traceSnapshot,
} from "./scripts/publication.js";
import {
  publicManifestDirectory,
  publicQrDirectory,
} from "./scripts/runtime.js";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const staticRoot = path.join(projectDirectory, "consumer");
const qrRoot = publicQrDirectory;
const publicManifestRoot = publicManifestDirectory;
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
const agentUrl = process.env.url?.trim() ?? "";
const agentKey = process.env.key?.trim() ?? "";
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
    if (size > maximumBodySize) throw new Error("The request body is too large.");
    chunks.push(chunk);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw new Error("The request body must be valid JSON.");
  }
}

function httpError(message, statusCode) {
  const error = new Error(message);
  error.statusCode = statusCode;
  return error;
}

function validateAssistantRequest(body) {
  const batchId = typeof body?.batchId === "string" ? body.batchId.trim() : "";
  const snapshotId = typeof body?.snapshotId === "string" ? body.snapshotId.trim() : "";
  const providedSessionId = typeof body?.sessionId === "string"
    ? body.sessionId.trim()
    : "";
  const question = typeof body?.question === "string" ? body.question.trim() : "";

  if (!batchId || batchId.length > 256) {
    throw httpError("A valid Batch ID is required.", 422);
  }
  if (!snapshotId || snapshotId.length > 256) {
    throw httpError("A valid Snapshot ID is required.", 422);
  }
  if (providedSessionId && (providedSessionId.length > 128 ||
      !/^[A-Za-z0-9-]+$/.test(providedSessionId))) {
    throw httpError("A valid assistant session ID is required.", 422);
  }
  if (!question || question.length > 1000) {
    throw httpError("A question between 1 and 1000 characters is required.", 422);
  }

  return {
    batchId,
    snapshotId,
    sessionId: providedSessionId || crypto.randomUUID(),
    firstQuestion: !providedSessionId,
    question,
  };
}

async function readAgentAnswer(response) {
  if (!response.body) throw httpError("The AI service returned an empty stream.", 502);

  const decoder = new TextDecoder();
  let buffer = "";
  let answer = "";

  const consumeLine = (line) => {
    if (!line.startsWith("data:")) return;
    const data = line.slice(5).trim();
    if (!data || data === "[DONE]") return;

    let event;
    try {
      event = JSON.parse(data);
    } catch {
      throw httpError("The AI service returned a malformed event stream.", 502);
    }
    if (typeof event.content === "string") answer += event.content;
    if (answer.length > 64000) {
      throw httpError("The AI response is too large.", 502);
    }
  };

  for await (const chunk of response.body) {
    buffer += decoder.decode(chunk, { stream: true });
    const lines = buffer.split(/\r?\n/);
    buffer = lines.pop() ?? "";
    for (const line of lines) consumeLine(line);
  }
  buffer += decoder.decode();
  if (buffer) consumeLine(buffer);

  const completeAnswer = answer.trim();
  if (!completeAnswer) throw httpError("The AI service returned no answer.", 502);

  let parsedAnswer;
  try {
    parsedAnswer = JSON.parse(completeAnswer);
  } catch {
    return completeAnswer;
  }

  const displayValues = [];
  const collectDisplayValues = (value) => {
    if (typeof value === "string") {
      const text = value.trim();
      if (text) displayValues.push(text);
      return;
    }
    if (typeof value === "number" || typeof value === "boolean") {
      displayValues.push(String(value));
      return;
    }
    if (Array.isArray(value)) {
      value.forEach(collectDisplayValues);
      return;
    }
    if (value && typeof value === "object") {
      Object.values(value).forEach(collectDisplayValues);
    }
  };

  collectDisplayValues(parsedAnswer);
  if (displayValues.length === 0) {
    throw httpError("The AI service returned no displayable answer value.", 502);
  }
  return displayValues.join("\n");
}

async function askAgent(trace, sessionId, question, firstQuestion) {
  if (!agentUrl || !agentKey) {
    throw httpError("The AI assistant has not been configured.", 503);
  }
  try {
    new URL(agentUrl);
  } catch {
    throw httpError("The AI assistant URL is invalid.", 503);
  }

  const content = firstQuestion
    ? `${JSON.stringify(trace)}\n\n${question}`
    : question;

  let response;
  try {
    response = await fetch(agentUrl, {
      method: "POST",
      headers: {
        Authorization: agentKey,
        "Content-Type": "application/json",
        Accept: "text/event-stream",
      },
      body: JSON.stringify({
        messages: [{ role: "user", content }],
        sessionId,
        source: "api",
        extra: {},
      }),
      signal: AbortSignal.timeout(60000),
    });
  } catch (error) {
    const message = error instanceof Error ? error.message : "request failed";
    throw httpError(`The AI service is unavailable: ${message}`, 503);
  }

  if (!response.ok) {
    throw httpError(`The AI service rejected the request (${response.status}).`, 502);
  }
  return readAgentAnswer(response);
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
    if (request.method === "POST" && url.pathname === "/api/assistant") {
      const assistantRequest = validateAssistantRequest(await readJsonBody(request));
      const trace = await traceBatch(assistantRequest.batchId);
      if (!trace) {
        json(response, 404, { error: "No active public trace was found for this Batch ID." });
        return;
      }
      if (!trace.verified) {
        json(response, 409, {
          error: "AI questions are available only for a verified active Snapshot.",
        });
        return;
      }
      if (trace.snapshotId !== assistantRequest.snapshotId) {
        json(response, 409, {
          error: "The active Snapshot changed. Refresh the scan result before asking again.",
        });
        return;
      }

      const answer = await askAgent(
        trace,
        assistantRequest.sessionId,
        assistantRequest.question,
        assistantRequest.firstQuestion,
      );
      json(response, 200, {
        batchId: trace.batchId,
        snapshotId: trace.snapshotId,
        sessionId: assistantRequest.sessionId,
        answer,
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
    if (error?.code === "SNAPSHOT_NOT_AVAILABLE") {
      json(response, 404, {
        error: message,
        code: error.code,
        state: error.state,
        batchId: error.batchId,
        availableFrom: error.availableFrom,
        availableUntil: error.availableUntil,
      });
      return;
    }
    if (Number.isInteger(error?.statusCode)) {
      json(response, error.statusCode, { error: message });
      return;
    }
    const unavailable = /ECONNREFUSED|ECONNRESET|ETIMEDOUT|fetch failed|socket/i
      .test(message);
    json(response, unavailable ? 503 : 422, { error: message });
  }
});

server.listen(port, host, () => {
  console.log(`Consumer trace: ${consumerPublicUrl}`);
  console.log(`Public manifests: ${publicManifestRoot}`);
  console.log(`Public QR Codes: ${qrRoot}`);
});
