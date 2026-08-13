import { network } from "hardhat";
import {
  loadDeployment,
  readJson,
  resolvePayloadPath,
  statusName,
} from "./runtime.js";

const { ethers } = await network.connect();
const networkInfo = await ethers.provider.getNetwork();
const chainId = networkInfo.chainId.toString();
const deployment = loadDeployment(chainId);
const payloadPath = resolvePayloadPath();
const payload = readJson(payloadPath);

function requireBytes32(name, value) {
  if (typeof value !== "string" || !/^0x[0-9a-fA-F]{64}$/.test(value)) {
    throw new Error(`${name} must be a 0x-prefixed bytes32 value.`);
  }
}

for (const name of [
  "protocolHash",
  "snapshotIdHash",
  "batchIdHash",
  "publicRoot",
  "manifestHash",
  "sourceBlockHash",
  "sourceNetworkId",
]) {
  requireBytes32(name, payload[name]);
}

if (BigInt(payload.destinationChainId) !== networkInfo.chainId) {
  throw new Error(
    `Payload chain ${payload.destinationChainId} does not match connected ` +
      `chain ${chainId}.`,
  );
}
if (ethers.id(payload.protocol) !== payload.protocolHash.toLowerCase()) {
  throw new Error("protocolHash does not match protocol.");
}
if (ethers.id(payload.snapshotId) !== payload.snapshotIdHash.toLowerCase()) {
  throw new Error("snapshotIdHash does not match snapshotId.");
}
if (ethers.id(payload.batchId) !== payload.batchIdHash.toLowerCase()) {
  throw new Error("batchIdHash does not match batchId.");
}

const gateway = await ethers.getContractAt(
  "SnapshotGateway",
  deployment.contractAddress,
);

if (!(await gateway.allowedSourceNetworks(payload.sourceNetworkId))) {
  throw new Error("The payload source network is not allowed by the gateway.");
}

const request = {
  protocol: payload.protocol,
  snapshotId: payload.snapshotId,
  batchId: payload.batchId,
  publicRoot: payload.publicRoot,
  manifestHash: payload.manifestHash,
  sourceBlockHash: payload.sourceBlockHash,
  sourceNetworkId: payload.sourceNetworkId,
  destinationChainId: BigInt(payload.destinationChainId),
  nonce: BigInt(payload.nonce),
  snapshotVersion: Number(payload.snapshotVersion),
};

console.log(`Publishing payload: ${payloadPath}`);
console.log(`Gateway: ${deployment.contractAddress}`);
const transaction = await gateway.publishSnapshot(request);
console.log(`Submitted transaction: ${transaction.hash}`);

const receipt = await transaction.wait();
const record = await gateway.getSnapshot(payload.snapshotIdHash);

console.log(`Confirmed in block: ${receipt.blockNumber}`);
console.log(`Gas used: ${receipt.gasUsed}`);
console.log(`Snapshot ID hash: ${payload.snapshotIdHash}`);
console.log(`Status: ${statusName(record.status)}`);
