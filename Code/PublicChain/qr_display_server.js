import fs from "node:fs";
import http from "node:http";
import path from "node:path";
import { fileURLToPath } from "node:url";
import "dotenv/config";

const projectDirectory = path.dirname(fileURLToPath(import.meta.url));
const staticRoot = path.join(projectDirectory, "qr-display");
const port = Number(process.env.QR_DISPLAY_PORT ?? "8084");
const host = process.env.QR_DISPLAY_HOST ?? "0.0.0.0";
const consumerInternalUrl = (
  process.env.CONSUMER_INTERNAL_URL ??
  `http://127.0.0.1:${process.env.CONSUMER_PORT ?? "8082"}`
).replace(/\/+$/, "");

const contentTypes = new Map([
  [".html", "text/html; charset=utf-8"],
  [".css", "text/css; charset=utf-8"],
  [".js", "application/javascript; charset=utf-8"],
]);

function send(response, status, body, contentType = "application/json; charset=utf-8") {
  response.writeHead(status, {
    "Content-Type": contentType,
    "Content-Length": Buffer.byteLength(body),
    "Cache-Control": "no-store",
  });
  response.end(body);
}

function json(response, status, value) {
  send(response, status, JSON.stringify(value));
}

function serveStatic(response, pathname) {
  const relative = pathname === "/" ? "index.html" : pathname.slice(1);
  const resolved = path.resolve(staticRoot, relative);
  if (!resolved.startsWith(`${staticRoot}${path.sep}`) ||
      !fs.existsSync(resolved) || !fs.statSync(resolved).isFile()) {
    json(response, 404, { error: "Not found" });
    return;
  }
  const contentType = contentTypes.get(path.extname(resolved)) ??
    "application/octet-stream";
  send(response, 200, fs.readFileSync(resolved), contentType);
}

async function proxyQrCodes(response) {
  const upstream = await fetch(`${consumerInternalUrl}/api/qr-codes`);
  const body = await upstream.text();
  send(response, upstream.status, body, "application/json; charset=utf-8");
}

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url, `http://${request.headers.host ?? `${host}:${port}`}`);
  try {
    if (request.method === "GET" && url.pathname === "/api/status") {
      json(response, 200, { service: "qr-display", status: "ready" });
      return;
    }
    if (request.method === "GET" && url.pathname === "/api/qr-codes") {
      await proxyQrCodes(response);
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
    json(response, 503, { error: message });
  }
});

server.listen(port, host, () => {
  console.log(`QR display: http://127.0.0.1:${port}`);
  console.log(`QR display upstream: ${consumerInternalUrl}`);
});
