const loginCard = document.querySelector("#login-card");
const loginForm = document.querySelector("#login-form");
const loginStatus = document.querySelector("#login-status");
const rememberLogin = document.querySelector("#remember-login");
const recordCard = document.querySelector("#record-card");
const identityStatus = document.querySelector("#identity-status");
const logoutButton = document.querySelector("#logout-button");
const form = document.querySelector("#record-form");
const currentStage = document.querySelector("#current-stage");
const statusLine = document.querySelector("#request-status");
const submissionDialog = document.querySelector("#submission-dialog");
const verificationTitle = document.querySelector("#verification-title");
const productInput = document.querySelector("#product-input");
const supplierMasterFields = document.querySelector("#supplier-master-fields");
const existingBatchFields = document.querySelector("#existing-batch-fields");
const batchSelect = document.querySelector("#batch-select");
const batchProduct = document.querySelector("#batch-product");
const batchHarvestDate = document.querySelector("#batch-harvest-date");
const batchFarmLocation = document.querySelector("#batch-farm-location");
const batchStatus = document.querySelector("#batch-status");
const deliveryLocationInput = document.querySelector("[name=deliveryLocation]");
const shipmentIdInput = document.querySelector("[name=shipmentId]");
const vehicleContainerIdInput = document.querySelector("[name=vehicleContainerId]");
const storageLotIdInput = document.querySelector("[name=storageLotId]");
const storageZoneRackIdInput = document.querySelector("[name=storageZoneRackId]");
const attachmentCategory = document.querySelector("#attachment-category");
const ipfsFiles = document.querySelector("#ipfs-files");
const attachmentList = document.querySelector("#attachment-list");
const submitRecord = document.querySelector("#submit-record");
const clearFormButton = document.querySelector("#clear-form-button");
const storeLocationNumber = document.querySelector("#store-location-number");
const storeLocationId = document.querySelector("#store-location-id");
const confirmationPanel = document.querySelector("#confirmation-panel");
const confirmationPolicySummary = document.querySelector("#confirmation-policy-summary");
const confirmationMethods = document.querySelector("#confirmation-methods");
const typedNameField = document.querySelector("#typed-name-field");
const typedConfirmationName = document.querySelector("#typed-confirmation-name");
const confirmationError = document.querySelector("#confirmation-error");
const signatureStatus = document.querySelector("#signature-status");
const controlApiBase = "http://127.0.0.1:8081/api";
const sessionKey = "supply-chain-user-session";
const CONTROL_REQUEST_TIMEOUT_MS = 30000;
const RECORD_REQUEST_TIMEOUT_MS = 60000;
const IPFS_UPLOAD_TIMEOUT_MS = 120000;

async function fetchWithTimeout(url, options = {}, timeoutMs = CONTROL_REQUEST_TIMEOUT_MS) {
    const controller = new AbortController();
    const timeoutId = window.setTimeout(() => controller.abort(), timeoutMs);
    try {
        return await fetch(url, { ...options, signal: controller.signal });
    } catch (error) {
        if (error.name === "AbortError") {
            throw new Error("The server did not respond in time. Check the control server.");
        }
        throw error;
    } finally {
        window.clearTimeout(timeoutId);
    }
}

const roleLabels = {
    supplier: "Supplier",
    logistics: "Logistics",
    warehouse: "Warehouse",
    supermarket: "Supermarket"
};

const roleConfig = {
    supplier: {
        fields: ["harvestDate", "farmLocation", "certificateId"],
        categories: {
            pesticideFertilizerRecords: "Pesticide / Fertilizer Records",
            soilWeatherData: "Soil / Weather Data",
            harvestPhotos: "Harvest Photos",
            inspectionReports: "Inspection Reports"
        }
    },
    logistics: {
        fields: [
            "shipmentId", "pickupLocation", "deliveryLocation",
            "departureTime", "arrivalTime", "temperature", "temperatureUnit",
            "humidity",
            "vehicleContainerId"
        ],
        categories: {
            gpsTrackLogs: "GPS Track Logs",
            temperatureLogs: "Temperature Logs",
            transportDocuments: "Transport Documents",
            sealVerificationImages: "Seal Verification Images"
        }
    },
    warehouse: {
        fields: [
            "storageLotId", "inboundTime", "outboundTime",
            "temperature", "temperatureUnit", "humidity", "storageZoneRackId"
        ],
        categories: {
            inspectionReports: "Inspection Reports",
            fullTemperatureLogs: "Full Temperature Logs",
            energyUsageLogs: "Energy Usage Logs"
        }
    },
    supermarket: {
        fields: ["shelfPlacementDate", "expirationSellByDate", "storeLocationId"],
        categories: {
            productPhotosLabels: "Product Photos / Labels",
            receiptTransactionRecords: "Receipt / Transaction Records",
            recallNotices: "Recall Notices",
            consumerFeedbackData: "Consumer Feedback Data"
        }
    }
};

