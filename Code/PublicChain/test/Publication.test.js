import { expect } from "chai";
import { id, keccak256, toUtf8Bytes } from "ethers";
import { buildPublicRoot, validateCandidate } from "../scripts/publication.js";

function candidateFixture() {
  const manifest = {
    protocol: "Supermarket-Trace-v1",
    batch: {
      batch_id: "BATCH-POTATO-0001",
      product_name: "Potatoes",
    },
  };
  const manifestCanonical = JSON.stringify(manifest);
  const publicFields = [
    { name: "protocol", value: manifest.protocol },
    { name: "batch.batch_id", value: manifest.batch.batch_id },
    { name: "batch.product_name", value: manifest.batch.product_name },
  ];
  const snapshotId = "SNAP-BATCH-POTATO-0001-V0001";
  return {
    protocol: manifest.protocol,
    snapshotId,
    snapshotVersion: 1,
    generatedAt: "2026-08-13T00:00:00Z",
    batchId: manifest.batch.batch_id,
    protocolHash: id(manifest.protocol),
    snapshotIdHash: id(snapshotId),
    batchIdHash: id(manifest.batch.batch_id),
    publicRoot: buildPublicRoot(publicFields),
    manifestHash: keccak256(toUtf8Bytes(manifestCanonical)),
    sourceBlockHash: id("private-block"),
    manifestCanonical,
    manifest,
    publicFields,
  };
}

describe("Publication candidate verification", function () {
  it("rebuilds the duplicate-last SHA-256 Merkle Root", function () {
    const candidate = candidateFixture();
    expect(buildPublicRoot(candidate.publicFields)).to.equal(candidate.publicRoot);
  });

  it("accepts a self-consistent candidate", function () {
    expect(Object.values(validateCandidate(candidateFixture()))).to.deep.equal([
      true,
      true,
      true,
      true,
      true,
    ]);
  });

  it("rejects a changed public field", function () {
    const candidate = candidateFixture();
    candidate.publicFields[2].value = "Changed product";
    expect(() => validateCandidate(candidate)).to.throw("publicRoot");
  });
});
