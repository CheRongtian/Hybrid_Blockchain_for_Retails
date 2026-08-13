import { expect } from "chai";
import { network } from "hardhat";

const { ethers } = await network.connect();

const protocol = "Supermarket-Trace-v1";
const sourceNetworkId = ethers.id("supermarket-private-local-v1");
const testChainId = (await ethers.provider.getNetwork()).chainId;

function request(snapshotId, batchId, nonce, overrides = {}) {
  return {
    protocol,
    snapshotId,
    batchId,
    publicRoot: ethers.id(`public-root:${snapshotId}`),
    manifestHash: ethers.id(`manifest:${snapshotId}`),
    sourceBlockHash: ethers.id(`source-block:${snapshotId}`),
    sourceNetworkId,
    destinationChainId: testChainId,
    nonce,
    snapshotVersion: 1,
    ...overrides,
  };
}

async function deployGateway() {
  const gateway = await ethers.deployContract("SnapshotGateway");
  await gateway.waitForDeployment();
  await (await gateway.setSourceNetwork(sourceNetworkId, true)).wait();
  return gateway;
}

describe("SnapshotGateway", function () {
  it("publishes and queries an active snapshot", async function () {
    const gateway = await deployGateway();
    const publishRequest = request("SNAP-BATCH-0001-A", "BATCH-0001", 1n);

    await expect(gateway.publishSnapshot(publishRequest))
      .to.emit(gateway, "SnapshotPublished");

    const record = await gateway.getCurrentSnapshot("BATCH-0001");
    expect(record.publicRoot).to.equal(publishRequest.publicRoot);
    expect(record.status).to.equal(1n);
  });

  it("rejects unauthorized publishers", async function () {
    const gateway = await deployGateway();
    const [, outsider] = await ethers.getSigners();

    await expect(
      gateway.connect(outsider).publishSnapshot(
        request("SNAP-BATCH-0001-A", "BATCH-0001", 1n),
      ),
    ).to.be.revertedWithCustomError(gateway, "Unauthorized");
  });

  it("rejects duplicate snapshot IDs and source-network nonce replay", async function () {
    const gateway = await deployGateway();
    const first = request("SNAP-BATCH-0001-A", "BATCH-0001", 1n);
    await gateway.publishSnapshot(first);

    await expect(gateway.publishSnapshot(first))
      .to.be.revertedWithCustomError(gateway, "SnapshotAlreadyExists");

    await expect(
      gateway.publishSnapshot(
        request("SNAP-BATCH-0002-A", "BATCH-0002", 1n),
      ),
    ).to.be.revertedWithCustomError(gateway, "NonceAlreadyUsed");
  });

  it("rejects invalid protocol, version, chain, source, and root", async function () {
    const gateway = await deployGateway();
    const base = request("SNAP-BATCH-0001-A", "BATCH-0001", 1n);

    await expect(gateway.publishSnapshot({ ...base, protocol: "Other-v1" }))
      .to.be.revertedWithCustomError(gateway, "InvalidProtocol");
    await expect(gateway.publishSnapshot({ ...base, snapshotVersion: 2 }))
      .to.be.revertedWithCustomError(gateway, "InvalidSnapshotVersion");
    await expect(gateway.publishSnapshot({ ...base, destinationChainId: 1n }))
      .to.be.revertedWithCustomError(gateway, "InvalidDestinationChain");
    await expect(gateway.publishSnapshot({
      ...base,
      sourceNetworkId: ethers.id("unknown-network"),
    })).to.be.revertedWithCustomError(gateway, "UnsupportedSourceNetwork");
    await expect(gateway.publishSnapshot({
      ...base,
      publicRoot: ethers.ZeroHash,
    })).to.be.revertedWithCustomError(gateway, "InvalidHashField");
  });

  it("supersedes the current snapshot and preserves ordered history", async function () {
    const gateway = await deployGateway();
    const first = request("SNAP-BATCH-0001-A", "BATCH-0001", 1n);
    const second = request("SNAP-BATCH-0001-B", "BATCH-0001", 2n);
    await gateway.publishSnapshot(first);
    await gateway.publishSnapshot(second);

    const firstRecord = await gateway.getSnapshot(ethers.id(first.snapshotId));
    const secondRecord = await gateway.getSnapshot(
      ethers.id(second.snapshotId),
    );
    const history = await gateway.getBatchHistory("BATCH-0001");

    expect(firstRecord.status).to.equal(2n);
    expect(secondRecord.status).to.equal(1n);
    expect(history).to.deep.equal([
      ethers.id(first.snapshotId),
      ethers.id(second.snapshotId),
    ]);
  });

  it("recalls a batch and revokes a snapshot", async function () {
    const gateway = await deployGateway();
    const first = request("SNAP-BATCH-0001-A", "BATCH-0001", 1n);
    await gateway.publishSnapshot(first);
    await gateway.recallBatch("BATCH-0001", ethers.id("recall reason"));

    const recalled = await gateway.getSnapshot(ethers.id(first.snapshotId));
    expect(recalled.status).to.equal(3n);
    await expect(
      gateway.publishSnapshot(
        request("SNAP-BATCH-0001-B", "BATCH-0001", 2n),
      ),
    ).to.be.revertedWithCustomError(gateway, "BatchIsRecalled");

    const secondGateway = await deployGateway();
    const second = request("SNAP-BATCH-0002-A", "BATCH-0002", 3n);
    await secondGateway.publishSnapshot(second);
    await secondGateway.revokeSnapshot(
      ethers.id(second.snapshotId),
      ethers.id("revoke reason"),
    );
    const revoked = await secondGateway.getSnapshot(
      ethers.id(second.snapshotId),
    );
    expect(revoked.status).to.equal(4n);
  });

  it("enforces publisher roles and pause state", async function () {
    const gateway = await deployGateway();
    const [, publisher] = await ethers.getSigners();
    await gateway.setPublisher(publisher.address, true);
    await gateway.connect(publisher).publishSnapshot(
      request("SNAP-BATCH-0001-A", "BATCH-0001", 1n),
    );

    await gateway.setPaused(true);
    await expect(
      gateway.publishSnapshot(
        request("SNAP-BATCH-0002-A", "BATCH-0002", 2n),
      ),
    ).to.be.revertedWithCustomError(gateway, "ContractPaused");
  });
});