let session = null;
let availableBatches = [];
let uploadedReferences = [];
let confirmationPolicy = null;
let confirmationPolicyRequestId = 0;
let batchLoadRequestId = 0;
let liveEventSource = null;
let recordSubmissionInFlight = false;
let submissionCompleted = false;
let liveRefreshInFlight = false;
let liveRefreshQueued = false;

function currentRole() {
    return session?.user?.role || "";
}

function setCurrentStage(role, batch = null) {
    currentStage.value = batch?.nextNodeLabel || roleLabels[role] || role;
}

function syncStoreLocationId() {
    if (!storeLocationNumber || !storeLocationId) return;
    const number = storeLocationNumber.value.trim();
    storeLocationId.value = /^[0-9]{4}$/.test(number) ? "STORE-" + number : "";
}

function resetSubmissionState() {
    statusLine.textContent = "";
    statusLine.className = "request-status";
    signatureStatus.textContent = "";
    signatureStatus.className = "signature-status";
    if (submissionDialog?.open) submissionDialog.close();
}

function clearRecordForm() {
    if (!session) return;
    if (submissionDialog?.open) submissionDialog.close();
    form.reset();
    clearAttachmentState();
    submissionCompleted = false;
    liveRefreshQueued = false;
    confirmationPolicy = null;
    confirmationMethods.replaceChildren();
    confirmationPanel.hidden = true;
    confirmationError.textContent = "";
    configureRole();
}

function showSession(result) {
    resetSubmissionState();
    session = result;
    loginCard.hidden = true;
    recordCard.hidden = false;
    identityStatus.textContent =
        "Logged in: " + result.user.username + " · " +
        result.user.role + " · " + result.user.organizationId;
    startLiveUpdates();
    configureRole();
}

function saveSession(result, remember) {
    const serialized = JSON.stringify(result);
    sessionStorage.removeItem(sessionKey);
    localStorage.removeItem(sessionKey);
    (remember ? localStorage : sessionStorage).setItem(sessionKey, serialized);
}

function readSavedSession() {
    const remembered = localStorage.getItem(sessionKey);
    if (remembered) return remembered;
    return sessionStorage.getItem(sessionKey);
}

async function readJsonResponse(response) {
    const text = await response.text();
    try {
        return text ? JSON.parse(text) : {};
    } catch {
        throw new Error(
            "The private-chain server returned malformed JSON. Rebuild the server and refresh the page."
        );
    }
}

function clearAttachmentState() {
    uploadedReferences = [];
    ipfsFiles.value = "";
    renderAttachmentList();
}

function clearSession() {
    stopLiveUpdates();
    batchLoadRequestId += 1;
    confirmationPolicyRequestId += 1;
    session = null;
    availableBatches = [];
    sessionStorage.removeItem(sessionKey);
    localStorage.removeItem(sessionKey);
    loginCard.hidden = false;
    recordCard.hidden = true;
    resetSubmissionState();
    confirmationPolicy = null;
    confirmationMethods.replaceChildren();
    confirmationPanel.hidden = true;
    confirmationError.textContent = "";
    form.reset();
    clearAttachmentState();
}

function stopLiveUpdates() {
    if (liveEventSource) {
        liveEventSource.close();
        liveEventSource = null;
    }
    recordSubmissionInFlight = false;
    submissionCompleted = false;
    liveRefreshInFlight = false;
    liveRefreshQueued = false;
}

