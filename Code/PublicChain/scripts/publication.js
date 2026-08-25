import crypto from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { Contract, JsonRpcProvider, ZeroHash, id, keccak256, toUtf8Bytes } from "ethers";
import { projectDirectory, readJson } from "./runtime.js";

const statusNames = ["None", "Active", "Superseded", "Recalled", "Revoked"];
const privateControlServerUrl = (
  process.env.PRIVATE_CONTROL_SERVER_URL ?? "http://127.0.0.1:8081"
).replace(/\/+$/, "");
const publicationToken = process.env.PUBLIC_CHAIN_PUBLICATION_TOKEN ??
  "local-publication-demo-token";

function requireText(candidate, name) {
  const value = candidate[name];
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`${name} is required.`);
  }
  return value;
}

function requireBytes32(candidate, name) {
  const value = requireText(candidate, name);
  if (!/^0x[0-9a-fA-F]{64}$/.test(value)) {
    throw new Error(`${name} must be a 0x-prefixed bytes32 value.`);
  }
  return value.toLowerCase();
}

function sha256Hex(value) {
  return crypto.createHash("sha256").update(value, "utf8").digest("hex");
}

async function fetchCurrentRouteState(batchId) {
  const url = `${privateControlServerUrl}/api/public-route-state?batchId=${
    encodeURIComponent(batchId)
  }`;
  const response = await fetch(url, {
    headers: { "X-Publication-Token": publicationToken },
  });
  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new Error("The private route-state response is not valid JSON.");
  }
  if (!response.ok) {
    throw new Error(payload.error || `Private route-state request failed: ${response.status}`);
  }
  if (typeof payload.routeFingerprint !== "string" ||
      !/^[0-9a-fA-F]{64}$/.test(payload.routeFingerprint) ||
      typeof payload.routeShape !== "string" || payload.routeShape.length === 0) {
    throw new Error("The private route-state response is incomplete.");
  }
  return {
    routeFingerprint: payload.routeFingerprint.toLowerCase(),
    routeShape: payload.routeShape,
  };
}

async function fetchSnapshotStatus(batchId) {
  const url = privateControlServerUrl +
    "/api/snapshot/status?batchId=" + encodeURIComponent(batchId);
  const response = await fetch(url, {
    headers: { "X-Publication-Token": publicationToken },
  });
  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new Error("The private Snapshot status response is not valid JSON.");
  }
  if (!response.ok) {
    throw new Error(payload.error ||
      `Private Snapshot status request failed: ${response.status}`);
  }
  return payload;
}

function candidateRouteShape(candidate) {
  const route = Array.isArray(candidate.manifest?.route)
    ? candidate.manifest.route
    : candidate.manifest?.verification?.route;
  if (!Array.isArray(route)) return "";
  return route.map((stage) => typeof stage === "string" ? stage : stage?.stage ?? "")
    .join("|");
}

function candidateMatchesRoute(candidate, routeState) {
  if (typeof candidate.routeFingerprint === "string" &&
      candidate.routeFingerprint.length > 0) {
    return candidate.routeFingerprint.toLowerCase() === routeState.routeFingerprint;
  }
  return candidateRouteShape(candidate) === routeState.routeShape;
}

export class SnapshotAvailabilityError extends Error {
  constructor(state, batchId, availableFrom, availableUntil) {
    const message = state === "upcoming"
      ? "This batch is not available to customers yet."
      : "This Snapshot cannot be served in its current availability state.";
    super(message);
    this.name = "SnapshotAvailabilityError";
    this.code = "SNAPSHOT_NOT_AVAILABLE";
    this.state = state;
    this.batchId = batchId;
    this.availableFrom = availableFrom;
    this.availableUntil = availableUntil;
  }
}

function snapshotAvailability(manifest, now = Date.now()) {
  if (manifest?.availability === undefined) {
    return { state: "legacy", available: true, availableFrom: "", availableUntil: "" };
  }
  const availableFrom = manifest?.availability?.available_from;
  const availableUntil = manifest?.availability?.available_until;
  const start = Date.parse(availableFrom);
  const end = Date.parse(availableUntil);
  if (typeof availableFrom !== "string" || typeof availableUntil !== "string" ||
      !Number.isFinite(start) || !Number.isFinite(end) || end <= start) {
    throw new Error("The Snapshot availability window is invalid.");
  }
  if (now < start) {
    return { state: "upcoming", available: false, availableFrom, availableUntil };
  }
  if (now >= end) {
    return { state: "expired", available: true, availableFrom, availableUntil };
  }
  return { state: "available", available: true, availableFrom, availableUntil };
}

