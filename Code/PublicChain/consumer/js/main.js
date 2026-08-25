const batchSelect = document.querySelector("#batch-select");
const statusLine = document.querySelector("#search-status");
const resultSection = document.querySelector("#trace-result");
const routeDetail = document.querySelector("#route-detail");
const routeDetailTitle = document.querySelector("#route-detail-title");
const routeDetailIndex = document.querySelector("#route-detail-index");
const routeDetailPrimary = document.querySelector("#route-detail-primary");
const routeDetailSecondary = document.querySelector("#route-detail-secondary");
const verificationOnly = new URLSearchParams(window.location.search).get("view") ===
  "verification";
let currentBatchId = "";
let currentSnapshotId = "";
let requestedSnapshotId = "";
let requestedBatchId = "";
let batchRefreshInFlight = false;
let liveEventSource = null;
let liveRefreshInFlight = false;
let liveRefreshQueued = false;
let liveRefreshQueuedType = "";
let availabilityTimer = 0;

if (verificationOnly) {
  document.body.classList.add("verification-only");
  document.title = "Verification Result";
}

function setVerificationReady(ready) {
  if (verificationOnly) document.body.classList.toggle("verification-ready", ready);
}

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

function routeStageLabel(stages, index, manifest) {
  const details = routeStageDetails(stages[index], manifest);
  const occurrence = stages
    .slice(0, index + 1)
    .filter((stage) => stage.stage === stages[index].stage)
    .length;
  return occurrence > 1 ? `${details.title} ${occurrence}` : details.title;
}