async function refreshAssignedStageFromLiveEvent() {
    if (!session || submissionCompleted) return;
    if (recordSubmissionInFlight || liveRefreshInFlight) {
        liveRefreshQueued = true;
        return;
    }

    liveRefreshInFlight = true;
    liveRefreshQueued = false;
    try {
        if (currentRole() === "supplier") {
            await loadConfirmationPolicyForBatch(null);
        } else {
            await loadBatches(currentRole(), { preserveForm: true, background: true });
        }
    } finally {
        liveRefreshInFlight = false;
        if (liveRefreshQueued && session && !submissionCompleted) {
            liveRefreshQueued = false;
            window.queueMicrotask(refreshAssignedStageFromLiveEvent);
        }
    }
}

function startLiveUpdates() {
    stopLiveUpdates();
    liveEventSource = new EventSource(controlApiBase + "/events");
    liveEventSource.onmessage = (event) => {
        if (!event.data || submissionCompleted) return;
        try {
            const payload = JSON.parse(event.data);
            if (!["state_sync", "route_changed", "batch_changed"].includes(payload.type)) return;
            refreshAssignedStageFromLiveEvent();
        } catch {
            // Ignore malformed broadcast data; the normal form remains usable.
        }
    };
}

function setConfirmationError(message) {
    confirmationError.textContent = message;
    confirmationError.className = message ? "request-status error" : "request-status";
}

function selectedConfirmationMethod() {
    return document.querySelector("input[name=confirmation-method]:checked")?.value || "";
}

function updateTypedNameState() {
    const typed = selectedConfirmationMethod() === "typed_name";
    typedNameField.hidden = !typed;
    if (!typed) {
        setConfirmationError("");
        return true;
    }

    const expected = session?.user?.displayName || session?.user?.username || "";
    const actual = typedConfirmationName.value.trim();
    if (!actual) {
        setConfirmationError("Type your registered name to continue.");
        return false;
    }
    if (actual !== expected) {
        setConfirmationError("The typed name does not match your registered name.");
        return false;
    }
    setConfirmationError("");
    return true;
}

function renderConfirmationPolicy(policy) {
    confirmationPolicy = policy;
    confirmationMethods.replaceChildren();
    confirmationPanel.hidden = false;
    const nodeLabel = policy.nodeLabel || roleLabels[policy.role] || policy.role;
    const accountLabel = policy.username ? ` (${policy.username})` : "";
    confirmationPolicySummary.textContent =
        `Select one method enabled for ${nodeLabel}${accountLabel}.`;

    const methods = [
        { property: "typedName", value: "typed_name", label: "Typed name", supported: true },
        { property: "handwritten", value: "handwritten", label: "Handwritten", supported: false },
        { property: "face", value: "face", label: "Face", supported: false }
    ];
    let selected = false;
    for (const method of methods) {
        if (!policy[method.property]) continue;
        const wrapper = document.createElement("label");
        wrapper.className = "confirmation-method";
        const input = document.createElement("input");
        input.type = "radio";
        input.name = "confirmation-method";
        input.value = method.value;
        input.disabled = !method.supported;
        if (method.supported && !selected) {
            input.checked = true;
            selected = true;
        }
        const text = document.createElement("span");
        text.textContent = method.label;
        wrapper.append(input, text);
        confirmationMethods.append(wrapper);
        input.addEventListener("change", updateTypedNameState);
    }
    if (!selected) {
        setConfirmationError("No usable confirmation method is configured for this route stage.");
        return;
    }
    updateTypedNameState();
}

function clearConfirmationPolicy() {
    confirmationPolicy = null;
    confirmationMethods.replaceChildren();
    confirmationPanel.hidden = true;
    confirmationPolicySummary.textContent = "";
    setConfirmationError("");
}

async function loadConfirmationPolicyForBatch(batch = null) {
    if (!session) return;
    const role = currentRole();
    if (role !== "supplier" && !batch) {
        clearConfirmationPolicy();
        return;
    }

    const requestId = ++confirmationPolicyRequestId;
    const query = new URLSearchParams();
    if (batch?.batchId) {
        query.set("batchId", batch.batchId);
    } else {
        query.set("routeId", "route-default");
        query.set("role", role);
        query.set("username", session.user.username);
    }
    try {
        const response = await fetch(
            controlApiBase + "/confirmation-policy?" + query.toString(),
            { headers: { Authorization: "Bearer " + session.token } }
        );
        const policy = await readJsonResponse(response);
        if (requestId !== confirmationPolicyRequestId) return;
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) throw new Error(policy.error || ("Request failed: " + response.status));
        if (role !== "supplier" && batch &&
            (policy.nodeId !== batch.nextNodeId ||
             policy.username !== session.user.username)) {
            throw new Error("The confirmation policy does not match the assigned route node.");
        }
        renderConfirmationPolicy(policy);
    } catch (error) {
        if (requestId !== confirmationPolicyRequestId) return;
        confirmationPanel.hidden = false;
        confirmationPolicySummary.textContent = error.message;
        setConfirmationError(error.message);
    }
}

