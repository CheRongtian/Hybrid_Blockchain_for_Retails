import { computed, onBeforeUnmount, onMounted, reactive } from "vue";

const API = "http://127.0.0.1:8081/api";
const SESSION_KEY = "supply-chain-user-session";
const CONTROL_TIMEOUT = 30_000;
const RECORD_TIMEOUT = 60_000;
const UPLOAD_TIMEOUT = 120_000;

const roleLabels = {
  supplier: "Supplier", logistics: "Logistics",
  warehouse: "Warehouse", supermarket: "Supermarket",
};

const roleConfig = {
  supplier: {
    fields: ["harvestDate", "farmLocation", "certificateId"],
    categories: {
      pesticideFertilizerRecords: "Pesticide / Fertilizer Records",
      soilWeatherData: "Soil / Weather Data",
      harvestPhotos: "Harvest Photos",
      inspectionReports: "Inspection Reports",
    },
  },
  logistics: {
    fields: ["shipmentId", "pickupLocation", "deliveryLocation", "departureTime", "arrivalTime", "temperature", "temperatureUnit", "humidity", "vehicleContainerId"],
    categories: {
      gpsTrackLogs: "GPS Track Logs", temperatureLogs: "Temperature Logs",
      transportDocuments: "Transport Documents", sealVerificationImages: "Seal Verification Images",
    },
  },
  warehouse: {
    fields: ["storageLotId", "inboundTime", "outboundTime", "temperature", "temperatureUnit", "humidity", "storageZoneRackId"],
    categories: {
      inspectionReports: "Inspection Reports", fullTemperatureLogs: "Full Temperature Logs",
      energyUsageLogs: "Energy Usage Logs",
    },
  },
  supermarket: {
    fields: ["shelfPlacementDate", "expirationSellByDate", "storeLocationId"],
    categories: {
      productPhotosLabels: "Product Photos / Labels",
      receiptTransactionRecords: "Receipt / Transaction Records",
      recallNotices: "Recall Notices", consumerFeedbackData: "Consumer Feedback Data",
    },
  },
};

const blankEvent = () => ({
  harvestDate: "", farmLocation: "", certificateId: "CERT-0001",
  shipmentId: "", pickupLocation: "", deliveryLocation: "",
  departureTime: "", arrivalTime: "", temperature: "", temperatureUnit: "C",
  humidity: "", vehicleContainerId: "", storageLotId: "",
  inboundTime: "", outboundTime: "", storageZoneRackId: "",
  shelfPlacementDate: "", expirationSellByDate: "", storeLocationId: "STORE-0001",
});

const readJson = async (response) => {
  const text = await response.text();
  try { return text ? JSON.parse(text) : {}; }
  catch { throw new Error("The private-chain server returned malformed JSON. Rebuild and restart it."); }
};

const fetchWithTimeout = async (url, options = {}, timeout = CONTROL_TIMEOUT) => {
  const controller = new AbortController();
  const timer = window.setTimeout(() => controller.abort(), timeout);
  try { return await fetch(url, { ...options, signal: controller.signal }); }
  catch (error) {
    if (error.name === "AbortError") throw new Error("The control server did not respond in time.");
    throw error;
  } finally { window.clearTimeout(timer); }
};

const byteLength = (value) => new TextEncoder().encode(value).length;
const signatureField = (name, value) => `${name}:${byteLength(value)}:${value}\n`;
const bytesToBase64 = (bytes) => {
  let binary = "";
  bytes.forEach((byte) => { binary += String.fromCharCode(byte); });
  return btoa(binary);
};