export function buildPublicRoot(publicFields) {
  if (!Array.isArray(publicFields) || publicFields.length === 0) {
    throw new Error("publicFields must contain at least one field.");
  }

  let level = publicFields.map((field) => {
    if (!field || typeof field.name !== "string" ||
        typeof field.value !== "string") {
      throw new Error("Every public field must contain string name and value properties.");
    }
    const valueLength = Buffer.byteLength(field.value, "utf8");
    return sha256Hex(`${field.name}:${valueLength}:${field.value}`);
  });

  while (level.length > 1) {
    if (level.length % 2 === 1) level.push(level[level.length - 1]);
    const next = [];
    for (let index = 0; index < level.length; index += 2) {
      next.push(sha256Hex(level[index] + level[index + 1]));
    }
    level = next;
  }
  return `0x${level[0]}`;
}

export function validateCandidate(candidate) {
  if (!candidate || typeof candidate !== "object" || Array.isArray(candidate)) {
    throw new Error("A publication candidate object is required.");
  }

  const protocol = requireText(candidate, "protocol");
  const snapshotId = requireText(candidate, "snapshotId");
  const batchId = requireText(candidate, "batchId");
  const protocolHash = requireBytes32(candidate, "protocolHash");
  const snapshotIdHash = requireBytes32(candidate, "snapshotIdHash");
  const batchIdHash = requireBytes32(candidate, "batchIdHash");
  const publicRoot = requireBytes32(candidate, "publicRoot");
  const manifestHash = requireBytes32(candidate, "manifestHash");
  requireBytes32(candidate, "sourceBlockHash");

  if (candidate.routeFingerprint !== undefined) {
    if (typeof candidate.routeFingerprint !== "string" ||
        !/^[0-9a-fA-F]{64}$/.test(candidate.routeFingerprint)) {
      throw new Error("routeFingerprint must be a 64-character hexadecimal value.");
    }
    const manifestFingerprint = candidate.manifest?.route_fingerprint;
    if (typeof manifestFingerprint !== "string" ||
        manifestFingerprint.toLowerCase() !== candidate.routeFingerprint.toLowerCase()) {
      throw new Error("The candidate route fingerprint does not match its manifest.");
    }
  }

  if (!Number.isInteger(candidate.snapshotVersion) || candidate.snapshotVersion <= 0) {
    throw new Error("snapshotVersion must be a positive integer.");
  }
  const manifestCanonical = requireText(candidate, "manifestCanonical");
  let canonicalManifest;
  try {
    canonicalManifest = JSON.parse(manifestCanonical);
  } catch {
    throw new Error("manifestCanonical is not valid JSON.");
  }
  if (JSON.stringify(canonicalManifest) !== JSON.stringify(candidate.manifest)) {
    throw new Error("The parsed manifest does not match manifestCanonical.");
  }
  snapshotAvailability(candidate.manifest);

  const checks = {
    protocolHash: id(protocol).toLowerCase() === protocolHash,
    snapshotIdHash: id(snapshotId).toLowerCase() === snapshotIdHash,
    batchIdHash: id(batchId).toLowerCase() === batchIdHash,
    manifestHash: keccak256(toUtf8Bytes(manifestCanonical)).toLowerCase() === manifestHash,
    publicRoot: buildPublicRoot(candidate.publicFields).toLowerCase() === publicRoot,
  };
  const failed = Object.entries(checks).filter(([, valid]) => !valid)
    .map(([name]) => name);
  if (failed.length > 0) {
    throw new Error(`Publication candidate verification failed: ${failed.join(", ")}.`);
  }
  return checks;
}

function artifactPath() {
  return path.join(
    projectDirectory,
    "artifacts",
    "contracts",
    "SnapshotGateway.sol",
    "SnapshotGateway.json",
  );
}

export function publicManifestPath(snapshotIdHash) {
  return path.join(projectDirectory, "public-manifests", `${snapshotIdHash}.json`);
}

export async function openGateway() {
  const rpcUrl = process.env.PUBLIC_CHAIN_RPC_URL ?? "http://127.0.0.1:8545";
  const provider = new JsonRpcProvider(rpcUrl);
  const network = await provider.getNetwork();
  const chainId = network.chainId.toString();
  const deployment = readJson(path.join(projectDirectory, "deployments", `${chainId}.json`));
  const artifact = readJson(artifactPath());
  return { provider, chainId, deployment, artifact };
}

