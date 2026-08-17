const batchSelect = document.querySelector("#batch-select");
const statusLine = document.querySelector("#search-status");
const resultSection = document.querySelector("#trace-result");
let currentBatchId = "";
let currentSnapshotId = "";
let batchRefreshInFlight = false;

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

function routeStageDetails(stage, manifest) {
  if (stage.stage === "supplier") {
    return {
      title: "Supplier",
      primary: stage.location || manifest.origin.farm_location,
      secondary: `Harvested ${stage.harvest_date || manifest.origin.harvest_date}`,
    };
  }
  if (stage.stage === "logistics") {
    return {
      title: "Logistics",
      primary: `${stage.pickup_location} → ${stage.delivery_location}`,
      secondary: `${measurement(stage.temperature)} · ${measurement(stage.humidity)}`,
    };
  }
  if (stage.stage === "warehouse") {
    return {
      title: "Warehouse",
      primary: `${stage.inbound_local_time} → ${stage.outbound_local_time}`,
      secondary: `${measurement(stage.temperature)} · ${measurement(stage.humidity)}`,
    };
  }
  return {
    title: "Supermarket",
    primary: stage.store_location_id || manifest.retail.store_location_id,
    secondary: `Shelved ${stage.shelf_placement_date || manifest.retail.shelf_placement_date} · ` +
      `Sell by ${stage.sell_by_date || manifest.retail.sell_by_date}`,
  };
}

function renderRoute(manifest) {
  const route = document.querySelector("#trace-route");
  route.replaceChildren();
  const stages = Array.isArray(manifest.route) && manifest.route.length > 0
    ? manifest.route
    : [
        {
          stage: "supplier",
          location: manifest.origin.farm_location,
          harvest_date: manifest.origin.harvest_date,
        },
        { stage: "logistics", ...(manifest.transport || {}) },
        { stage: "warehouse", ...(manifest.storage || {}) },
        { stage: "supermarket", ...(manifest.retail || {}) },
      ];

  stages.forEach((stage, index) => {
    if (index > 0) {
      const connector = document.createElement("span");
      connector.className = "connector";
      connector.setAttribute("aria-hidden", "true");
      connector.textContent = "→";
      route.append(connector);
    }
    const details = routeStageDetails(stage, manifest);
    const card = document.createElement("article");
    card.className = "route-node";
    const sequence = document.createElement("span");
    sequence.className = "route-index";
    sequence.textContent = String(index + 1);
    const title = document.createElement("p");
    title.textContent = details.title;
    const primary = document.createElement("strong");
    primary.textContent = details.primary || "Not disclosed";
    const secondary = document.createElement("small");
    secondary.textContent = details.secondary || "Not disclosed";
    card.append(sequence, title, primary, secondary);
    route.append(card);
  });
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
  renderRoute(manifest);
  text("#route-state", manifest.verification.route_completed ? "Route completed" : "Route incomplete");
  renderEvidence(trace.evidence);
  renderTechnical(trace.technical);
  renderChecks(trace.checks);
  resultSection.hidden = false;
}

async function search(batchId, { background = false } = {}) {
  if (!background) {
    statusLine.className = "status";
    statusLine.textContent = "Reading public-chain trace...";
    resultSection.hidden = true;
    batchSelect.disabled = true;
  }
  try {
    const response = await fetch(`/api/trace/${encodeURIComponent(batchId)}`);
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
    renderTrace(payload);
    currentBatchId = batchId;
    currentSnapshotId = payload.snapshotId || "";
    statusLine.textContent = background
      ? "The trace has been updated to the latest active snapshot."
      : payload.verified
        ? "Public Manifest and Merkle Root match the active chain record."
        : "The trace requires attention. Review its status and verification details.";
    statusLine.className = payload.verified ? "status" : "status error";
  } catch (error) {
    statusLine.textContent = error.message;
    statusLine.className = "status error";
  } finally {
    if (!background) batchSelect.disabled = false;
  }
}

async function loadPublishedBatches({ background = false } = {}) {
  if (batchRefreshInFlight) return;
  batchRefreshInFlight = true;
  const selectedBatchId = batchSelect.value;
  if (!background) {
    statusLine.className = "status";
    statusLine.textContent = "Loading published product batches...";
  }
  try {
    const response = await fetch("/api/batches");
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
    const batches = payload.batches || [];

    batchSelect.replaceChildren();
    const placeholder = document.createElement("option");
    placeholder.value = "";
    placeholder.textContent = "Choose a product batch";
    batchSelect.append(placeholder);

    for (const batch of batches) {
      const option = document.createElement("option");
      option.value = batch.batchId;
      option.textContent = `${batch.product} · ${batch.batchId}`;
      batchSelect.append(option);
    }

    const selectedBatch = batches.find((batch) => batch.batchId === selectedBatchId);
    if (selectedBatch) batchSelect.value = selectedBatchId;
    const hasBatches = batches.length > 0;
    batchSelect.disabled = !hasBatches;
    if (!background) {
      statusLine.textContent = hasBatches
        ? "Choose a published product batch to view its trace."
        : "No published product batches are available yet.";
      statusLine.className = hasBatches ? "status" : "status error";
    }

    if (selectedBatch && currentBatchId === selectedBatchId &&
        selectedBatch.snapshotId && selectedBatch.snapshotId !== currentSnapshotId) {
      await search(selectedBatchId, { background: true });
    } else if (!selectedBatch && currentBatchId === selectedBatchId) {
      currentBatchId = "";
      currentSnapshotId = "";
      resultSection.hidden = true;
      statusLine.textContent = hasBatches
        ? "The previous trace is no longer active. Choose a product batch."
        : "No published product batches are available yet.";
      statusLine.className = hasBatches ? "status" : "status error";
    }
  } catch (error) {
    batchSelect.replaceChildren();
    batchSelect.disabled = true;
    resultSection.hidden = true;
    currentBatchId = "";
    currentSnapshotId = "";
    statusLine.textContent = error.message;
    statusLine.className = "status error";
  } finally {
    batchRefreshInFlight = false;
  }
}

batchSelect.addEventListener("change", () => {
  if (batchSelect.value) search(batchSelect.value);
});

loadPublishedBatches();
window.setInterval(() => loadPublishedBatches({ background: true }), 5000);
