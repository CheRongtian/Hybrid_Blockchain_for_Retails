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
const resultCard = document.querySelector("#result-card");
const verificationTitle = document.querySelector("#verification-title");
const batchIdInput = document.querySelector("#batch-id-input");
const productInput = document.querySelector("#product-input");
const supplierMasterFields = document.querySelector("#supplier-master-fields");
const existingBatchFields = document.querySelector("#existing-batch-fields");
const batchSelect = document.querySelector("#batch-select");
const batchProduct = document.querySelector("#batch-product");
const batchHarvestDate = document.querySelector("#batch-harvest-date");
const batchFarmLocation = document.querySelector("#batch-farm-location");
const batchStatus = document.querySelector("#batch-status");
const attachmentCategory = document.querySelector("#attachment-category");
const ipfsFiles = document.querySelector("#ipfs-files");
const attachmentList = document.querySelector("#attachment-list");
const submitRecord = document.querySelector("#submit-record");
const controlApiBase = "http://127.0.0.1:8081/api";
const sessionKey = "supply-chain-user-session";

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
            "departureTime", "arrivalTime", "temperatureHumiditySummary",
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
            "temperatureHumiditySummary", "storageZoneRackId"
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

function currentRole() {
    return session?.user?.role || "";
}

function setCurrentStage(role) {
    currentStage.value = roleLabels[role] || role;
}

function showSession(result) {
    session = result;
    loginCard.hidden = true;
    recordCard.hidden = false;
    identityStatus.textContent =
        "Logged in: " + result.user.username + " · " +
        result.user.role + " · " + result.user.organizationId;
    setCurrentStage(result.user.role);
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

function clearAttachmentState() {
    uploadedReferences = [];
    ipfsFiles.value = "";
    renderAttachmentList();
}

function clearSession() {
    session = null;
    availableBatches = [];
    sessionStorage.removeItem(sessionKey);
    localStorage.removeItem(sessionKey);
    loginCard.hidden = false;
    recordCard.hidden = true;
    resultCard.hidden = true;
    form.reset();
    clearAttachmentState();
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
        const result = await response.json();
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
        const user = await response.json();
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

function updateBatchSummary() {
    const batch = selectedBatch();
    batchProduct.value = batch?.product || "";
    batchHarvestDate.value = batch?.harvestDate || "";
    batchFarmLocation.value = batch?.farmLocation || "";
    submitRecord.disabled = !batch;
    batchStatus.textContent = batch
        ? "Next route stage: " + (roleLabels[batch.nextStage] || batch.nextStage) + "."
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
    updateBatchSummary();
}

async function loadBatches(role) {
    if (!session || role === "supplier") return;

    batchStatus.textContent = "Loading batches...";
    batchStatus.className = "request-status pending";
    submitRecord.disabled = true;

    try {
        const response = await fetch(controlApiBase + "/batches", {
            headers: { Authorization: "Bearer " + session.token }
        });
        const result = await response.json();
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) {
            throw new Error(result.error || ("Request failed: " + response.status));
        }

        availableBatches = result.filter((batch) => batch.nextStage === role);
        populateBatchSelect();
        if (availableBatches.length === 0) {
            batchStatus.textContent = "No batch is waiting for this route stage.";
            batchStatus.className = "request-status pending";
        }
    } catch (error) {
        availableBatches = [];
        populateBatchSelect();
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
    setRequired("#batch-id-input, #product-input", supplier);

    document.querySelectorAll("[data-role-section]").forEach((section) => {
        const active = section.dataset.roleSection === role;
        section.hidden = !active;
        section.querySelectorAll("input").forEach((input) => {
            input.required = active;
        });
    });

    populateAttachmentCategories(role);
    setCurrentStage(role);
    if (supplier) {
        submitRecord.disabled = false;
        batchStatus.textContent = "";
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
        const response = await fetch(controlApiBase + "/ipfs/files", {
            method: "POST",
            headers: { Authorization: "Bearer " + session.token },
            body: upload
        });
        const result = await response.json();
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

function buildRecordPayload(role) {
    const payload = new URLSearchParams();
    const batchId = role === "supplier" ? batchIdInput.value : batchSelect.value;
    payload.set("batchId", batchId);

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
    return payload;
}

loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", logout);
batchSelect.addEventListener("change", updateBatchSummary);
ipfsFiles.addEventListener("change", renderAttachmentList);
attachmentCategory.addEventListener("change", renderAttachmentList);

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!session) return;

    const role = currentRole();
    statusLine.textContent = "Submitting record...";
    statusLine.className = "request-status pending";
    resultCard.hidden = true;
    submitRecord.disabled = true;

    try {
        await uploadSelectedFiles();
        statusLine.textContent = "Verifying Merkle proof...";

        const response = await fetch(controlApiBase + "/records", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: "Bearer " + session.token
            },
            body: buildRecordPayload(role).toString()
        });

        const result = await response.json();
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) {
            throw new Error(result.error || ("Request failed: " + response.status));
        }

        verificationTitle.textContent = result.verified ? "Verified ✓" : "Verification failed";
        verificationTitle.className = result.verified ? "verified" : "failed";
        document.querySelector("#block-id").textContent = result.blockID;
        document.querySelector("#next-stage").textContent =
            roleLabels[result.nextStage] || "Route complete";
        document.querySelector("#ipfs-count").textContent = result.ipfsCount;
        resultCard.hidden = false;
        form.reset();
        clearAttachmentState();
        setCurrentStage(role);
        configureRole();

        statusLine.textContent = result.verified
            ? "Record saved. Merkle proof verified."
            : "Record saved, but Merkle proof verification failed.";
        statusLine.className = result.verified
            ? "request-status success"
            : "request-status error";
    } catch (error) {
        statusLine.textContent = error.message;
        statusLine.className = "request-status error";
        if (session) submitRecord.disabled = false;
    }
});

restoreSession();