export async function publishCandidate(candidate) {
  const candidateChecks = validateCandidate(candidate);
  const routeState = await fetchCurrentRouteState(candidate.batchId);
  if (!candidateMatchesRoute(candidate, routeState)) {
    throw new Error("The publication candidate does not match the current private route.");
  }
  const runtime = await openGateway();
  const { provider, chainId, deployment, artifact } = runtime;
  const signer = process.env.RELAYER_PRIVATE_KEY?.trim()
    ? new (await import("ethers")).Wallet(process.env.RELAYER_PRIVATE_KEY.trim(), provider)
    : await provider.getSigner(0);
  const gateway = new Contract(deployment.contractAddress, artifact.abi, signer);

  if (BigInt(chainId) <= 0n) throw new Error("The destination chain ID is invalid.");
  if (!(await gateway.allowedSourceNetworks(deployment.sourceNetworkId))) {
    throw new Error("The deployed gateway does not allow the configured source network.");
  }

  let nonce = BigInt(Date.now());
  while (await gateway.usedNonces(deployment.sourceNetworkId, nonce)) nonce += 1n;
  const request = {
    protocol: candidate.protocol,
    snapshotId: candidate.snapshotId,
    batchId: candidate.batchId,
    publicRoot: candidate.publicRoot,
    manifestHash: candidate.manifestHash,
    sourceBlockHash: candidate.sourceBlockHash,
    sourceNetworkId: deployment.sourceNetworkId,
    destinationChainId: BigInt(chainId),
    nonce,
    snapshotVersion: candidate.snapshotVersion,
  };

  const transaction = await gateway.publishSnapshot(request);
  const receipt = await transaction.wait();
  const record = await gateway.getSnapshot(candidate.snapshotIdHash);
  const publication = {
    candidate,
    chain: {
      contractAddress: deployment.contractAddress,
      chainId,
      sourceNetworkId: deployment.sourceNetworkId,
      transactionHash: transaction.hash,
      blockNumber: receipt.blockNumber,
      nonce: nonce.toString(),
      publisher: record.publisher,
      publishedAt: new Date(Number(record.publishedAt) * 1000).toISOString(),
      status: statusNames[Number(record.status)] ?? "Unknown",
    },
    verification: candidateChecks,
  };

  const directory = path.join(projectDirectory, "public-manifests");
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(
    publicManifestPath(candidate.snapshotIdHash),
    `${JSON.stringify(publication, null, 2)}\n`,
    "utf8",
  );
  return publication;
}

async function tracePublishedSnapshot(
  runtime,
  gateway,
  snapshotHash,
  {
    expectedBatchId = "",
    expectedSnapshotId = "",
    missingManifestIsError = false,
    requireActive = false,
  } = {},
) {
  const normalizedSnapshotHash = snapshotHash.toLowerCase();
  const filePath = publicManifestPath(normalizedSnapshotHash);
  if (!fs.existsSync(filePath)) {
    if (missingManifestIsError) {
      throw new Error("The chain record exists, but its public manifest is unavailable.");
    }
    return null;
  }

  const publication = readJson(filePath);
  const candidate = publication.candidate;
  const candidateChecks = validateCandidate(candidate);
  if (expectedBatchId && candidate.batchId !== expectedBatchId) return null;
  if (expectedSnapshotId && candidate.snapshotId !== expectedSnapshotId) return null;

  const currentSnapshotHash = await gateway.currentSnapshotByBatch(id(candidate.batchId));
  if (currentSnapshotHash === ZeroHash ||
      currentSnapshotHash.toLowerCase() !== normalizedSnapshotHash) {
    return null;
  }

 const routeState = await fetchCurrentRouteState(candidate.batchId);
 if (!candidateMatchesRoute(candidate, routeState)) return null;
  let localStatus = null;
  try {
    localStatus = await fetchSnapshotStatus(candidate.batchId);
  } catch {
    // The public-chain verification remains usable if the local hot status is unavailable.
  }
  if (localStatus?.snapshotId !== candidate.snapshotId) localStatus = null;

 const record = await gateway.getSnapshot(snapshotHash);
  const status = statusNames[Number(record.status)] ?? "Unknown";
  if (requireActive && status !== "Active") return null;
  const availability = snapshotAvailability(candidate.manifest);
  if (!availability.available) {
    throw new SnapshotAvailabilityError(
      availability.state,
      candidate.batchId,
      availability.availableFrom,
      availability.availableUntil,
    );
  }
  const { chainId, deployment } = runtime;
  const chainChecks = {
    snapshotId: record.snapshotId.toLowerCase() === candidate.snapshotIdHash.toLowerCase(),
    batchId: record.batchIdHash.toLowerCase() === candidate.batchIdHash.toLowerCase(),
    publicRoot: record.publicRoot.toLowerCase() === candidate.publicRoot.toLowerCase(),
    manifestHash: record.manifestHash.toLowerCase() === candidate.manifestHash.toLowerCase(),
    sourceBlockHash: record.sourceBlockHash.toLowerCase() === candidate.sourceBlockHash.toLowerCase(),
    sourceNetwork: record.sourceNetworkId.toLowerCase() === deployment.sourceNetworkId.toLowerCase(),
    destinationChain: record.destinationChainId === BigInt(chainId),
  };
  const integrityVerified = [
    ...Object.values(candidateChecks),
    ...Object.values(chainChecks),
  ].every(Boolean);

  return {
    batchId: candidate.batchId,
    snapshotId: candidate.snapshotId,
    status,
    integrityVerified,
    verified: integrityVerified && status === "Active",
    manifest: candidate.manifest,
    availability,
   evidence: candidate.manifest.public_evidence ?? [],
   checks: { candidate: candidateChecks, chain: chainChecks },
    snapshotPublishedAt: localStatus?.publishedAt ??
      new Date(Number(record.publishedAt) * 1000).toISOString(),
    latestVerificationAt: localStatus?.latestVerificationAt ?? "",
    verificationStatus: localStatus?.verificationStatus ?? "",
    verificationMessage: localStatus?.verificationMessage ?? "",
   technical: {
      publicRoot: record.publicRoot,
      manifestHash: record.manifestHash,
      sourceBlockHash: record.sourceBlockHash,
      snapshotIdHash: record.snapshotId,
      sourceNetworkId: record.sourceNetworkId,
      contractAddress: deployment.contractAddress,
      chainId,
      nonce: record.nonce.toString(),
      publisher: record.publisher,
      publishedAt: new Date(Number(record.publishedAt) * 1000).toISOString(),
      transactionHash: publication.chain.transactionHash,
      blockNumber: publication.chain.blockNumber,
    },
  };
}