async function loadConfirmationPolicy() {
    return loadConfirmationPolicyForBatch(
        currentRole() === "supplier" ? null : selectedBatch()
    );
}

function formatFileSize(bytes) {
    if (bytes < 1024) return bytes + " B";
    if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
    return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

function selectedAttachmentCategoryLabel() {
    return attachmentCategory.options[attachmentCategory.selectedIndex]?.textContent ||
        attachmentCategory.value;
}

async function logout() {
    const token = session?.token;
    if (token) {
        try {
            await fetch(controlApiBase + "/auth/logout", {
                method: "POST",
                headers: { Authorization: "Bearer " + token }
            });
        } catch {
            // Local logout still clears the stored session if the server is unavailable.
        }
    }
    clearSession();
}

async function login(event) {
    event.preventDefault();
    loginStatus.textContent = "Authenticating…";
    loginStatus.className = "request-status pending";

    try {
        const response = await fetch(controlApiBase + "/auth/login", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
            },
            body: new URLSearchParams(new FormData(loginForm)).toString()
        });
        const result = await readJsonResponse(response);
        if (!response.ok) {
            throw new Error(result.error || ("Login failed: " + response.status));
        }
        if (!roleConfig[result.user.role]) {
            throw new Error("This account cannot submit a route event.");
        }

        saveSession(result, rememberLogin.checked);
        loginForm.reset();
        showSession(result);
        loginStatus.textContent = "Identity verified.";
        loginStatus.className = "request-status success";
    } catch (error) {
        loginStatus.textContent = error.message;
        loginStatus.className = "request-status error";
    }
}

async function restoreSession() {
    const saved = readSavedSession();
    if (!saved) return;

    try {
        const stored = JSON.parse(saved);
        const response = await fetch(controlApiBase + "/auth/me", {
            headers: { Authorization: "Bearer " + stored.token }
        });
        const user = await readJsonResponse(response);
        if (!response.ok || !roleConfig[user.role]) throw new Error("Session expired");
        showSession({ token: stored.token, user });
    } catch {
        clearSession();
    }
}

function setRequired(selector, required) {
    document.querySelectorAll(selector).forEach((element) => {
        element.required = required;
    });
}

function populateAttachmentCategories(role) {
    attachmentCategory.replaceChildren();
    const categories = roleConfig[role]?.categories || {};
    Object.entries(categories).forEach(([value, label]) => {
        const option = document.createElement("option");
        option.value = value;
        option.textContent = label;
        attachmentCategory.append(option);
    });
}

function selectedBatch() {
    return availableBatches.find((batch) => batch.batchId === batchSelect.value);
}

function captureEditableFormState() {
    const values = {};
    const activeSection = document.querySelector(
        `[data-role-section="${currentRole()}"]`
    );
    const elements = [
        ...(activeSection
            ? [...activeSection.querySelectorAll("input, select, textarea")]
            : []),
        ...form.querySelectorAll("#confirmed, #typed-confirmation-name")
    ];
    for (const element of elements) {
        if (!element.name && element.id !== "typed-confirmation-name") continue;
        if (element.type === "file" || element.readOnly) continue;
        const key = element.name || element.id;
        values[key] = element.type === "checkbox" || element.type === "radio"
            ? element.checked
            : element.value;
    }
    return { batchId: batchSelect.value, values };
}

function restoreEditableFormState(state) {
    if (!state) return;
    if ([...batchSelect.options].some((option) => option.value === state.batchId)) {
        batchSelect.value = state.batchId;
        updateBatchSummary();
    }
    const activeSection = document.querySelector(
        `[data-role-section="${currentRole()}"]`
    );
    const elements = [
        ...(activeSection
            ? [...activeSection.querySelectorAll("input, select, textarea")]
            : []),
        ...form.querySelectorAll("#confirmed, #typed-confirmation-name")
    ];
    for (const element of elements) {
        if (!element.name && element.id !== "typed-confirmation-name") continue;
        if (element.type === "file" || element.readOnly) continue;
        const key = element.name || element.id;
        if (!(key in state.values)) continue;
        if (element.type === "checkbox" || element.type === "radio") {
            element.checked = Boolean(state.values[key]);
        } else {
            element.value = state.values[key];
        }
    }
    syncStoreLocationId();
    renderAttachmentList();
}