function selectRouteStage(stages, manifest, index, selectedButton) {
  const details = routeStageDetails(stages[index], manifest);
  document.querySelectorAll(".route-overview-node").forEach((button) => {
    button.removeAttribute("aria-current");
  });
  selectedButton.setAttribute("aria-current", "step");
  routeDetailTitle.textContent = routeStageLabel(stages, index, manifest);
  routeDetailIndex.textContent = `Stage ${index + 1} of ${stages.length}`;
  routeDetailPrimary.textContent = details.primary || "Not disclosed";
  routeDetailSecondary.textContent = details.secondary || "Not disclosed";
  routeDetail.hidden = false;
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

  if (verificationOnly) {
    const sequence = document.createElement("p");
    sequence.className = "route-overview-sequence";
    sequence.textContent = stages
      .map((stage, index) => routeStageLabel(stages, index, manifest))
      .join(" → ");
    route.append(sequence);

    const overview = document.createElement("div");
    overview.className = "route-overview-grid";
    stages.forEach((stage, index) => {
      const label = routeStageLabel(stages, index, manifest);
      const button = document.createElement("button");
      button.type = "button";
      button.className = "route-overview-node";
      button.setAttribute("aria-label", `View ${label}, stage ${index + 1}`);
      const sequenceNumber = document.createElement("span");
      sequenceNumber.className = "route-index";
      sequenceNumber.textContent = String(index + 1);
      const title = document.createElement("strong");
      title.textContent = label;
      button.append(sequenceNumber, title);
      button.addEventListener("click", () =>
        selectRouteStage(stages, manifest, index, button));
      overview.append(button);
    });
    route.append(overview);
    selectRouteStage(stages, manifest, 0, overview.firstElementChild);
    return;
  }

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

function formatTimestamp(value) {
  if (!value) return "";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  const pad = (part) => String(part).padStart(2, "0");
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())} ` +
    `${pad(date.getHours())}:${pad(date.getMinutes())}:${pad(date.getSeconds())}`;
}

function clearAvailabilityTimer() {
  if (!availabilityTimer) return;
  window.clearTimeout(availabilityTimer);
  availabilityTimer = 0;
}

function scheduleAvailabilityBoundary(timestamp, batchId) {
  clearAvailabilityTimer();
  const boundary = Date.parse(timestamp);
  if (!batchId || !Number.isFinite(boundary)) return;
  const maximumDelay = 2_147_000_000;
  const arm = () => {
    const remaining = boundary - Date.now();
    if (remaining > 0) {
      availabilityTimer = window.setTimeout(
        arm,
        Math.min(remaining + 250, maximumDelay),
      );
      return;
    }
    availabilityTimer = 0;
    void (async () => {
      await loadPublishedBatches({ background: true });
      await searchBatch(batchId, { background: true });
    })();
  };
  arm();
}

function renderTrace(trace) {
  const manifest = trace.manifest;
  const availabilityState = trace.availability?.state || "legacy";
  const offShelf = availabilityState === "expired";
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
  text("#availability-status", offShelf ? "Off shelf" : "On shelf");
  const snapshotPublishedAt = trace.snapshotPublishedAt || trace.technical?.publishedAt;
  const latestVerificationAt = trace.latestVerificationAt;
  const lastUpdatedAt = latestVerificationAt || snapshotPublishedAt;
  text("#snapshot-published-at", snapshotPublishedAt);
  text("#latest-verification-at", latestVerificationAt, "Pending first verification");
  text("#verification-last-updated", formatTimestamp(lastUpdatedAt), "Unavailable");
  text("#verification-snapshot-published", formatTimestamp(snapshotPublishedAt), "Unavailable");
  if (availabilityState === "available") {
    scheduleAvailabilityBoundary(
      manifest.availability?.available_until,
      trace.batchId,
    );
  } else {
    clearAvailabilityTimer();
  }
  renderRoute(manifest);
  text("#route-state", manifest.verification.route_completed ? "Route completed" : "Route incomplete");
  if (!verificationOnly) {
    renderEvidence(trace.evidence);
    renderTechnical(trace.technical);
    renderChecks(trace.checks);
  }
  resultSection.hidden = false;
  setVerificationReady(true);
}

async function readTrace(
  endpoint,
  { background = false, batchId = "", snapshotId = "" } = {},
) {
  if (!background) {
    clearAvailabilityTimer();
    statusLine.className = "status";
    statusLine.textContent = "Reading public-chain trace...";
    resultSection.hidden = true;
    setVerificationReady(false);
    batchSelect.disabled = true;
  }
  try {
    const response = await fetch(endpoint);
    const payload = await response.json();
    if (!response.ok) {
      if (payload.code === "SNAPSHOT_NOT_AVAILABLE" && payload.state === "upcoming") {
        scheduleAvailabilityBoundary(payload.availableFrom, payload.batchId || batchId);
      } else if (payload.code === "SNAPSHOT_NOT_AVAILABLE") {
        clearAvailabilityTimer();
      }
      throw new Error(payload.error || `Request failed: ${response.status}`);
    }
    renderTrace(payload);
    currentBatchId = payload.batchId || batchId;
    currentSnapshotId = payload.snapshotId || "";
    if (snapshotId) requestedSnapshotId = snapshotId;
    if ([...batchSelect.options].some((option) => option.value === currentBatchId)) {
      batchSelect.value = currentBatchId;
    }
    const offShelf = payload.availability?.state === "expired";
    statusLine.textContent = offShelf
      ? "This batch is off shelf. Showing its final published Snapshot."
      : payload.verified
        ? "Public Manifest and Merkle Root match the active chain record."
        : "The trace requires attention. Review its status and verification details.";
    statusLine.className = payload.verified ? "status" : "status error";
  } catch (error) {
    currentSnapshotId = "";
    resultSection.hidden = true;
    setVerificationReady(false);
    statusLine.textContent = error.message;
    statusLine.className = "status error";
  } finally {
    if (!background) batchSelect.disabled = batchSelect.options.length <= 1;
  }
}

function searchBatch(batchId, options = {}) {
  requestedSnapshotId = "";
  return readTrace(`/api/trace/${encodeURIComponent(batchId)}`, {
    ...options,
    batchId,
  });
}

function searchSnapshot(snapshotId, options = {}) {
  requestedSnapshotId = snapshotId;
  return readTrace(`/api/trace/snapshot/${encodeURIComponent(snapshotId)}`, {
    ...options,
    snapshotId,
  });
}

async function loadPublishedBatches({ background = false } = {}) {
    if (batchRefreshInFlight) return null;
    batchRefreshInFlight = true;
    const selectedBatchId = verificationOnly && requestedBatchId
      ? requestedBatchId
      : currentBatchId || batchSelect.value;
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
      option.textContent = `${batch.product} · ${batch.batchId}` +
        (batch.availabilityState === "expired" ? " · Off shelf" : "");
      batchSelect.append(option);
    }

    const selectedBatch = batches.find((batch) => batch.batchId === selectedBatchId);
    if (selectedBatch) batchSelect.value = selectedBatchId;
    const hasBatches = batches.length > 0;
    batchSelect.disabled = !hasBatches;
    if (!selectedBatch && selectedBatchId &&
        (currentBatchId === selectedBatchId || verificationOnly)) {
      currentSnapshotId = "";
      resultSection.hidden = true;
      setVerificationReady(false);
      statusLine.textContent =
        "This batch does not have an active public Snapshot yet.";
      statusLine.className = hasBatches ? "status" : "status error";
    } else if (!background || !currentBatchId) {
      statusLine.textContent = hasBatches
        ? "Choose a published product batch to view its trace."
        : "No published product batches are available yet.";
      statusLine.className = hasBatches ? "status" : "status error";
    }
    return batches;
  } catch (error) {
    batchSelect.replaceChildren();
    batchSelect.disabled = true;
    resultSection.hidden = true;
    currentSnapshotId = "";
    statusLine.textContent = error.message;
    statusLine.className = "status error";
    return null;
  } finally {
    batchRefreshInFlight = false;
  }
}

batchSelect.addEventListener("change", () => {
  if (!batchSelect.value) return;
  requestedSnapshotId = "";
  requestedBatchId = "";
  const pageUrl = new URL(window.location.href);
  pageUrl.searchParams.delete("snapshot");
  pageUrl.searchParams.delete("batch");
  window.history.replaceState(null, "", pageUrl);
  searchBatch(batchSelect.value);
});

async function refreshFromLiveEvent(eventType = "") {
  if (batchRefreshInFlight || liveRefreshInFlight) {
    liveRefreshQueued = true;
    liveRefreshQueuedType = eventType || liveRefreshQueuedType;
    return;
  }
  liveRefreshInFlight = true;
  const selectedBatchId = verificationOnly && requestedBatchId
    ? requestedBatchId
    : currentBatchId || batchSelect.value;
  try {
    const batches = await loadPublishedBatches({ background: true });
    const activeBatch = batches?.find((batch) => batch.batchId === selectedBatchId);
    if (requestedSnapshotId) {
      if (!activeBatch || activeBatch.snapshotId !== requestedSnapshotId) {
        currentSnapshotId = "";
        resultSection.hidden = true;
        setVerificationReady(false);
        statusLine.textContent =
          "This Snapshot is inactive or no longer matches the current route.";
        statusLine.className = "status error";
      } else if (resultSection.hidden || currentSnapshotId !== requestedSnapshotId) {
        await searchSnapshot(requestedSnapshotId, { background: true });
      }
      return;
    }
    if (!selectedBatchId) return;
    if (activeBatch &&
        (resultSection.hidden || activeBatch.snapshotId !== currentSnapshotId ||
          eventType === "snapshot_checked")) {
      await searchBatch(selectedBatchId, { background: true });
    } else if (!activeBatch && verificationOnly && requestedBatchId) {
      currentSnapshotId = "";
      resultSection.hidden = true;
      setVerificationReady(false);
      statusLine.textContent = "Waiting for this batch's active public Snapshot.";
      statusLine.className = "status";
    }
  } finally {
    liveRefreshInFlight = false;
    if (liveRefreshQueued) {
      const queuedType = liveRefreshQueuedType;
      liveRefreshQueued = false;
      liveRefreshQueuedType = "";
      refreshFromLiveEvent(queuedType);
    }
  }
}

async function initializeConsumerPage() {
  const query = new URLSearchParams(window.location.search);
  requestedSnapshotId = query.get("snapshot")?.trim() || "";
  requestedBatchId = query.get("batch")?.trim() || "";
  if (verificationOnly) {
    if (requestedSnapshotId) {
      await searchSnapshot(requestedSnapshotId);
    } else if (requestedBatchId) {
      await searchBatch(requestedBatchId);
    } else {
      statusLine.textContent = "This verification link does not contain a Batch ID.";
      statusLine.className = "status error";
    }
  } else {
    await loadPublishedBatches();
    if (requestedSnapshotId) await searchSnapshot(requestedSnapshotId);
  }

  liveEventSource = new EventSource("/api/events");
  liveEventSource.onmessage = (event) => {
    if (!event.data) return;
    try {
      const payload = JSON.parse(event.data);
      if (["state_sync", "route_changed", "batch_changed", "snapshot_published",
        "snapshot_checked"].includes(payload.type)) {
        refreshFromLiveEvent(payload.type);
      }
    } catch {
      // Ignore malformed broadcast data; the next event or manual selection remains usable.
    }
  };
}

initializeConsumerPage();
