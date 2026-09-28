import { computed, onBeforeUnmount, onMounted, reactive } from "vue";

const formatTimestamp = (value) => {
  if (!value) return "";
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return value;
  return new Intl.DateTimeFormat([], {
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
  }).format(date);
};

const measurement = (value) => {
  if (!value) return "Not disclosed";
  const range = value.minimum === value.maximum ? `${value.minimum}` : `${value.minimum}–${value.maximum}`;
  return `${range} ${value.unit === "percent_rh" ? "% RH" : `°${value.unit}`}`;
};

const routeStageDetails = (stage, manifest, index, route) => {
  const occurrence = route.slice(0, index + 1).filter((item) => item.stage === stage.stage).length;
  const suffix = occurrence > 1 ? ` ${occurrence}` : "";
  if (stage.stage === "supplier") return {
    label: `Supplier${suffix}`,
    primary: stage.location || manifest.origin.farm_location || "Not disclosed",
    secondary: `Harvested ${stage.harvest_date || manifest.origin.harvest_date || "date not disclosed"}`,
  };
  if (stage.stage === "logistics") return {
    label: `Logistics${suffix}`,
    primary: `${stage.pickup_location || "Not disclosed"} → ${stage.delivery_location || "Not disclosed"}`,
    secondary: `${measurement(stage.temperature)} · ${measurement(stage.humidity)}`,
  };
  if (stage.stage === "warehouse") return {
    label: `Warehouse${suffix}`,
    primary: `${stage.inbound_local_time || "Not disclosed"} → ${stage.outbound_local_time || "Not disclosed"}`,
    secondary: `${measurement(stage.temperature)} · ${measurement(stage.humidity)}`,
  };
  return {
    label: `Supermarket${suffix}`,
    primary: stage.store_location_id || manifest.retail.store_location_id || "Not disclosed",
    secondary: `Shelved ${stage.shelf_placement_date || manifest.retail.shelf_placement_date || "date not disclosed"} · Sell by ${stage.sell_by_date || manifest.retail.sell_by_date || "date not disclosed"}`,
  };
};