function updateBatchSummary() {
    const batch = selectedBatch();
    setCurrentStage(currentRole(), batch);
    batchProduct.value = batch?.product || "";
    batchHarvestDate.value = batch?.harvestDate || "";
    batchFarmLocation.value = batch?.farmLocation || "";
    if (deliveryLocationInput) {
        deliveryLocationInput.value = batch?.nextDestinationLabel || "";
        deliveryLocationInput.readOnly = true;
    }
    if (shipmentIdInput) {
        shipmentIdInput.value = currentRole() === "logistics"
            ? batch?.nextShipmentId || ""
            : "";
        shipmentIdInput.readOnly = currentRole() === "logistics";
    }
    if (vehicleContainerIdInput) {
        vehicleContainerIdInput.value = currentRole() === "logistics"
            ? batch?.nextVehicleContainerId || ""
            : "";
        vehicleContainerIdInput.readOnly = currentRole() === "logistics";
    }
    if (storageLotIdInput) {
        storageLotIdInput.value = currentRole() === "warehouse"
            ? batch?.nextStorageLotId || ""
            : "";
        storageLotIdInput.readOnly = currentRole() === "warehouse";
    }
    if (storageZoneRackIdInput) {
        storageZoneRackIdInput.value = currentRole() === "warehouse"
            ? batch?.nextStorageZoneRackId || ""
            : "";
        storageZoneRackIdInput.readOnly = currentRole() === "warehouse";
    }
    const destination = batch?.nextDestinationLabel
        ? " Destination: " + batch.nextDestinationLabel + "."
        : "";
    submitRecord.disabled = submissionCompleted || !batch ||
        (currentRole() === "logistics" && !batch?.nextDestinationLabel);
    batchStatus.textContent = batch
        ? "Next route stage: " + (batch.nextNodeLabel ||
            roleLabels[batch.nextStage] || batch.nextStage) + "." + destination
        : "Select a batch waiting for your route stage.";
    batchStatus.className = batch ? "request-status success" : "request-status pending";
}

function populateBatchSelect() {
    batchSelect.replaceChildren();
    availableBatches.forEach((batch) => {
        const option = document.createElement("option");
        option.value = batch.batchId;
        option.textContent = batch.batchId + " · " + batch.product;
        batchSelect.append(option);
    });
    batchSelect.value = availableBatches[0]?.batchId || "";
    updateBatchSummary();
}

async function loadBatches(role, { preserveForm = false, background = false } = {}) {
    if (!session || role === "supplier") return;
    const requestId = ++batchLoadRequestId;

    const preservedFormState = preserveForm ? captureEditableFormState() : null;

    if (!background) {
        batchStatus.textContent = "Loading batches...";
        batchStatus.className = "request-status pending";
        submitRecord.disabled = true;
    }

    try {
        const response = await fetch(controlApiBase + "/batches", {
            headers: { Authorization: "Bearer " + session.token }
        });
        const result = await readJsonResponse(response);
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) {
            throw new Error(result.error || ("Request failed: " + response.status));
        }
        if (!Array.isArray(result)) {
            throw new Error("The route batch response is malformed.");
        }
        if (requestId !== batchLoadRequestId || !session || currentRole() !== role) return;

        availableBatches = result.filter((batch) =>
            typeof batch.batchId === "string" &&
            typeof batch.nextNodeId === "string" &&
            batch.nextNodeId.length > 0 &&
            batch.nextStage === role &&
            batch.nextNodeUsername === session.user.username &&
            batch.status !== "completed"
        );
        populateBatchSelect();
        restoreEditableFormState(preservedFormState);
        await loadConfirmationPolicyForBatch(selectedBatch());
        if (availableBatches.length === 0) {
            batchStatus.textContent = "No batch is waiting for this route stage.";
            batchStatus.className = "request-status pending";
        }
    } catch (error) {
        if (requestId !== batchLoadRequestId || !session || currentRole() !== role) return;
        availableBatches = [];
        populateBatchSelect();
        clearConfirmationPolicy();
        batchStatus.textContent = error.message;
        batchStatus.className = "request-status error";
    }
}

