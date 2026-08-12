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

function currentRole() {
    return session?.user?.role || "";
}

function setCurrentStage(role) {
    currentStage.value = roleLabels[role] || role;
}

function syncStoreLocationId() {
    if (!storeLocationNumber || !storeLocationId) return;
    const number = storeLocationNumber.value.trim();
    storeLocationId.value = /^[0-9]{4}$/.test(number) ? "STORE-" + number : "";
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
    loadConfirmationPolicy();
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
    confirmationPolicy = null;
    confirmationMethods.replaceChildren();
    confirmationPanel.hidden = true;
    confirmationError.textContent = "";
    signatureStatus.textContent = "";
    form.reset();
    clearAttachmentState();
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
    const configured = [
        ["typed_name", "Typed name", Boolean(policy.typedName), true],
        ["handwritten", "Handwritten signature", Boolean(policy.handwritten), false],
        ["face", "Face confirmation", Boolean(policy.face), false]
    ];
    const available = configured.filter(([, , enabled, implemented]) =>
        enabled && implemented
    );
    confirmationPolicySummary.textContent =
        `Select one method enabled for the ${roleLabels[policy.role] || policy.role} role.`;

    for (const [value, label, enabled, implemented] of configured) {
        if (!enabled) continue;
        const wrapper = document.createElement("label");
        wrapper.className = implemented
            ? "confirmation-method"
            : "confirmation-method disabled";
        const input = document.createElement("input");
        input.type = "radio";
        input.name = "confirmation-method";
        input.value = value;
        input.disabled = !implemented;
        const text = document.createElement("span");
        text.textContent = implemented ? label : `${label} (Unavailable in this demo)`;
        wrapper.append(input, text);
        confirmationMethods.append(wrapper);
        input.addEventListener("change", updateTypedNameState);
    }

    const selected = selectedConfirmationMethod();
    if (available.length === 0) {
        confirmationPolicySummary.textContent =
            "No usable confirmation method is available in this demo. Enable Typed name in the control panel.";
        setConfirmationError("A usable confirmation method is required for this demo.");
    } else if (!selected) {
        setConfirmationError("Select a confirmation method to continue.");
    } else {
        updateTypedNameState();
    }
}

async function loadConfirmationPolicy() {
    if (!session) return;
    try {
        const response = await fetch(controlApiBase + "/confirmation-policy", {
            headers: { Authorization: "Bearer " + session.token }
        });
        const policy = await response.json();
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) throw new Error(policy.error || ("Request failed: " + response.status));
        renderConfirmationPolicy(policy);
    } catch (error) {
        confirmationPanel.hidden = false;
        confirmationPolicySummary.textContent = error.message;
        setConfirmationError(error.message);
    }
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
    setRequired("#product-input", supplier);

    document.querySelectorAll("[data-role-section]").forEach((section) => {
        const active = section.dataset.roleSection === role;
        section.hidden = !active;
        section.querySelectorAll("input").forEach((input) => {
            input.required = active;
        });
    });
    storeLocationNumber.required = role === "supermarket";

    populateAttachmentCategories(role);
    setCurrentStage(role);
    syncStoreLocationId();
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
    const challengeResponse = await fetch(controlApiBase + "/confirmation/challenge", {
        headers: { Authorization: "Bearer " + session.token }
    });
    const challengeResult = await challengeResponse.json();
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
    if (role !== "supplier") payload.set("batchId", batchSelect.value);

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
batchSelect.addEventListener("change", updateBatchSummary);
ipfsFiles.addEventListener("change", renderAttachmentList);
attachmentCategory.addEventListener("change", renderAttachmentList);
storeLocationNumber.addEventListener("input", syncStoreLocationId);
typedConfirmationName.addEventListener("input", updateTypedNameState);

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!session) return;
    syncStoreLocationId();
    if (!form.reportValidity()) return;

    const role = currentRole();
    statusLine.textContent = "Submitting record...";
    statusLine.className = "request-status pending";
    resultCard.hidden = true;
    submitRecord.disabled = true;

    try {
        await uploadSelectedFiles();
        statusLine.textContent = "Creating digital confirmation...";
        const confirmation = await createDigitalConfirmation(role);
        signatureStatus.textContent = "Digital signature ready. Server verification is required.";
        signatureStatus.className = "signature-status pending";

        const response = await fetch(controlApiBase + "/records", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: "Bearer " + session.token
            },
            body: buildRecordPayload(role, confirmation).toString()
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
        document.querySelector("#batch-id-result").textContent = result.batchId;
        document.querySelector("#block-id").textContent = result.blockID;
        document.querySelector("#next-stage").textContent =
            roleLabels[result.nextStage] || "Route complete";
        document.querySelector("#ipfs-count").textContent = result.ipfsCount;
        resultCard.hidden = false;
        form.reset();
        clearAttachmentState();
        setCurrentStage(role);
        configureRole();
        if (confirmationPolicy) renderConfirmationPolicy(confirmationPolicy);

        statusLine.textContent = result.verified
            ? (result.signatureVerified
                ? "Verified ✓ Digital signature and Merkle record saved."
                : "Verified ✓ Merkle record saved.")
            : "Verification failed. Record saved.";
        statusLine.className = result.verified
            ? "request-status success"
            : "request-status error";
        signatureStatus.textContent = result.signatureVerified
            ? "Digital signature verified by the server."
            : confirmation
                ? "Digital signature verification failed."
                : "The selected confirmation method did not produce a verified signature.";
        signatureStatus.className = result.signatureVerified
            ? "signature-status success"
            : confirmation
                ? "signature-status error"
                : "signature-status";
    } catch (error) {
        statusLine.textContent = error.message;
        statusLine.className = "request-status error";
        if (session) submitRecord.disabled = false;
    }
});

restoreSession();
