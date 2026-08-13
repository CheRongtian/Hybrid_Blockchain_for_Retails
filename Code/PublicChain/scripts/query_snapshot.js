import { network } from "hardhat";
import { loadDeployment, statusName } from "./runtime.js";

const { ethers } = await network.connect();
const networkInfo = await ethers.provider.getNetwork();
const chainId = networkInfo.chainId.toString();
const deployment = loadDeployment(chainId);
const gateway = await ethers.getContractAt(
  "SnapshotGateway",
  deployment.contractAddress,
);

const batchId = process.env.QUERY_BATCH_ID ?? "BATCH-ORANGES-0001";
const configuredSnapshotHash = process.env.QUERY_SNAPSHOT_HASH?.trim();
const batchIdHash = ethers.id(batchId);
const currentSnapshotHash = await gateway.currentSnapshotByBatch(batchIdHash);
const snapshotHash = configuredSnapshotHash || currentSnapshotHash;

console.log(`Gateway: ${deployment.contractAddress}`);
console.log(`Batch ID: ${batchId}`);
console.log(`Batch ID hash: ${batchIdHash}`);

if (currentSnapshotHash === ethers.ZeroHash) {
  console.log("Current snapshot: none");
} else {
  console.log(`Current snapshot: ${currentSnapshotHash}`);
}

const history = await gateway.getBatchHistory(batchId);
console.log(`History (${history.length}):`);
for (const item of history) console.log(`  ${item}`);

if (snapshotHash !== ethers.ZeroHash) {
  const record = await gateway.getSnapshot(snapshotHash);
  console.log("Snapshot record:");
  console.log(`  Snapshot ID hash: ${record.snapshotId}`);
  console.log(`  Public Root: ${record.publicRoot}`);
  console.log(`  Manifest hash: ${record.manifestHash}`);
  console.log(`  Source block hash: ${record.sourceBlockHash}`);
  console.log(`  Source network ID: ${record.sourceNetworkId}`);
  console.log(`  Destination chain ID: ${record.destinationChainId}`);
  console.log(`  Nonce: ${record.nonce}`);
  console.log(`  Snapshot version: ${record.snapshotVersion}`);
  console.log(`  Publisher: ${record.publisher}`);
  console.log(
    `  Published at: ${new Date(Number(record.publishedAt) * 1000).toISOString()}`,
  );
  console.log(`  Status: ${statusName(record.status)}`);
}