function configureRole() {
    const role = currentRole();
    const config = roleConfig[role];
    if (!config) return;

    const supplier = role === "supplier";
    supplierMasterFields.hidden = !supplier;
    existingBatchFields.hidden = supplier;
    setRequired("#product-input", supplier);

    document.querySelectorAll("[data-role-section]").forEach((section) => {
        const active = section.dataset.roleSection === role;
        section.hidden = !active;
        section.querySelectorAll("input").forEach((input) => {
            input.required = active;
        });
    });
    storeLocationNumber.required = role === "supermarket";
    if (deliveryLocationInput) {
        deliveryLocationInput.readOnly = role !== "logistics";
    }

    populateAttachmentCategories(role);
    setCurrentStage(role);
    syncStoreLocationId();
    if (supplier) {
        submitRecord.disabled = submissionCompleted;
        batchStatus.textContent = "";
        loadConfirmationPolicyForBatch(null);
    } else {
        loadBatches(role);
    }
}

function renderAttachmentList() {
    attachmentList.replaceChildren();
    uploadedReferences.forEach((reference) => {
        const item = document.createElement("li");
        item.className = "attachment-item";
        item.textContent =
            reference.filename + " · " + reference.category + " · " + reference.cid;
        attachmentList.append(item);
    });

    for (const file of ipfsFiles.files) {
        const item = document.createElement("li");
        item.className = "attachment-item";
        item.textContent = file.name + " · " +
            selectedAttachmentCategoryLabel() + " · " + formatFileSize(file.size);
        attachmentList.append(item);
    }
}

async function uploadSelectedFiles() {
    const files = [...ipfsFiles.files];
    if (files.length === 0) return;

    for (const file of files) {
        const upload = new FormData();
        upload.append("category", attachmentCategory.value);
        upload.append("file", file);
        const response = await fetchWithTimeout(controlApiBase + "/ipfs/files", {
            method: "POST",
            headers: { Authorization: "Bearer " + session.token },
            body: upload
        }, IPFS_UPLOAD_TIMEOUT_MS);
        const result = await readJsonResponse(response);
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) throw new Error(result.error || "IPFS upload failed.");
        uploadedReferences.push(result);
    }

    ipfsFiles.value = "";
    renderAttachmentList();
}

function encodeIpfsReferences() {
    return uploadedReferences.map((reference) => [
        reference.category,
        reference.cid,
        reference.filename,
        reference.contentType,
        reference.size
    ].map((value) => encodeURIComponent(String(value))).join("|")).join(",");
}

function byteLength(value) {
    return new TextEncoder().encode(value).length;
}

function signatureField(name, value) {
    return name + ":" + byteLength(value) + ":" + value + "\n";
}

function canonicalIpfsReferences() {
    const compareBytes = (left, right) => {
        const leftBytes = new TextEncoder().encode(left);
        const rightBytes = new TextEncoder().encode(right);
        const length = Math.min(leftBytes.length, rightBytes.length);
        for (let index = 0; index < length; index += 1) {
            if (leftBytes[index] !== rightBytes[index]) {
                return leftBytes[index] - rightBytes[index];
            }
        }
        return leftBytes.length - rightBytes.length;
    };
    return [...uploadedReferences]
        .sort((left, right) => {
            const leftValues = [left.category, left.cid, left.filename,
                left.contentType, String(left.size)];
            const rightValues = [right.category, right.cid, right.filename,
                right.contentType, String(right.size)];
            for (let index = 0; index < leftValues.length; index += 1) {
                const comparison = compareBytes(leftValues[index], rightValues[index]);
                if (comparison !== 0) return comparison;
            }
            return 0;
        })
        .map((reference) => [
            reference.category,
            reference.cid,
            reference.filename,
            reference.contentType,
            reference.size
        ].map((value) => encodeURIComponent(String(value))).join("|"))
        .join(",");
}

function activeEventValues(role) {
    const section = document.querySelector(
        "[data-role-section=\"" + role + "\"]"
    );
    return Object.fromEntries(roleConfig[role].fields.map((field) => {
        const input = section.querySelector("[name=\"" + field + "\"]");
        return [field, input?.value.trim() || ""];
    }));
}