export function useParticipantRecord() {
  const state = reactive({
    session: null,
    auth: { username: "", password: "", remember: false, busy: false, status: "", kind: "" },
    product: "", batches: [], selectedBatchId: "", batchStatus: "", batchStatusKind: "",
    event: blankEvent(), attachmentCategory: "", selectedFiles: [], uploadedReferences: [],
    policy: null, confirmationMethod: "typed_name", confirmationName: "", confirmationError: "",
    confirmed: false, submitting: false, status: "", statusKind: "", signatureStatus: "",
    result: null, resultOpen: false, completed: false,
  });
  let liveEvents = null;
  let batchRequest = 0;
  let policyRequest = 0;
  let refreshInFlight = false;
  let refreshQueued = false;

  const role = computed(() => state.session?.user?.role || "");
  const config = computed(() => roleConfig[role.value] || { fields: [], categories: {} });
  const selectedBatch = computed(() => state.batches.find((batch) => batch.batchId === state.selectedBatchId) || null);
  const stageLabel = computed(() => selectedBatch.value?.nextNodeLabel || roleLabels[role.value] || role.value);
  const identityLabel = computed(() => state.session
    ? `${state.session.user.username} · ${roleLabels[role.value]} · ${state.session.user.organizationId}`
    : "");
  const attachmentCategories = computed(() => Object.entries(config.value.categories).map(([value, label]) => ({ value, label })));
  const availableMethods = computed(() => {
    if (!state.policy) return [];
    return [
      { key: "typedName", value: "typed_name", label: "Typed name", supported: true },
      { key: "handwritten", value: "handwritten", label: "Handwritten", supported: false },
      { key: "face", value: "face", label: "Face", supported: false },
    ].filter((method) => state.policy[method.key]);
  });
  const canSubmit = computed(() => {
    if (state.submitting || state.completed || !state.policy) return false;
    if (role.value !== "supplier" && !selectedBatch.value) return false;
    if (role.value === "logistics" && !selectedBatch.value?.nextDestinationLabel) return false;
    return availableMethods.value.some((method) => method.supported);
  });

  const saveSession = (result, remember) => {
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
    (remember ? localStorage : sessionStorage).setItem(SESSION_KEY, JSON.stringify(result));
  };
  const storedSession = () => localStorage.getItem(SESSION_KEY) || sessionStorage.getItem(SESSION_KEY);
  const setAuthStatus = (message, kind = "") => { state.auth.status = message; state.auth.kind = kind; };
  const setStatus = (message, kind = "") => { state.status = message; state.statusKind = kind; };

  const stopLiveUpdates = () => { liveEvents?.close(); liveEvents = null; refreshInFlight = false; refreshQueued = false; };
  const clearSession = () => {
    stopLiveUpdates();
    batchRequest += 1;
    policyRequest += 1;
    state.session = null;
    state.batches = [];
    state.selectedBatchId = "";
    state.policy = null;
    state.resultOpen = false;
    state.result = null;
    state.completed = false;
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
  };

  const syncAssignedFields = () => {
    const batch = selectedBatch.value;
    if (role.value === "logistics") {
      state.event.shipmentId = batch?.nextShipmentId || "";
      state.event.deliveryLocation = batch?.nextDestinationLabel || "";
      state.event.vehicleContainerId = batch?.nextVehicleContainerId || "";
    }
    if (role.value === "warehouse") {
      state.event.storageLotId = batch?.nextStorageLotId || "";
      state.event.storageZoneRackId = batch?.nextStorageZoneRackId || "";
    }
  };

  const validateTypedName = () => {
    if (state.confirmationMethod !== "typed_name") { state.confirmationError = ""; return true; }
    const expected = state.session?.user?.displayName || state.session?.user?.username || "";
    const actual = state.confirmationName.trim();
    if (!actual) state.confirmationError = "Type your registered name to continue.";
    else if (actual !== expected) state.confirmationError = "The typed name does not match your registered name.";
    else state.confirmationError = "";
    return !state.confirmationError;
  };

  const loadPolicy = async (batch = selectedBatch.value) => {
    if (!state.session) return;
    if (role.value !== "supplier" && !batch) { state.policy = null; return; }
    const requestId = ++policyRequest;
    const query = new URLSearchParams();
    if (batch?.batchId) query.set("batchId", batch.batchId);
    else {
      query.set("routeId", "route-default");
      query.set("role", role.value);
      query.set("username", state.session.user.username);
    }
    try {
      const response = await fetch(`${API}/confirmation-policy?${query}`, { headers: { Authorization: `Bearer ${state.session.token}` } });
      const policy = await readJson(response);
      if (requestId !== policyRequest) return;
      if (response.status === 401) { clearSession(); throw new Error("Your session expired. Sign in again."); }
      if (!response.ok) throw new Error(policy.error || `Request failed: ${response.status}`);
      if (role.value !== "supplier" && batch && (policy.nodeId !== batch.nextNodeId || policy.username !== state.session.user.username)) {
        throw new Error("The confirmation policy does not match this assigned route node.");
      }
      state.policy = policy;
      const first = [
        { key: "typedName", value: "typed_name", supported: true },
        { key: "handwritten", value: "handwritten", supported: false },
        { key: "face", value: "face", supported: false },
      ].find((method) => policy[method.key] && method.supported);
      state.confirmationMethod = first?.value || "";
      validateTypedName();
    } catch (error) {
      if (requestId !== policyRequest) return;
      state.policy = null;
      state.confirmationError = error.message;
    }
  };

  const loadBatches = async ({ background = false } = {}) => {
    if (!state.session || role.value === "supplier") return;
    const currentRole = role.value;
    const requestId = ++batchRequest;
    if (!background) { state.batchStatus = "Loading assigned batches…"; state.batchStatusKind = "pending"; }
    try {
      const response = await fetch(`${API}/batches`, { headers: { Authorization: `Bearer ${state.session.token}` } });
      const result = await readJson(response);
      if (response.status === 401) { clearSession(); throw new Error("Your session expired. Sign in again."); }
      if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
      if (!Array.isArray(result)) throw new Error("The route batch response is malformed.");
      if (requestId !== batchRequest || role.value !== currentRole) return;
      state.batches = result.filter((batch) => batch.nextNodeId && batch.nextStage === currentRole && batch.nextNodeUsername === state.session.user.username && batch.status !== "completed");
      if (!state.batches.some((batch) => batch.batchId === state.selectedBatchId)) state.selectedBatchId = state.batches[0]?.batchId || "";
      syncAssignedFields();
      await loadPolicy(selectedBatch.value);
      state.batchStatus = selectedBatch.value
        ? `Assigned to ${selectedBatch.value.nextNodeLabel || roleLabels[currentRole]}.`
        : "No batch is waiting for this account.";
      state.batchStatusKind = selectedBatch.value ? "success" : "pending";
    } catch (error) {
      if (requestId !== batchRequest) return;
      state.batches = [];
      state.selectedBatchId = "";
      state.policy = null;
      state.batchStatus = error.message;
      state.batchStatusKind = "error";
    }
  };

  const configureSession = async (result) => {
    state.session = result;
    state.completed = false;
    state.result = null;
    state.resultOpen = false;
    const categories = Object.keys(roleConfig[result.user.role]?.categories || {});
    state.attachmentCategory = categories[0] || "";
    if (result.user.role === "supplier") await loadPolicy(null);
    else await loadBatches();
    startLiveUpdates();
  };

  const login = async () => {
    if (state.auth.busy) return;
    state.auth.busy = true;
    setAuthStatus("Authenticating participant…", "pending");
    try {
      const response = await fetch(`${API}/auth/login`, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: new URLSearchParams({ username: state.auth.username, password: state.auth.password, remember: state.auth.remember ? "true" : "false" }).toString(),
      });
      const result = await readJson(response);
      if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);
      if (!roleConfig[result.user.role]) throw new Error("This account cannot submit route events.");
      saveSession(result, state.auth.remember);
      state.auth.password = "";
      setAuthStatus("Identity verified.", "success");
      await configureSession(result);
    } catch (error) { setAuthStatus(error.message, "error"); }
    finally { state.auth.busy = false; }
  };

  const restoreSession = async () => {
    const saved = storedSession();
    if (!saved) return;
    try {
      const stored = JSON.parse(saved);
      const response = await fetch(`${API}/auth/me`, { headers: { Authorization: `Bearer ${stored.token}` } });
      const user = await readJson(response);
      if (!response.ok || !roleConfig[user.role]) throw new Error("Session expired");
      await configureSession({ token: stored.token, user });
    } catch { clearSession(); }
  };

  const logout = async () => {
    const token = state.session?.token;
    if (token) {
      try { await fetch(`${API}/auth/logout`, { method: "POST", headers: { Authorization: `Bearer ${token}` } }); }
      catch { /* local logout still clears the browser session */ }
    }
    clearSession();
  };

  const selectBatch = async (batchId) => {
    state.selectedBatchId = batchId;
    state.completed = false;
    syncAssignedFields();
    await loadPolicy(selectedBatch.value);
  };
  const selectFiles = (fileList) => { state.selectedFiles = [...fileList]; };
  const formatFileSize = (bytes) => bytes < 1024 ? `${bytes} B` : bytes < 1024 * 1024 ? `${(bytes / 1024).toFixed(1)} KB` : `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
  const attachmentItems = computed(() => [
    ...state.uploadedReferences.map((item) => ({ key: item.cid, title: item.filename, meta: `${item.category} · ${item.cid}` })),
    ...state.selectedFiles.map((file, index) => ({ key: `${file.name}-${index}`, title: file.name, meta: `${state.attachmentCategory} · ${formatFileSize(file.size)}` })),
  ]);

  const uploadFiles = async () => {
    for (const file of state.selectedFiles) {
      const upload = new FormData();
      upload.append("category", state.attachmentCategory);
      upload.append("file", file);
      const response = await fetchWithTimeout(`${API}/ipfs/files`, { method: "POST", headers: { Authorization: `Bearer ${state.session.token}` }, body: upload }, UPLOAD_TIMEOUT);
      const result = await readJson(response);
      if (response.status === 401) { clearSession(); throw new Error("Your session expired. Sign in again."); }
      if (!response.ok) throw new Error(result.error || "IPFS upload failed.");
      state.uploadedReferences.push(result);
    }
    state.selectedFiles = [];
  };

  const encodeReferences = (references = state.uploadedReferences) => references.map((reference) =>
    [reference.category, reference.cid, reference.filename, reference.contentType, reference.size]
      .map((value) => encodeURIComponent(String(value))).join("|")).join(",");
  const compareBytes = (left, right) => {
    const a = new TextEncoder().encode(left);
    const b = new TextEncoder().encode(right);
    for (let index = 0; index < Math.min(a.length, b.length); index += 1) if (a[index] !== b[index]) return a[index] - b[index];
    return a.length - b.length;
  };
  const canonicalReferences = () => encodeReferences([...state.uploadedReferences].sort((left, right) => {
    const a = [left.category, left.cid, left.filename, left.contentType, String(left.size)];
    const b = [right.category, right.cid, right.filename, right.contentType, String(right.size)];
    for (let index = 0; index < a.length; index += 1) { const result = compareBytes(a[index], b[index]); if (result) return result; }
    return 0;
  }));

  const signaturePayload = (challenge, method, name) => {
    const currentRole = role.value;
    const identity = currentRole === "supplier"
      ? { batchId: "SERVER_ALLOCATED", product: state.product.trim() }
      : { batchId: state.selectedBatchId, product: selectedBatch.value?.product || "" };
    const values = [
      ["challenge", challenge], ["uid", state.session.user.uid], ["username", state.session.user.username],
      ["role", currentRole], ["confirmationMethod", method], ["confirmationName", name],
      ["batchId", identity.batchId], ["product", identity.product], ["confirmed", "true"],
      ...config.value.fields.map((field) => [`event.${field}`, String(state.event[field] || "").trim()]),
      ["ipfsReferences", canonicalReferences()],
    ];
    return values.map(([field, value]) => signatureField(field, value)).join("");
  };
  const signingKey = async () => {
    if (!window.crypto?.subtle) throw new Error("This browser does not support Web Crypto digital signatures.");
    const key = `supply-chain-signing-key:${state.session.user.uid}`;
    const stored = localStorage.getItem(key);
    if (stored) return crypto.subtle.importKey("jwk", JSON.parse(stored), { name: "ECDSA", namedCurve: "P-256" }, true, ["sign"]);
    const pair = await crypto.subtle.generateKey({ name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]);
    localStorage.setItem(key, JSON.stringify(await crypto.subtle.exportKey("jwk", pair.privateKey)));
    return pair.privateKey;
  };
  const publicKey = async (privateKey) => {
    const privateJwk = await crypto.subtle.exportKey("jwk", privateKey);
    const publicJwk = { kty: privateJwk.kty, crv: privateJwk.crv, x: privateJwk.x, y: privateJwk.y, key_ops: ["verify"], ext: true };
    const imported = await crypto.subtle.importKey("jwk", publicJwk, { name: "ECDSA", namedCurve: "P-256" }, true, ["verify"]);
    return bytesToBase64(new Uint8Array(await crypto.subtle.exportKey("spki", imported)));
  };
  const digitalConfirmation = async () => {
    if (!state.policy) throw new Error("Confirmation policy is still loading.");
    if (state.confirmationMethod !== "typed_name") throw new Error("This confirmation method is unavailable in the current demo.");
    if (!validateTypedName()) throw new Error(state.confirmationError);
    const response = await fetchWithTimeout(`${API}/confirmation/challenge`, { headers: { Authorization: `Bearer ${state.session.token}` } });
    const challenge = await readJson(response);
    if (response.status === 401) { clearSession(); throw new Error("Your session expired. Sign in again."); }
    if (!response.ok) throw new Error(challenge.error || "Unable to create a confirmation challenge.");
    const name = state.confirmationName.trim();
    const payload = signaturePayload(challenge.challenge, state.confirmationMethod, name);
    const privateKey = await signingKey();
    const signature = await crypto.subtle.sign({ name: "ECDSA", hash: "SHA-256" }, privateKey, new TextEncoder().encode(payload));
    return { method: state.confirmationMethod, name, challenge: challenge.challenge, payload, signature: bytesToBase64(new Uint8Array(signature)), publicKey: await publicKey(privateKey) };
  };

  const recordPayload = (confirmation) => {
    const payload = new URLSearchParams();
    if (role.value !== "supplier") {
      payload.set("batchId", state.selectedBatchId);
      payload.set("routeId", selectedBatch.value?.routeId || "");
      payload.set("routeNodeId", selectedBatch.value?.nextNodeId || "");
    } else payload.set("product", state.product);
    config.value.fields.forEach((field) => payload.set(field, state.event[field] || ""));
    payload.set("ipfsRefs", encodeReferences());
    payload.set("confirmed", state.confirmed ? "true" : "false");
    payload.set("confirmationMethod", confirmation.method);
    payload.set("confirmationName", confirmation.name);
    payload.set("confirmationChallenge", confirmation.challenge);
    payload.set("signatureAlgorithm", "ECDSA-P256-SHA256");
    payload.set("signature", confirmation.signature);
    payload.set("signaturePublicKey", confirmation.publicKey);
    payload.set("signaturePayload", confirmation.payload);
    return payload;
  };

  const submit = async (form) => {
    if (!state.session || state.submitting || !form?.reportValidity()) return;
    state.submitting = true;
    setStatus("Uploading evidence and creating the Merkle block…", "pending");
    try {
      await uploadFiles();
      state.signatureStatus = "Creating the ECDSA confirmation…";
      const confirmation = await digitalConfirmation();
      state.signatureStatus = "Signature ready. Waiting for server verification…";
      const response = await fetchWithTimeout(`${API}/records`, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8", Authorization: `Bearer ${state.session.token}` },
        body: recordPayload(confirmation).toString(),
      }, RECORD_TIMEOUT);
      const result = await readJson(response);
      if (response.status === 401) { clearSession(); throw new Error("Your session expired. Sign in again."); }
      if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
      state.completed = true;
      state.result = { success: Boolean(result.verified), title: result.verified ? "Block verified" : "Verification failed", batchId: result.batchId, blockId: result.blockID, nextStage: result.nextNodeLabel || roleLabels[result.nextStage] || "Route complete", ipfsCount: result.ipfsCount };
      state.resultOpen = true;
      setStatus("", "");
      state.signatureStatus = "";
    } catch (error) {
      state.result = { success: false, title: "Submission failed", batchId: state.selectedBatchId || "Not created", blockId: "No block created", nextStage: error.message, ipfsCount: state.uploadedReferences.length };
      state.resultOpen = true;
      setStatus("", "");
      state.signatureStatus = "";
    } finally { state.submitting = false; }
  };

  const clearForm = () => {
    state.event = blankEvent();
    state.product = "";
    state.selectedFiles = [];
    state.uploadedReferences = [];
    state.confirmationName = "";
    state.confirmed = false;
    state.completed = false;
    state.result = null;
    state.resultOpen = false;
    setStatus("", "");
    syncAssignedFields();
  };

  const refreshAssignedStage = async () => {
    if (!state.session || state.completed) return;
    if (refreshInFlight || state.submitting) { refreshQueued = true; return; }
    refreshInFlight = true;
    try { if (role.value === "supplier") await loadPolicy(null); else await loadBatches({ background: true }); }
    finally { refreshInFlight = false; if (refreshQueued && !state.completed) { refreshQueued = false; void refreshAssignedStage(); } }
  };
  const startLiveUpdates = () => {
    stopLiveUpdates();
    liveEvents = new EventSource(`${API}/events`);
    liveEvents.onmessage = (event) => {
      try { const payload = JSON.parse(event.data); if (["state_sync", "route_changed", "batch_changed"].includes(payload.type)) void refreshAssignedStage(); }
      catch { /* a later state event can recover */ }
    };
  };

  onMounted(restoreSession);
  onBeforeUnmount(stopLiveUpdates);

  return {
    state, role, config, selectedBatch, stageLabel, identityLabel,
    attachmentCategories, availableMethods, attachmentItems, canSubmit,
    roleLabels, login, logout, selectBatch, selectFiles, validateTypedName,
    submit, clearForm, closeResult: () => { state.resultOpen = false; },
  };
}