export async function traceBatch(batchId) {
  if (typeof batchId !== "string" || batchId.length === 0 || batchId.length > 256) {
    throw new Error("A valid Batch ID is required.");
  }
  const runtime = await openGateway();
  const gateway = new Contract(
    runtime.deployment.contractAddress,
    runtime.artifact.abi,
    runtime.provider,
  );
  const currentSnapshotHash = await gateway.currentSnapshotByBatch(id(batchId));
  if (currentSnapshotHash === ZeroHash) return null;
  return tracePublishedSnapshot(runtime, gateway, currentSnapshotHash, {
    expectedBatchId: batchId,
    missingManifestIsError: true,
  });
}

export async function traceSnapshot(snapshotId) {
  if (typeof snapshotId !== "string" ||
      snapshotId.length === 0 || snapshotId.length > 256) {
    throw new Error("A valid Snapshot ID is required.");
  }
  const runtime = await openGateway();
  const gateway = new Contract(
    runtime.deployment.contractAddress,
    runtime.artifact.abi,
    runtime.provider,
  );
  return tracePublishedSnapshot(runtime, gateway, id(snapshotId), {
    expectedSnapshotId: snapshotId,
    requireActive: true,
  });
}

export async function listPublishedBatches() {
  const directory = path.join(projectDirectory, "public-manifests");
  if (!fs.existsSync(directory)) return [];

  const files = fs.readdirSync(directory)
    .filter((fileName) => fileName.endsWith(".json"))
    .sort();
  if (files.length === 0) return [];

  const { provider, deployment, artifact } = await openGateway();
  const gateway = new Contract(deployment.contractAddress, artifact.abi, provider);
  const batches = new Map();
  const routeStates = new Map();

  for (const fileName of files) {
    const publication = readJson(path.join(directory, fileName));
    const candidate = publication.candidate;
    validateCandidate(candidate);
    if (!snapshotAvailability(candidate.manifest).available) continue;

    const currentSnapshotHash = await gateway.currentSnapshotByBatch(
      id(candidate.batchId),
    );
    const candidateSnapshotHash = id(candidate.snapshotId);
    if (currentSnapshotHash === ZeroHash ||
        currentSnapshotHash.toLowerCase() !== candidateSnapshotHash.toLowerCase()) {
      continue;
    }

    if (!routeStates.has(candidate.batchId)) {
      routeStates.set(candidate.batchId, await fetchCurrentRouteState(candidate.batchId));
    }
    if (!candidateMatchesRoute(candidate, routeStates.get(candidate.batchId))) continue;

    const record = await gateway.getSnapshot(currentSnapshotHash);
    const manifestBatch = candidate.manifest?.batch ?? {};
    batches.set(candidate.batchId, {
      batchId: candidate.batchId,
      product: manifestBatch.product_name ?? candidate.batchId,
      category: manifestBatch.category ?? "",
      availabilityState: snapshotAvailability(candidate.manifest).state,
      status: statusNames[Number(record.status)] ?? "Unknown",
      verified: Number(record.status) === 1,
      snapshotId: candidate.snapshotId,
      snapshotIdHash: candidateSnapshotHash,
    });
  }

  return [...batches.values()].sort((left, right) =>
    left.batchId.localeCompare(right.batchId));
}