function signatureBatchAndProduct(role) {
    if (role === "supplier") {
        return { batchId: "SERVER_ALLOCATED", product: productInput.value.trim() };
    }
    return { batchId: batchSelect.value.trim(), product: batchProduct.value.trim() };
}

function buildSignaturePayload(challenge, method, name, role) {
    const identity = signatureBatchAndProduct(role);
    const values = [
        ["challenge", challenge],
        ["uid", session.user.uid],
        ["username", session.user.username],
        ["role", session.user.role],
        ["confirmationMethod", method],
        ["confirmationName", name],
        ["batchId", identity.batchId],
        ["product", identity.product],
        ["confirmed", "true"]
    ];
    Object.entries(activeEventValues(role)).forEach(([field, value]) => {
        values.push(["event." + field, value]);
    });
    values.push(["ipfsReferences", canonicalIpfsReferences()]);
    return values.map(([name, value]) => signatureField(name, value)).join("");
}

function bytesToBase64(bytes) {
    let binary = "";
    bytes.forEach((byte) => { binary += String.fromCharCode(byte); });
    return btoa(binary);
}

async function getSigningKey() {
    if (!window.crypto?.subtle) {
        throw new Error("This browser does not support Web Crypto digital signatures.");
    }
    const storageKey = "supply-chain-signing-key:" + session.user.uid;
    const stored = localStorage.getItem(storageKey);
    if (stored) {
        const jwk = JSON.parse(stored);
        return crypto.subtle.importKey(
            "jwk", jwk, { name: "ECDSA", namedCurve: "P-256" }, true,
            ["sign"]
        );
    }

    const keyPair = await crypto.subtle.generateKey(
        { name: "ECDSA", namedCurve: "P-256" }, true, ["sign", "verify"]
    );
    const privateJwk = await crypto.subtle.exportKey("jwk", keyPair.privateKey);
    localStorage.setItem(storageKey, JSON.stringify(privateJwk));
    return keyPair.privateKey;
}

async function publicKeyForSigningKey(privateKey) {
    const privateJwk = await crypto.subtle.exportKey("jwk", privateKey);
    const publicJwk = {
        kty: privateJwk.kty,
        crv: privateJwk.crv,
        x: privateJwk.x,
        y: privateJwk.y,
        key_ops: ["verify"],
        ext: true
    };
    return bytesToBase64(new Uint8Array(await crypto.subtle.exportKey(
        "spki",
        await crypto.subtle.importKey(
            "jwk", publicJwk, { name: "ECDSA", namedCurve: "P-256" }, true,
            ["verify"]
        )
    )));
}

async function createDigitalConfirmation(role) {
    if (!confirmationPolicy) throw new Error("Confirmation policy is still loading.");
    const method = selectedConfirmationMethod();
    if (!method) {
        throw new Error("Select a confirmation method.");
    }
    if (method !== "typed_name") {
        throw new Error("This confirmation method is unavailable in the current demo.");
    }
    if (!updateTypedNameState()) throw new Error(confirmationError.textContent);

    const name = typedConfirmationName.value.trim();
    const challengeResponse = await fetchWithTimeout(
        controlApiBase + "/confirmation/challenge",
        {
        headers: { Authorization: "Bearer " + session.token }
        },
        CONTROL_REQUEST_TIMEOUT_MS
    );
    const challengeResult = await readJsonResponse(challengeResponse);
    if (challengeResponse.status === 401) {
        clearSession();
        throw new Error("Your session has expired. Please log in again.");
    }
    if (!challengeResponse.ok) {
        throw new Error(challengeResult.error || "Unable to create a confirmation challenge.");
    }

    const payload = buildSignaturePayload(challengeResult.challenge, method, name, role);
    const privateKey = await getSigningKey();
    const signature = await crypto.subtle.sign(
        { name: "ECDSA", hash: "SHA-256" },
        privateKey,
        new TextEncoder().encode(payload)
    );
    return {
        method,
        name,
        challenge: challengeResult.challenge,
        payload,
        signature: bytesToBase64(new Uint8Array(signature)),
        publicKey: await publicKeyForSigningKey(privateKey)
    };
}

