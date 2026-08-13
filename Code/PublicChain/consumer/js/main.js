const batchSelect = document.querySelector("#batch-select");
const statusLine = document.querySelector("#search-status");
const resultSection = document.querySelector("#trace-result");

const text = (selector, value, fallback = "Not disclosed") => {
  document.querySelector(selector).textContent = value || fallback;
};

function measurement(value) {
  if (!value) return "Not disclosed";
  const range = value.minimum === value.maximum
    ? `${value.minimum}`
    : `${value.minimum}–${value.maximum}`;
  const unit = value.unit === "percent_rh" ? "% RH" : `°${value.unit}`;
  return `${range} ${unit}`;
}

function renderChecks(checks) {
  const list = document.querySelector("#check-list");
  list.replaceChildren();
  for (const [group, values] of Object.entries(checks)) {
    for (const [name, valid] of Object.entries(values)) {
      const item = document.createElement("span");
      item.className = `check${valid ? "" : " failed"}`;
      item.textContent = `${group}.${name}: ${valid ? "pass" : "fail"}`;
      list.append(item);
    }
  }
}

function renderTechnical(values) {
  const list = document.querySelector("#technical-details");
  list.replaceChildren();
  const labels = {
    publicRoot: "Public Root",
    manifestHash: "Manifest Hash",
    sourceBlockHash: "Final Private Block Hash",
    snapshotIdHash: "Snapshot ID Hash",
    transactionHash: "Transaction Hash",
    contractAddress: "Contract Address",
    blockNumber: "Block Number",
    chainId: "Chain ID",
    nonce: "Nonce",
    publisher: "Publisher",
    publishedAt: "Published At (UTC)",
  };
  for (const [name, label] of Object.entries(labels)) {
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = label;
    detail.textContent = values[name] ?? "Unavailable";
    list.append(term, detail);
  }
}

function renderEvidence(evidence) {
  const section = document.querySelector("#evidence-section");
  const list = document.querySelector("#evidence-list");
  list.replaceChildren();
  section.hidden = evidence.length === 0;
  for (const item of evidence) {
    const card = document.createElement("article");
    card.className = "evidence-item";
    const title = document.createElement("strong");
    const cid = document.createElement("code");
    title.textContent = `${item.type} · ${item.stage}`;
    cid.textContent = item.cid;
    card.append(title, cid);
    list.append(card);
  }
}

function renderTrace(trace) {
  const manifest = trace.manifest;
  const badge = document.querySelector("#verification-badge");
  badge.className = `badge ${trace.verified ? "verified" :
    trace.integrityVerified ? "warning" : "failed"}`;
  badge.textContent = trace.verified ? "Verified" :
    trace.integrityVerified ? trace.status : "Verification Failed";

  text("#product-name", manifest.batch.product_name);
  text("#batch-summary", `${manifest.batch.batch_id} · Snapshot ${trace.snapshotId}`);
  text("#origin", manifest.origin.farm_location);
  text("#harvest-date", manifest.origin.harvest_date);
  text("#category", manifest.batch.category);
  text("#chain-status", trace.status);
  text("#supplier-location", manifest.origin.farm_location);
  text("#supplier-date", `Harvested ${manifest.origin.harvest_date}`);
  text("#transport-route",
    `${manifest.transport.pickup_location} → ${manifest.transport.delivery_location}`);
  text("#transport-condition",
    `${measurement(manifest.transport.temperature)} · ${measurement(manifest.transport.humidity)}`);
  text("#storage-time",
    `${manifest.storage.inbound_local_time} → ${manifest.storage.outbound_local_time}`);
  text("#storage-condition",
    `${measurement(manifest.storage.temperature)} · ${measurement(manifest.storage.humidity)}`);
  text("#store-location", manifest.retail.store_location_id);
  text("#retail-dates",
    `Shelved ${manifest.retail.shelf_placement_date} · Sell by ${manifest.retail.sell_by_date}`);
  text("#route-state", manifest.verification.route_completed ? "Route completed" : "Route incomplete");
  renderEvidence(trace.evidence);
  renderTechnical(trace.technical);
  renderChecks(trace.checks);
  resultSection.hidden = false;
}

async function search(batchId) {
  statusLine.className = "status";
  statusLine.textContent = "Reading public-chain trace...";
  resultSection.hidden = true;
  batchSelect.disabled = true;
  try {
    const response = await fetch(`/api/trace/${encodeURIComponent(batchId)}`);
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
    renderTrace(payload);
    statusLine.textContent = payload.verified
      ? "Public Manifest and Merkle Root match the active chain record."
      : "The trace requires attention. Review its status and verification details.";
    statusLine.className = payload.verified ? "status" : "status error";
  } catch (error) {
    statusLine.textContent = error.message;
    statusLine.className = "status error";
  } finally {
    batchSelect.disabled = false;
  }
}

async function loadPublishedBatches() {
  statusLine.className = "status";
  statusLine.textContent = "Loading published product batches...";
  try {
    const response = await fetch("/api/batches");
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);

    batchSelect.replaceChildren();
    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Choose a product batch";
    batchSelect.append(placeholder);

    for (const batch of payload.batches || []) {
      const option = document.createElement("option");
      option.value = batch.batchId;
      option.textContent = `${batch.product} · ${batch.batchId}`;
      batchSelect.append(option);
    }

    const hasBatches = (payload.batches || []).length > 0;
    batchSelect.disabled = !hasBatches;
    statusLine.textContent = hasBatches
      ? "Choose a published product batch to view its trace."
      : "No published product batches are available yet.";
    statusLine.className = hasBatches ? "status" : "status error";
  } catch (error) {
    batchSelect.replaceChildren();
    batchSelect.disabled = true;
    statusLine.textContent = error.message;
    statusLine.className = "status error";
  }
}

batchSelect.addEventListener("change", () => {
  if (batchSelect.value) search(batchSelect.value);
});

loadPublishedBatches();