export function useConsumerTrace() {
  const query = new URLSearchParams(window.location.search);
  const state = reactive({
    verificationOnly: query.get("view") === "verification",
    requestedSnapshotId: query.get("snapshot")?.trim() || "",
    requestedBatchId: query.get("batch")?.trim() || "",
    batches: [], selectedBatchId: "", trace: null, selectedStageIndex: 0,
    status: "", statusKind: "", loadingBatches: false, loadingTrace: false,
  });
  const internal = reactive({
    currentSnapshotId: "", refreshInFlight: false, refreshQueuedType: "",
    assistantSessionId: "", assistantContextKey: "",
  });
  const assistantState = reactive({
    available: false, open: false, messages: [], question: "",
    status: "", error: false, busy: false,
  });
  let liveEvents = null;
  let availabilityTimer = 0;
  let messageSequence = 0;

  const stages = computed(() => {
    if (!state.trace?.manifest) return [];
    const manifest = state.trace.manifest;
    const route = Array.isArray(manifest.route) && manifest.route.length
      ? manifest.route
      : [
          { stage: "supplier", location: manifest.origin.farm_location, harvest_date: manifest.origin.harvest_date },
          { stage: "logistics", ...(manifest.transport || {}) },
          { stage: "warehouse", ...(manifest.storage || {}) },
          { stage: "supermarket", ...(manifest.retail || {}) },
        ];
    return route.map((stage, index) => routeStageDetails(stage, manifest, index, route));
  });

  const setStatus = (message, kind = "") => {
    state.status = message;
    state.statusKind = kind;
  };
  const clearAvailabilityTimer = () => {
    if (availabilityTimer) window.clearTimeout(availabilityTimer);
    availabilityTimer = 0;
  };
  const scheduleBoundary = (timestamp, batchId) => {
    clearAvailabilityTimer();
    const boundary = Date.parse(timestamp);
    if (!batchId || !Number.isFinite(boundary)) return;
    const arm = () => {
      const remaining = boundary - Date.now();
      if (remaining > 0) {
        availabilityTimer = window.setTimeout(arm, Math.min(remaining + 250, 2_147_000_000));
        return;
      }
      availabilityTimer = 0;
      void refreshFromLiveEvent("availability_boundary");
    };
    arm();
  };

  const resetAssistant = (key = "") => {
    internal.assistantContextKey = key;
    internal.assistantSessionId = "";
    assistantState.open = false;
    assistantState.messages = [];
    assistantState.question = "";
    assistantState.status = "";
    assistantState.error = false;
    assistantState.busy = false;
  };
  const configureAssistant = (trace) => {
    const available = state.verificationOnly && Boolean(trace.verified);
    if (!available) {
      assistantState.available = false;
      resetAssistant();
      return;
    }
    const key = `${trace.batchId}:${trace.snapshotId}`;
    if (internal.assistantContextKey !== key) resetAssistant(key);
    assistantState.available = true;
  };
  const pushMessage = (role, content) => {
    const date = new Date();
    assistantState.messages.push({
      id: ++messageSequence, role, content, isoTime: date.toISOString(),
      time: new Intl.DateTimeFormat([], { hour: "2-digit", minute: "2-digit" }).format(date),
    });
  };
  const submitAssistant = async () => {
    const question = assistantState.question.trim();
    if (!question || assistantState.busy || !state.trace) return;
    const contextKey = internal.assistantContextKey;
    pushMessage("user", question);
    assistantState.question = "";
    assistantState.busy = true;
    assistantState.error = false;
    assistantState.status = "Thinking…";
    try {
      const response = await fetch("/api/assistant", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          batchId: state.trace.batchId,
          snapshotId: state.trace.snapshotId,
          sessionId: internal.assistantSessionId,
          question,
        }),
      });
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
      if (contextKey !== internal.assistantContextKey) return;
      internal.assistantSessionId = payload.sessionId;
      pushMessage("assistant", payload.answer);
      assistantState.status = "";
    } catch (error) {
      if (contextKey !== internal.assistantContextKey) return;
      assistantState.question = question;
      assistantState.error = true;
      assistantState.status = error.message;
    } finally {
      if (contextKey === internal.assistantContextKey) assistantState.busy = false;
    }
  };
  const assistant = {
    get available() { return assistantState.available; },
    get open() { return assistantState.open; },
    get messages() { return assistantState.messages; },
    get question() { return assistantState.question; },
    set question(value) { assistantState.question = value; },
    get status() { return assistantState.status; },
    get error() { return assistantState.error; },
    get busy() { return assistantState.busy; },
    openAssistant: () => { assistantState.open = true; },
    closeAssistant: () => { assistantState.open = false; },
    submit: submitAssistant,
  };

  const renderTrace = (payload) => {
    const publishedAt = payload.snapshotPublishedAt || payload.technical?.publishedAt;
    payload.formattedPublishedAt = formatTimestamp(publishedAt);
    payload.formattedLastUpdated = formatTimestamp(payload.latestVerificationAt || publishedAt);
    state.trace = payload;
    document.body.classList.toggle("verification-ready", state.verificationOnly);
    state.selectedBatchId = payload.batchId || state.selectedBatchId;
    state.selectedStageIndex = 0;
    internal.currentSnapshotId = payload.snapshotId || "";
    if (payload.availability?.state === "available") {
      scheduleBoundary(payload.manifest.availability?.available_until, payload.batchId);
    } else {
      clearAvailabilityTimer();
    }
    configureAssistant(payload);
  };

  const readTrace = async (endpoint, { background = false } = {}) => {
    if (!background) {
      state.loadingTrace = true;
      state.trace = null;
      document.body.classList.remove("verification-ready");
      setStatus("Reading the public-chain trace…");
    }
    try {
      const response = await fetch(endpoint);
      const payload = await response.json();
      if (!response.ok) {
        if (payload.code === "SNAPSHOT_NOT_AVAILABLE" && payload.state === "upcoming") {
          scheduleBoundary(payload.availableFrom, payload.batchId);
        } else if (payload.code === "SNAPSHOT_NOT_AVAILABLE") {
          clearAvailabilityTimer();
        }
        throw new Error(payload.error || `Request failed: ${response.status}`);
      }
      renderTrace(payload);
      const offShelf = payload.availability?.state === "expired";
      setStatus(
        offShelf
          ? "This batch is off shelf. Showing its final published Snapshot."
          : payload.verified
            ? "Manifest, Merkle Root and active chain record agree."
            : "This trace requires attention. Review the failed checks.",
        payload.verified ? "success" : "error",
      );
    } catch (error) {
      state.trace = null;
      document.body.classList.remove("verification-ready");
      internal.currentSnapshotId = "";
      setStatus(error.message, "error");
    } finally {
      state.loadingTrace = false;
    }
  };
  const searchBatch = (batchId, options) => {
    state.requestedSnapshotId = "";
    return readTrace(`/api/trace/${encodeURIComponent(batchId)}`, options);
  };
  const searchSnapshot = (snapshotId, options) =>
    readTrace(`/api/trace/snapshot/${encodeURIComponent(snapshotId)}`, options);

  const loadPublishedBatches = async ({ background = false } = {}) => {
    if (state.loadingBatches) return null;
    state.loadingBatches = true;
    if (!background) setStatus("Loading published product batches…");
    try {
      const response = await fetch("/api/batches");
      const payload = await response.json();
      if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
      state.batches = payload.batches || [];
      if (!state.batches.some((batch) => batch.batchId === state.selectedBatchId) && !state.verificationOnly) {
        state.selectedBatchId = "";
      }
      if (!background && !state.trace) {
        setStatus(
          state.batches.length
            ? "Choose a published batch to inspect its trace."
            : "No published batches are available yet.",
          state.batches.length ? "" : "error",
        );
      }
      return state.batches;
    } catch (error) {
      state.batches = [];
      state.trace = null;
      setStatus(error.message, "error");
      return null;
    } finally {
      state.loadingBatches = false;
    }
  };

  const selectBatch = async (batchId) => {
    state.selectedBatchId = batchId;
    if (!batchId) return;
    state.requestedBatchId = "";
    const pageUrl = new URL(window.location.href);
    pageUrl.searchParams.delete("snapshot");
    pageUrl.searchParams.delete("batch");
    window.history.replaceState(null, "", pageUrl);
    await searchBatch(batchId);
  };
  const selectStage = (index) => { state.selectedStageIndex = index; };

  const refreshFromLiveEvent = async (eventType = "") => {
    if (internal.refreshInFlight) {
      internal.refreshQueuedType = eventType || internal.refreshQueuedType;
      return;
    }
    internal.refreshInFlight = true;
    try {
      const batches = await loadPublishedBatches({ background: true });
      const batchId = state.verificationOnly && state.requestedBatchId
        ? state.requestedBatchId
        : state.selectedBatchId;
      const active = batches?.find((batch) => batch.batchId === batchId);
      if (state.requestedSnapshotId) {
        if (!active || active.snapshotId !== state.requestedSnapshotId) {
          state.trace = null;
          setStatus("This Snapshot is inactive or no longer matches the current route.", "error");
        } else if (!state.trace || internal.currentSnapshotId !== state.requestedSnapshotId) {
          await searchSnapshot(state.requestedSnapshotId, { background: true });
        }
      } else if (batchId && active && (
        !state.trace || active.snapshotId !== internal.currentSnapshotId || eventType === "snapshot_checked"
      )) {
        await searchBatch(batchId, { background: true });
      } else if (batchId && !active && state.verificationOnly) {
        state.trace = null;
        setStatus("Waiting for this batch's active public Snapshot.");
      }
    } finally {
      internal.refreshInFlight = false;
      if (internal.refreshQueuedType) {
        const queued = internal.refreshQueuedType;
        internal.refreshQueuedType = "";
        void refreshFromLiveEvent(queued);
      }
    }
  };

  const escapeAssistant = (event) => {
    if (event.key === "Escape") assistantState.open = false;
  };

  onMounted(async () => {
    document.body.classList.toggle("verification-only", state.verificationOnly);
    if (state.verificationOnly) document.title = "Verification Result";
    if (state.verificationOnly && state.requestedSnapshotId) {
      await searchSnapshot(state.requestedSnapshotId);
    } else if (state.verificationOnly && state.requestedBatchId) {
      state.selectedBatchId = state.requestedBatchId;
      await searchBatch(state.requestedBatchId);
    } else if (state.verificationOnly) {
      setStatus("This verification link does not contain a Batch ID.", "error");
    } else {
      await loadPublishedBatches();
      if (state.requestedSnapshotId) await searchSnapshot(state.requestedSnapshotId);
    }
    liveEvents = new EventSource("/api/events");
    liveEvents.onmessage = (event) => {
      try {
        const payload = JSON.parse(event.data);
        if (["state_sync", "route_changed", "batch_changed", "snapshot_published", "snapshot_checked"].includes(payload.type)) {
          void refreshFromLiveEvent(payload.type);
        }
      } catch {
        // A later event or a manual batch selection can recover the view.
      }
    };
    window.addEventListener("keydown", escapeAssistant);
  });

  onBeforeUnmount(() => {
    liveEvents?.close();
    clearAvailabilityTimer();
    window.removeEventListener("keydown", escapeAssistant);
    document.body.classList.remove("verification-only");
    document.body.classList.remove("verification-ready");
    document.body.classList.remove("assistant-open");
  });

  return { state, stages, assistant, selectBatch, selectStage };
}