function buildRecordPayload(role, confirmation) {
    const payload = new URLSearchParams();
    const batch = selectedBatch();
    if (role !== "supplier") {
        payload.set("batchId", batchSelect.value);
        payload.set("routeId", batch?.routeId || "");
        payload.set("routeNodeId", batch?.nextNodeId || "");
    }

    if (role === "supplier") {
        payload.set("product", productInput.value);
    }

    const section = document.querySelector(
        "[data-role-section=\"" + role + "\"]"
    );
    roleConfig[role].fields.forEach((field) => {
        const input = section.querySelector("[name=\"" + field + "\"]");
        payload.set(field, input?.value || "");
    });

    payload.set("ipfsRefs", encodeIpfsReferences());
    payload.set("confirmed",
        document.querySelector("#confirmed").checked ? "true" : "false");
    if (confirmation) {
        payload.set("confirmationMethod", confirmation.method);
        payload.set("confirmationName", confirmation.name);
        payload.set("confirmationChallenge", confirmation.challenge);
        payload.set("signatureAlgorithm", "ECDSA-P256-SHA256");
        payload.set("signature", confirmation.signature);
        payload.set("signaturePublicKey", confirmation.publicKey);
        payload.set("signaturePayload", confirmation.payload);
    }
    return payload;
}

loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", logout);
batchSelect.addEventListener("change", async () => {
    updateBatchSummary();
    await loadConfirmationPolicyForBatch(selectedBatch());
});
ipfsFiles.addEventListener("change", renderAttachmentList);
attachmentCategory.addEventListener("change", renderAttachmentList);
storeLocationNumber.addEventListener("input", syncStoreLocationId);
typedConfirmationName.addEventListener("input", updateTypedNameState);
clearFormButton.addEventListener("click", clearRecordForm);

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!session) return;
    syncStoreLocationId();
    if (!form.reportValidity()) return;

    const role = currentRole();
    statusLine.textContent = "Submitting record...";
    statusLine.className = "request-status pending";
    submitRecord.disabled = true;
    recordSubmissionInFlight = true;

    try {
        await uploadSelectedFiles();
        statusLine.textContent = "Creating digital confirmation...";
        const confirmation = await createDigitalConfirmation(role);
        signatureStatus.textContent = "Digital signature ready. Server verification is required.";
        signatureStatus.className = "signature-status pending";

        const response = await fetchWithTimeout(controlApiBase + "/records", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: "Bearer " + session.token
            },
            body: buildRecordPayload(role, confirmation).toString()
        }, RECORD_REQUEST_TIMEOUT_MS);

        const result = await readJsonResponse(response);
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) {
            throw new Error(result.error || ("Request failed: " + response.status));
        }

        verificationTitle.textContent = result.verified ? "Verified ✓" : "Verification failed";
        verificationTitle.className = result.verified ? "verified" : "failed";
        document.querySelector("#batch-id-result").textContent = result.batchId;
        document.querySelector("#block-id").textContent = result.blockID;
        document.querySelector("#next-stage").textContent =
            result.nextNodeLabel || roleLabels[result.nextStage] || "Route complete";
        document.querySelector("#ipfs-count").textContent = result.ipfsCount;
        submissionCompleted = true;

        statusLine.textContent = "";
        statusLine.className = "request-status";
        signatureStatus.textContent = "";
        signatureStatus.className = "signature-status";
        if (submissionDialog && !submissionDialog.open) submissionDialog.showModal();
    } catch (error) {
        statusLine.textContent = "";
        statusLine.className = "request-status";
        signatureStatus.textContent = "";
        signatureStatus.className = "signature-status";
        if (session) {
            submitRecord.disabled = false;
            verificationTitle.textContent = "Submission failed";
            verificationTitle.className = "failed";
            document.querySelector("#batch-id-result").textContent = batchSelect.value || "Not created";
            document.querySelector("#block-id").textContent = "No block created";
            document.querySelector("#next-stage").textContent = error.message;
            document.querySelector("#ipfs-count").textContent = uploadedReferences.length;
            if (submissionDialog && !submissionDialog.open) submissionDialog.showModal();
        }
    } finally {
        recordSubmissionInFlight = false;
        if (submissionCompleted) liveRefreshQueued = false;
        if (liveRefreshQueued && !submissionCompleted && session) {
            refreshAssignedStageFromLiveEvent();
        }
    }
});

restoreSession();
