const loginCard = document.querySelector("#login-card");
const loginForm = document.querySelector("#login-form");
const loginStatus = document.querySelector("#login-status");
const rememberLogin = document.querySelector("#remember-login");
const dashboard = document.querySelector("#dashboard");
const identityStatus = document.querySelector("#identity-status");
const logoutButton = document.querySelector("#logout-button");
const list = document.querySelector("#record-list");
const statusLine = document.querySelector("#load-status");
const workflowCanvas = document.querySelector("#workflow-canvas");
const workflowStatus = document.querySelector("#workflow-status");
const workflowRouteBadge = document.querySelector("#workflow-route-badge");
const workflowBatchSelect = document.querySelector("#workflow-batch-select");
const workflowNodeType = document.querySelector("#workflow-node-type");
const workflowNodeAccount = document.querySelector("#workflow-node-account");
const workflowAddNode = document.querySelector("#workflow-add-node");
const workflowDeleteNode = document.querySelector("#workflow-delete-node");
const workflowDeleteEdge = document.querySelector("#workflow-delete-edge");
const workflowAutoLayout = document.querySelector("#workflow-auto-layout");
const workflowResetRoute = document.querySelector("#workflow-reset-route");
const workflowUndo = document.querySelector("#workflow-undo");
const workflowRedo = document.querySelector("#workflow-redo");
const workflowScene = document.querySelector("#workflow-scene");
const workflowEdgeLayer = document.querySelector("#workflow-edge-layer");
const workflowNodeLayer = document.querySelector("#workflow-node-layer");
const workflowZoomOut = document.querySelector("#workflow-zoom-out");
const workflowFitView = document.querySelector("#workflow-fit-view");
const workflowZoomIn = document.querySelector("#workflow-zoom-in");
const workflowZoomLevel = document.querySelector("#workflow-zoom-level");
const rolePolicyList = document.querySelector("#role-policy-list");
const policyRoleCount = document.querySelector("#policy-role-count");
const confirmationPolicyForm = document.querySelector("#confirmation-policy-form");
const savePolicyButton = document.querySelector("#save-policy-button");
const policyStatus = document.querySelector("#policy-status");
const snapshotRefreshProduct = document.querySelector("#snapshot-refresh-product");
const snapshotRefreshValue = document.querySelector("#snapshot-refresh-value");
const snapshotRefreshUnit = document.querySelector("#snapshot-refresh-unit");
const snapshotRefreshPolicyStatus = document.querySelector("#snapshot-refresh-policy-status");
const snapshotPreviewForm = document.querySelector("#snapshot-preview-form");
const snapshotBatchSelect = document.querySelector("#snapshot-batch-select");
const snapshotBatchCount = document.querySelector("#snapshot-batch-count");
const snapshotEvidenceList = document.querySelector("#snapshot-evidence-list");
const generateSnapshotButton = document.querySelector("#generate-snapshot-button");
const snapshotStatus = document.querySelector("#snapshot-status");
const snapshotPreview = document.querySelector("#snapshot-preview");
const snapshotId = document.querySelector("#snapshot-id");
const snapshotPublicRoot = document.querySelector("#snapshot-public-root");
const snapshotPrivateHash = document.querySelector("#snapshot-private-hash");
const snapshotFieldCount = document.querySelector("#snapshot-field-count");
const snapshotManifestJson = document.querySelector("#snapshot-manifest-json");
const snapshotExcludedFields = document.querySelector("#snapshot-excluded-fields");
const publishSnapshotButton = document.querySelector("#publish-snapshot-button");
const snapshotPublishStatus = document.querySelector("#snapshot-publish-status");
const sessionKey = "supply-chain-control-session";
let session = null;
let workflowData = null;
let workflowBatchId = "";
let workflowSelectedNodeId = "";
let workflowSelectedEdgeIndex = -1;
let workflowPointerState = null;
let workflowViewport = { x: 0, y: 0, zoom: 1 };
let workflowViewportReady = false;
let workflowLayoutInitialized = false;
let workflowSceneBounds = { minX: 0, minY: 0, width: 900, height: 320 };
let workflowHistory = { past: [], future: [] };
let workflowWheelState = { pendingDelta: 0, anchor: null, frame: 0 };
let snapshotBatches = [];
let publicationCandidate = null;
let snapshotPreviewBatchId = "";
let snapshotPreviewRouteFingerprint = "";
let snapshotPreviewPrivateHash = "";
let workflowRouteDirty = false;
let workflowAutoSaveTimer = 0;
let workflowAutoSaveInFlight = false;
let workflowAutoSaveQueued = false;
let snapshotRefreshInFlight = false;
let snapshotRefreshQueued = false;
let snapshotRefreshPolicies = [];
let snapshotRefreshDefaultSeconds = 3600;
let snapshotRefreshPoliciesAvailable = false;
let snapshotRefreshEditorProduct = "";
let confirmationPolicies = [];
let confirmationPolicyLoadId = 0;
let workflowLoadId = 0;
let chainGraph = { nodes: [], edges: [] };
let chainGraphLoaded = false;
let liveEventSource = null;
let liveRefreshInFlight = false;
let liveRefreshQueued = false;
let liveRefreshQueuedType = "";
let dashboardReady = false;
let ignoreInitialLiveStateSync = false;
let snapshotOperation = "";

function showSession(result) {
    session = result;
    loginCard.hidden = true;
    dashboard.hidden = false;
    identityStatus.textContent =
        `Logged in: ${result.user.username} · ${result.user.role} · ${result.user.organizationId}`;
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
        const contentType = response.headers.get("content-type") || "";
        if (!contentType.toLowerCase().includes("application/json")) {
            throw new Error(
                "The running control server does not support this request. " +
                "Rebuild and restart the control server."
            );
        }
        throw new Error(
            "The control server returned invalid JSON. Rebuild and restart the control server."
        );
    }
}

function clearSession() {
    stopLiveUpdates();
    dashboardReady = false;
    ignoreInitialLiveStateSync = false;
    snapshotOperation = "";
    workflowLoadId += 1;
    confirmationPolicyLoadId += 1;
    if (workflowAutoSaveTimer) {
        window.clearTimeout(workflowAutoSaveTimer);
        workflowAutoSaveTimer = 0;
    }
    session = null;
    sessionStorage.removeItem(sessionKey);
    localStorage.removeItem(sessionKey);
    loginCard.hidden = false;
    dashboard.hidden = true;
    list.replaceChildren();
    workflowData = null;
    workflowBatchId = "";
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    workflowViewport = { x: 0, y: 0, zoom: 1 };
    workflowViewportReady = false;
    workflowLayoutInitialized = false;
    workflowHistory = { past: [], future: [] };
    workflowWheelState = { pendingDelta: 0, anchor: null, frame: 0 };
    chainGraph = { nodes: [], edges: [] };
    chainGraphLoaded = false;
    workflowBatchSelect.value = "";
    workflowEdgeLayer.replaceChildren();
    workflowNodeLayer.replaceChildren();
    snapshotBatches = [];
    snapshotBatchSelect.replaceChildren();
    snapshotEvidenceList.replaceChildren();
    snapshotPreview.hidden = true;
    publicationCandidate = null;
    snapshotPreviewBatchId = "";
    snapshotPreviewRouteFingerprint = "";
    snapshotPreviewPrivateHash = "";
    workflowRouteDirty = false;
    workflowAutoSaveInFlight = false;
    workflowAutoSaveQueued = false;
    snapshotRefreshInFlight = false;
    snapshotRefreshQueued = false;
    snapshotRefreshPolicies = [];
    snapshotRefreshDefaultSeconds = 3600;
    snapshotRefreshPoliciesAvailable = false;
    snapshotRefreshEditorProduct = "";
    snapshotRefreshProduct.textContent = "Select a completed batch";
    snapshotRefreshValue.value = "1";
    snapshotRefreshValue.disabled = true;
    snapshotRefreshUnit.value = "hours";
    snapshotRefreshUnit.disabled = true;
    snapshotRefreshPolicyStatus.textContent = "";
    snapshotRefreshPolicyStatus.className = "status";
}

function stopLiveUpdates() {
    if (liveEventSource) {
        liveEventSource.close();
        liveEventSource = null;
    }
    liveRefreshInFlight = false;
    liveRefreshQueued = false;
    liveRefreshQueuedType = "";
}

function queueLiveRefreshType(eventType) {
    liveRefreshQueued = true;
    const priorities = {
        snapshot_refresh_policy_changed: 1,
        snapshot_published: 2,
        batch_changed: 3,
        route_changed: 4,
        state_sync: 5
    };
    if ((priorities[eventType] || 0) >=
        (priorities[liveRefreshQueuedType] || 0)) {
        liveRefreshQueuedType = eventType;
    }
}

function liveRefreshBlocked() {
    return !dashboardReady || workflowRouteDirty || workflowAutoSaveInFlight ||
        Boolean(snapshotOperation);
}

function queueLiveRefresh(eventType = "") {
    if (!session) return;
    if (liveRefreshBlocked()) {
        queueLiveRefreshType(eventType);
        return;
    }
    if (liveRefreshInFlight) {
        queueLiveRefreshType(eventType);
        return;
    }
    liveRefreshInFlight = true;
    (async () => {
        try {
            const routeChanged = eventType === "route_changed" ||
                eventType === "state_sync";
            const chainChanged = routeChanged || eventType === "batch_changed";
            if (routeChanged) {
                const selectedBatchId = workflowBatchId;
                await loadWorkflowScopes();
                if (!workflowRouteDirty) await loadWorkflowView(selectedBatchId);
            } else if (chainChanged) {
                await loadWorkflowView(workflowBatchId);
            }
            const refreshes = [];
            if (eventType !== "snapshot_refresh_policy_changed") {
                refreshes.push(loadSnapshotCandidates());
            }
            if (eventType === "state_sync" ||
                eventType === "snapshot_refresh_policy_changed") {
                refreshes.push(loadSnapshotRefreshPolicies());
            }
            await Promise.all(refreshes);
        } catch {
            // The page's individual loaders expose their own actionable errors.
        } finally {
            liveRefreshInFlight = false;
            if (liveRefreshQueued && !liveRefreshBlocked()) {
                const queuedType = liveRefreshQueuedType;
                liveRefreshQueued = false;
                liveRefreshQueuedType = "";
                queueLiveRefresh(queuedType);
            }
        }
    })();
}

function startLiveUpdates() {
    stopLiveUpdates();
    ignoreInitialLiveStateSync = true;
    liveEventSource = new EventSource("/api/events");
    liveEventSource.onmessage = (event) => {
        if (!event.data) return;
        try {
            const payload = JSON.parse(event.data);
            if (["state_sync", "route_changed", "batch_changed", "snapshot_published",
                "snapshot_refresh_policy_changed"].includes(payload.type)) {
                if (payload.type === "state_sync" && ignoreInitialLiveStateSync) {
                    ignoreInitialLiveStateSync = false;
                    return;
                }
                queueLiveRefresh(payload.type);
            }
        } catch {
            // Ignore malformed broadcast data; normal API loads remain available.
        }
    };
}

async function initializeDashboard(result) {
    showSession(result);
    dashboardReady = false;
    await loadDashboard();
    if (!session || session.token !== result.token) return;
    dashboardReady = true;
    startLiveUpdates();
}

async function logout() {
    const token = session?.token;
    if(token)
    {
        try
        {
            await fetch("/api/auth/logout", {
                method: "POST",
                headers: { Authorization: `Bearer ${token}` }
            });
        }
        catch
        {
            // Local logout still clears the stored session if the server is unavailable.
        }
    }
    clearSession();
}

async function login(event) {
    event.preventDefault();
    loginStatus.textContent = "Authenticating administrator...";
    loginStatus.className = "status pending";

    try {
        const response = await fetch("/api/auth/login", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
            },
            body: new URLSearchParams(new FormData(loginForm)).toString()
        });
        const result = await readJsonResponse(response);
        if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);
        if (result.user.role !== "admin") throw new Error("This account does not have control-panel access.");

        saveSession(result, rememberLogin.checked);
        loginForm.reset();
        await initializeDashboard(result);
    } catch (error) {
        loginStatus.textContent = error.message;
        loginStatus.className = "status error";
    }
}

async function restoreSession() {
    const saved = readSavedSession();
    if (!saved) return;

    try {
        const stored = JSON.parse(saved);
        const response = await fetch("/api/auth/me", {
            headers: { Authorization: `Bearer ${stored.token}` }
        });
        const user = await readJsonResponse(response);
        if (!response.ok || user.role !== "admin") throw new Error("Session expired");
        await initializeDashboard({ token: stored.token, user });
    } catch {
        clearSession();
    }
}

function setPolicyStatus(message, state = "") {
    policyStatus.textContent = message;
    policyStatus.className = state ? `status ${state}` : "status";
}

function policyOption(nodeId, suffix, label, enabled) {
    const wrapper = document.createElement("label");
    wrapper.className = "policy-option";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.name = `policy-${nodeId}-${suffix}`;
    input.dataset.nodeId = nodeId;
    input.dataset.policySuffix = suffix;
    input.value = "true";
    input.checked = Boolean(enabled);
    input.setAttribute("aria-label", `${nodeId} ${label}`);
    const text = document.createElement("span");
    text.textContent = label;
    wrapper.append(input, text);
    return wrapper;
}

function renderConfirmationPolicies(policies = confirmationPolicies) {
    confirmationPolicies = policies.filter((policy) => policy.nodeId);
    rolePolicyList.replaceChildren();
    const header = document.createElement("div");
    header.className = "policy-matrix-header";
    for (const label of ["Route node / account", "Typed name", "Handwritten", "Face"]) {
        const cell = document.createElement("span");
        cell.textContent = label;
        header.append(cell);
    }
    rolePolicyList.append(header);
    for (const policy of confirmationPolicies) {
        const nodeId = policy.nodeId;
        const label = policy.nodeLabel || nodeId;
        const row = document.createElement("div");
        row.className = "role-policy";
        row.dataset.nodeId = nodeId;
        const roleName = document.createElement("div");
        roleName.className = "policy-role-name";
        const name = document.createElement("strong");
        name.textContent = label;
        const account = document.createElement("small");
        account.className = "policy-account";
        account.textContent = `${policy.role || "route"} · ${policy.username || "unassigned"}`;
        roleName.append(name, account);
        const typed = policyOption(nodeId, "typedName", "Typed name", policy.typedName);
        const handwritten = policyOption(
            nodeId, "handwritten", "Handwritten", policy.handwritten
        );
        const face = policyOption(nodeId, "face", "Face", policy.face);
        row.append(roleName, typed, handwritten, face);
        rolePolicyList.append(row);
    }
    policyRoleCount.textContent = `${confirmationPolicies.length} route nodes configured`;
}

function workflowLinkedNodes(workflow = workflowData) {
    if (!(workflow?.edges || []).length) return [];
    return workflowPreviewRouteOrder(workflow);
}

function hasSameRouteNodeIds(policies, nodes) {
    const expected = new Set((nodes || []).map((node) => node.id).filter(Boolean));
    const actual = new Set((policies || []).map((policy) => policy.nodeId).filter(Boolean));
    return expected.size === actual.size &&
        [...expected].every((nodeId) => actual.has(nodeId));
}

async function loadConfirmationPolicy() {
    if (!session) return;
    const loadId = ++confirmationPolicyLoadId;
    const requestedBatchId = workflowBatchId;
    const requestedRouteId = workflowData?.routeId || "";
    const requestedWorkflowSnapshot = workflowSnapshot();
    const requestedLinkedNodes = workflowLinkedNodes(workflowData);
    confirmationPolicies = [];
    rolePolicyList.replaceChildren();
    policyRoleCount.textContent = "Loading route-node policies";
    savePolicyButton.disabled = true;
    if (!workflowData?.nodes?.length) {
        policyRoleCount.textContent = "No route loaded";
        setPolicyStatus("Load a route before configuring route-node policies.", "pending");
        return;
    }
    if (!requestedLinkedNodes.length) {
        policyRoleCount.textContent = "No connected route nodes";
        setPolicyStatus(
            "Connect route nodes before configuring route-node policies.",
            "pending"
        );
        return;
    }
    try {
        const query = new URLSearchParams();
        if (requestedBatchId) {
            query.set("batchId", requestedBatchId);
        } else if (requestedRouteId) {
            query.set("routeId", requestedRouteId);
        }
        const suffix = query.toString() ? `?${query.toString()}` : "";
        const response = await fetch("/api/confirmation-policy" + suffix, {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        if (!Array.isArray(result.policies)) {
            throw new Error("The route-node policy response is malformed.");
        }
        if (loadId !== confirmationPolicyLoadId ||
            workflowBatchId !== requestedBatchId ||
            workflowData?.routeId !== requestedRouteId ||
            workflowSnapshot() !== requestedWorkflowSnapshot) {
            return;
        }
        if (!hasSameRouteNodeIds(result.policies, requestedLinkedNodes)) {
            throw new Error("The server returned an incomplete route-node policy set.");
        }
        renderConfirmationPolicies(result.policies);
        savePolicyButton.disabled = false;
        setPolicyStatus(
            "Select at least one confirmation method for every connected route node.",
            "success"
        );
    } catch (error) {
        if (loadId === confirmationPolicyLoadId &&
            workflowBatchId === requestedBatchId &&
            workflowData?.routeId === requestedRouteId &&
            workflowSnapshot() === requestedWorkflowSnapshot) {
            policyRoleCount.textContent = "Policies unavailable";
            savePolicyButton.disabled = true;
            setPolicyStatus(error.message, "error");
        }
    }
}

async function saveConfirmationPolicy(event) {
    event.preventDefault();
    if (!session || !workflowData) return;
    if (workflowRouteDirty || workflowAutoSaveInFlight) {
        setPolicyStatus("Wait for the route change to finish synchronizing.", "pending");
        return;
    }

    const rows = [...rolePolicyList.querySelectorAll(".role-policy")];
    const policies = [];
    for (const row of rows) {
        const nodeId = row.dataset.nodeId || "";
        const enabled = [...row.querySelectorAll("input[data-policy-suffix]:checked")];
        if (!nodeId || enabled.length === 0) {
            const label = row.querySelector(".policy-role-name strong")?.textContent || nodeId;
            setPolicyStatus(`Select at least one method for ${label}.`, "error");
            return;
        }
        const methods = new Set(enabled.map((input) => input.dataset.policySuffix));
        policies.push([
            nodeId,
            methods.has("typedName") ? "true" : "false",
            methods.has("handwritten") ? "true" : "false",
            methods.has("face") ? "true" : "false"
        ].join("|"));
    }
    if (policies.length === 0) {
        setPolicyStatus("Connect the route before saving confirmation methods.", "error");
        return;
    }

    const routeId = workflowData.routeId || "";
    const batchId = workflowBatchId;
    savePolicyButton.disabled = true;
    setPolicyStatus("Saving confirmation methods...", "pending");
    try {
        const response = await fetch("/api/confirmation-policy", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: `Bearer ${session.token}`
            },
            body: new URLSearchParams({
                batchId,
                routeId,
                policies: policies.join(";")
            }).toString()
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            throw new Error(result.error || "Unable to save confirmation methods.");
        }
        if (!Array.isArray(result.policies) ||
            !hasSameRouteNodeIds(
                result.policies,
                rows.map((row) => ({ id: row.dataset.nodeId })))) {
            throw new Error("The saved confirmation-method response is incomplete.");
        }
        renderConfirmationPolicies(result.policies);
        setPolicyStatus("Route-node confirmation methods saved.", "success");
    } catch (error) {
        setPolicyStatus(error.message, "error");
    } finally {
        if (session && workflowData?.routeId === routeId && workflowBatchId === batchId) {
            savePolicyButton.disabled = false;
        }
    }
}

function appendNodeLine(node, label, value) {
    const line = document.createElement("p");
    const strong = document.createElement("strong");
    strong.textContent = `${label}: `;
    line.append(strong, document.createTextNode(value || "Unknown"));
    node.append(line);
}

function shortHash(hash) {
    if (!hash) return "Unknown hash";
    if (hash.length <= 18) return hash;
    return `${hash.slice(0, 10)}…${hash.slice(-8)}`;
}

function merkleNodeLabel(node, depth = 0) {
    if (node.kind === "root") return "Root";
    if (node.kind === "internal") return `Branch · Level ${depth}`;
    if (node.kind === "duplicate") return "Duplicate Hash";
    return node.fieldName ? `Leaf ${node.leafIndex}: ${node.fieldName}` : `Leaf ${node.leafIndex}`;
}

function appendMerkleNodeDetails(container, node) {
    const type = document.createElement("p");
    type.textContent = `Type: ${node.kind}`;
    container.append(type);

    const hash = document.createElement("code");
    hash.textContent = `Hash: ${node.hash || "Unknown"}`;
    container.append(hash);

    if (node.kind === "leaf") {
        const value = document.createElement("p");
        value.textContent = `Value: ${node.value || "Empty"}`;
        container.append(value);

        const proof = document.createElement("code");
        proof.textContent = `Proof: ${node.proof || "None"}`;
        container.append(proof);
    }

    if (node.kind === "duplicate") {
        const note = document.createElement("p");
        note.textContent = "This node repeats the last hash to complete the level.";
        container.append(note);
    }

    const verification = document.createElement("p");
    verification.textContent = `Verification: ${node.verified ? "Verified" : "Failed"}`;
    verification.className = node.verified ? "status success" : "status error";
    container.append(verification);
}

function showMerkleNodeDetails(container, node, depth = 0) {
    container.replaceChildren();

    const title = document.createElement("h3");
    title.textContent = merkleNodeLabel(node, depth);
    container.append(title);

    appendMerkleNodeDetails(container, node);
}

function renderMerkleNode(node, detailsPanel, depth = 0) {
    const item = document.createElement("div");
    item.className = `merkle-tree-item merkle-${node.kind}`;

    const row = document.createElement("div");
    const stateClass = node.kind === "duplicate"
        ? "duplicate-state"
        : node.verified ? "verified-state" : "failed-state";
    row.className = `merkle-tree-row ${stateClass}`;
    row.style.setProperty("--tree-depth", depth);

    const hasChildren = Boolean(node.children?.length);
    const toggle = document.createElement("button");
    toggle.type = "button";
    toggle.className = "merkle-tree-toggle";
    toggle.textContent = hasChildren ? "+" : "";
    toggle.disabled = !hasChildren;
    toggle.setAttribute("aria-label", hasChildren ? "Expand node" : "Leaf node");

    const nodeButton = document.createElement("button");
    nodeButton.type = "button";
    nodeButton.className = `merkle-tree-node-button ${stateClass}`;
    nodeButton.addEventListener("click", () => showMerkleNodeDetails(detailsPanel, node, depth));
    const label = document.createElement("span");
    label.textContent = merkleNodeLabel(node, depth);
    nodeButton.append(label);

    const state = document.createElement("span");
    state.className = `merkle-state ${stateClass}`;
    state.title = node.kind === "duplicate"
        ? "Duplicated hash"
        : node.verified ? "Verified" : "Failed";
    nodeButton.append(state);
    row.append(toggle, nodeButton);
    item.append(row);

    if (hasChildren) {
        const children = document.createElement("div");
        children.className = "merkle-tree-children";
        children.hidden = true;
        for (const child of node.children) {
            children.append(renderMerkleNode(child, detailsPanel, depth + 1));
        }
        toggle.addEventListener("click", () => {
            children.hidden = !children.hidden;
            toggle.textContent = children.hidden ? "+" : "−";
            toggle.setAttribute("aria-label", children.hidden ? "Expand node" : "Collapse node");
        });
        item.append(children);
    }

    return item;
}

function appendMerkleTree(record) {
    const section = document.createElement("section");
    section.className = "merkle-tree-panel";

    const openButton = document.createElement("button");
    openButton.type = "button";
    openButton.className = "merkle-open-button";
    openButton.textContent = "Open Merkle Tree";
    section.append(openButton);

    const dialog = document.createElement("dialog");
    dialog.className = "merkle-tree-dialog";
    const dialogShell = document.createElement("div");
    dialogShell.className = "merkle-dialog-shell";

    const dialogHeader = document.createElement("header");
    dialogHeader.className = "merkle-dialog-header";
    const dialogTitle = document.createElement("div");
    const dialogHeading = document.createElement("h2");
    dialogHeading.textContent = `Block ${record.blockID} · Merkle Tree`;
    const dialogRoot = document.createElement("p");
    dialogRoot.textContent = `${record.merkleTree?.leafCount || 0} leaves · Select a node to inspect its details.`;
    dialogTitle.append(dialogHeading, dialogRoot);
    const closeButton = document.createElement("button");
    closeButton.type = "button";
    closeButton.className = "merkle-dialog-close";
    closeButton.textContent = "Close";
    closeButton.addEventListener("click", () => dialog.close());
    dialogHeader.append(dialogTitle, closeButton);
    dialogShell.append(dialogHeader);

    const dialogContent = document.createElement("div");
    dialogContent.className = "merkle-dialog-content";
    const treeViewport = document.createElement("div");
    treeViewport.className = "merkle-tree-viewport";
    const detailsPanel = document.createElement("aside");
    detailsPanel.className = "merkle-node-details";
    const legend = document.createElement("p");
    legend.className = "merkle-tree-legend";
    legend.textContent = "Green: verified · Red: failed · Yellow: duplicated hash";
    treeViewport.append(legend);
    dialogContent.append(treeViewport, detailsPanel);

    if (record.merkleTree?.root) {
        treeViewport.append(renderMerkleNode(record.merkleTree.root, detailsPanel));
        const hint = document.createElement("p");
        hint.className = "merkle-node-details-hint";
        hint.textContent = "Select a node to view its details.";
        detailsPanel.append(hint);
    } else {
        const empty = document.createElement("p");
        empty.className = "status pending";
        empty.textContent = "No Merkle tree structure is available for this block.";
        treeViewport.append(empty);
    }
    dialogShell.append(dialogContent);
    dialog.append(dialogShell);
    section.append(dialog);

    openButton.addEventListener("click", () => {
        if (!dialog.open) dialog.showModal();
    });
    dialog.addEventListener("click", (event) => {
        if (event.target === dialog) dialog.close();
    });

    return section;
}

function createBlockDetailsDialog(record) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "chain-node-details-button";
    button.textContent = "Open block details";

    const dialog = document.createElement("dialog");
    dialog.className = "block-details-dialog";
    const shell = document.createElement("div");
    shell.className = "block-details-dialog-shell";

    const header = document.createElement("header");
    header.className = "block-details-dialog-header";
    const heading = document.createElement("div");
    const title = document.createElement("h2");
    title.textContent = `Block ${record.blockID} · ${record.stage || "Route stage"}`;
    const root = document.createElement("p");
    root.textContent = `Merkle root: ${record.rootHash || "Unavailable"}`;
    heading.append(title, root);
    const close = document.createElement("button");
    close.type = "button";
    close.className = "block-details-dialog-close";
    close.textContent = "Close";
    header.append(heading, close);

    const body = document.createElement("div");
    body.className = "block-details-dialog-body";
    appendNodeLine(body, "Harvest Date", record.batchHarvestDate);
    appendNodeLine(body, "Farm Location", record.batchFarmLocation);
    appendNodeLine(body, "Location Summary", record.locationSummary);

    const eventData = document.createElement("pre");
    eventData.className = "event-data";
    eventData.textContent = JSON.stringify(record.eventData || {}, null, 2);
    body.append(eventData);

    const cidText = record.ipfsRefs?.length
        ? record.ipfsRefs.map((reference) => `${reference.category}: ${reference.cid}`).join("\n")
        : "None";
    appendNodeLine(body, "CID References", cidText);
    appendNodeLine(body, "Parent Block", record.parentBlockId >= 0
        ? `Block ${record.parentBlockId}`
        : "Genesis");
    appendNodeLine(body, "Parent Hash", shortHash(record.parentBlockHash || "GENESIS"));

    shell.append(header, body);
    dialog.append(shell);
    button.addEventListener("click", () => {
        if (!dialog.open) dialog.showModal();
    });
    close.addEventListener("click", () => dialog.close());
    dialog.addEventListener("click", (event) => {
        if (event.target === dialog) dialog.close();
    });

    return { button, dialog };
}

function chainRouteLabel(record) {
    const role = record.role || record.routeNodeRole || "";
    const routeLabel = String(record.routeNodeLabel || "").trim();
    const roleLabel = workflowRoleLabel(role);

    if (role === "logistics") {
        const sequence = routeLabel.match(/(\d+)$/)?.[1];
        return sequence ? `${roleLabel} ${sequence}` : roleLabel;
    }

    return routeLabel || roleLabel || record.stage || "Route stage";
}

function renderChainNode(record) {
    const node = document.createElement("div");
    node.className = "chain-node";

    const header = document.createElement("div");
    header.className = "chain-node-header";
    const title = document.createElement("strong");
    title.textContent = `Block ${record.blockID} · ${chainRouteLabel(record)}`;
    const badge = document.createElement("span");
    badge.className = record.verified ? "badge verified" : "badge failed";
    badge.textContent = record.verified ? "Verified" : "Failed";
    header.append(title, badge);

    node.append(header);

    const summary = document.createElement("div");
    summary.className = "chain-node-summary";
    appendNodeLine(summary, "Role", record.role);
    appendNodeLine(summary, "Organization", record.organizationId);
    appendNodeLine(summary, "Stage", record.stage);
    appendNodeLine(summary, "Product", record.product);
    node.append(summary);

    const details = createBlockDetailsDialog(record);
    node.append(details.button, details.dialog, appendMerkleTree(record));
    return node;
}

function renderPendingChainNode(record) {
    const node = document.createElement("div");
    node.className = "chain-node pending-chain-node";

    const header = document.createElement("div");
    header.className = "chain-node-header";
    const title = document.createElement("strong");
    title.textContent = `Pending · ${record.routeNodeLabel || record.stage || "Route stage"}`;
    const badge = document.createElement("span");
    badge.className = "badge pending";
    badge.textContent = "Pending";
    header.append(title, badge);

    const summary = document.createElement("div");
    summary.className = "chain-node-summary";
    appendNodeLine(summary, "Role", record.role || record.routeNodeRole);
    appendNodeLine(summary, "Account", record.routeNodeUsername);
    appendNodeLine(summary, "Stage", record.stage || record.routeNodeLabel);
    appendNodeLine(summary, "Product", record.product);

    const status = document.createElement("p");
    status.className = "chain-node-pending-status";
    status.textContent = "No block yet. Submit this stage's participant data to create it.";

    node.append(header, summary, status);
    return node;
}

function chainPreviewNodeLabel(node) {
    if (node.pending) return node.routeNodeLabel || node.stage || "Pending stage";
    return `Block ${node.blockID}`;
}

function chainPreviewOrderValue(node, fallback) {
    const previewOrder = Number(node.previewOrder);
    if (Number.isFinite(previewOrder)) return previewOrder;
    const blockID = Number(node.blockID);
    return Number.isFinite(blockID) ? blockID : fallback;
}

function renderChain(batchId, nodes, edges, options = {}) {
    const card = document.createElement("article");
    card.className = "record-card chain-card";
    const routeIncomplete = options.routeIncomplete === true;

    const header = document.createElement("header");
    const title = document.createElement("h2");
    title.textContent = `Batch ${batchId}`;
    const badge = document.createElement("span");
    const completed = !routeIncomplete && nodes.length > 0 && nodes.every((node) =>
        !node.pending && node.verified === true && node.signatureVerified === true);
    const hasPending = nodes.some((node) => node.pending);
    badge.className = completed ? "badge verified" : "badge pending";
    badge.textContent = completed ? "Completed" : hasPending ? "Pending route changes" : "In Progress";
    header.append(title, badge);

    const chainLabel = document.createElement("p");
    chainLabel.className = "chain-structure-label";
    chainLabel.textContent = "Linked Block Chain · each block contains an independent Merkle Tree";

    const flow = document.createElement("div");
    flow.className = "chain-flow";
    const sortedNodes = [...nodes].sort((left, right) =>
        chainPreviewOrderValue(left, 0) - chainPreviewOrderValue(right, 0));
    const hasDisconnectedLink = sortedNodes.some((node, index) => {
        if (index === 0) return false;
        const previous = sortedNodes[index - 1];
        return !edges.some((edge) =>
            edge.from === previous.previewKey && edge.to === node.previewKey
        );
    });
    const hasUnresolvedRoute = routeIncomplete || nodes.some((node) => node.pending) ||
        hasDisconnectedLink;
    sortedNodes.forEach((node, index) => {
        if (index > 0) {
            const previous = sortedNodes[index - 1];
            const connected = edges.some((edge) =>
                edge.from === previous.previewKey && edge.to === node.previewKey);
            const arrow = document.createElement("span");
            arrow.className = connected ? "chain-arrow" : "chain-arrow disconnected";
            arrow.textContent = connected ? "→" : "·";
            arrow.title = connected
                ? `${chainPreviewNodeLabel(previous)} → ${chainPreviewNodeLabel(node)}`
                : "These stages are not connected yet";
            flow.append(arrow);
        }
        flow.append(node.pending ? renderPendingChainNode(node) : renderChainNode(node));
    });

    const links = document.createElement("p");
    links.className = "chain-links";
    if (edges.length > 0) {
        const descriptions = edges.map((edge) => {
            const from = sortedNodes.find((node) => node.previewKey === edge.from);
            const to = sortedNodes.find((node) => node.previewKey === edge.to);
            return from && to
                ? `${chainPreviewNodeLabel(from)} → ${chainPreviewNodeLabel(to)}`
                : "Unknown route connection";
        });
        links.textContent = `Connections: ${descriptions.join(" · ")}`;
    } else {
        links.textContent = hasPending ? "No route connections yet." : "Genesis block";
    }

    card.append(header, chainLabel, flow, links);
    if (hasUnresolvedRoute && completed) {
        badge.className = "badge pending";
        badge.textContent = "In Progress";
    }
    return card;
}

function findWorkflowRecord(routeNode, records, usedRecords, routeId = "") {
    const isUsableRecord = (record) =>
        record.verified === true && record.signatureVerified === true &&
        record.role === routeNode.role &&
        record.routeNodeUsername === routeNode.username;
    const exact = records.find((record) =>
        !usedRecords.has(record) &&
        isUsableRecord(record) &&
        record.routeId === routeId &&
        record.routeNodeId === routeNode.id);
    if (exact) return exact;

    const stableMatches = records.filter((record) =>
        !usedRecords.has(record) &&
        isUsableRecord(record) &&
        record.routeNodeId === routeNode.id
    );
    if (stableMatches.length === 1) return stableMatches[0];

    const legacyMatches = records.filter((record) =>
        !usedRecords.has(record) &&
        isUsableRecord(record) &&
        !record.routeId &&
        !record.routeNodeId
    );
    return legacyMatches.length === 1 ? legacyMatches[0] : null;
}

function buildWorkflowChainPreview(batchId, records, graphEdges, activeBatchId = "") {
    if (!workflowData || activeBatchId !== batchId) {
        const formalNodes = records.map((record, index) => ({
            ...record,
            pending: false,
            previewKey: `block:${record.blockID}`,
            previewOrder: index
        }));
        const blockIds = new Set(records.map((record) => record.blockID));
        return {
            nodes: formalNodes,
            edges: graphEdges
                .filter((edge) => blockIds.has(edge.from) && blockIds.has(edge.to))
                .map((edge) => ({
                    from: `block:${edge.from}`,
                    to: `block:${edge.to}`
                })),
            routeIncomplete: false
        };
    }

    const orderedRouteNodes = workflowPreviewRouteOrder(workflowData);
    const hasCompleteRoutePath = orderedRouteNodes.length > 0;
    const routeNodesForPreview = hasCompleteRoutePath
        ? orderedRouteNodes
        : routeOrder(workflowData);
    const usedRecords = new Set();
    const previewNodes = [];
    const previewKeys = new Map();
    const recordsByRouteNode = new Map();
    const product = records.find((record) => record.product)?.product || "";
    for (const routeNode of routeNodesForPreview) {
        const record = findWorkflowRecord(
            routeNode,
            records,
            usedRecords,
            workflowData.routeId || ""
        );
        if (!record && !hasCompleteRoutePath) continue;
        const preview = record
            ? {
                  ...record,
                  pending: false,
                  previewKey: `block:${record.blockID}`,
                  previewOrder: previewNodes.length
              }
            : {
                  batchId,
                  product,
                  pending: true,
                  blockID: null,
                  previewKey: `route-node:${routeNode.id}`,
                  previewOrder: previewNodes.length,
                  routeId: workflowData.routeId || "",
                  routeNodeId: routeNode.id,
                  routeStepIndex: routeNode.stepIndex,
                  routeNodeLabel: routeNode.label,
                  routeNodeRole: routeNode.role,
                  routeNodeUsername: routeNode.username,
                  role: routeNode.role,
                  stage: routeNode.role,
                  chainStatus: "pending"
              };
        if (record) {
            usedRecords.add(record);
            recordsByRouteNode.set(routeNode.id, record);
        }
        previewNodes.push(preview);
        previewKeys.set(routeNode.id, preview.previewKey);
    }

    const workflowEdges = (workflowData.edges || [])
        .map((edge) => {
            const fromRecord = recordsByRouteNode.get(edge.from);
            const toRecord = recordsByRouteNode.get(edge.to);
            if (!fromRecord || !toRecord) return null;
            return {
                from: previewKeys.get(edge.from),
                to: previewKeys.get(edge.to)
            };
        })
        .filter(Boolean);
    const routeValidation = workflowValidation(workflowData);
    const missingRouteRecord = !hasCompleteRoutePath ||
        orderedRouteNodes.some((routeNode) =>
            !recordsByRouteNode.has(routeNode.id));

    return {
        nodes: previewNodes,
        edges: workflowEdges,
        routeIncomplete: !routeValidation.valid || missingRouteRecord
    };
}

function activeWorkflowPreviewBatchId(graph) {
    if (!workflowData) return "";
    if (workflowBatchId) return workflowBatchId;

    const matchingBatches = new Set(
        graph.nodes
            .filter((record) => record.routeId && record.routeId === workflowData.routeId)
            .map((record) => record.batchId)
            .filter(Boolean)
    );
    return matchingBatches.size === 1 ? [...matchingBatches][0] : "";
}

function renderChainGraph(graph) {
    const chains = new Map();
    for (const node of graph.nodes) {
        if (!chains.has(node.batchId)) chains.set(node.batchId, []);
        chains.get(node.batchId).push(node);
    }
    if (workflowData && workflowBatchId && !chains.has(workflowBatchId)) {
        chains.set(workflowBatchId, []);
    }

    const activeBatchId = activeWorkflowPreviewBatchId(graph);
    return [...chains.entries()].map(([batchId, records]) => {
        const preview = buildWorkflowChainPreview(
            batchId,
            records,
            graph.edges,
            activeBatchId
        );
        return renderChain(batchId, preview.nodes, preview.edges, {
            routeIncomplete: preview.routeIncomplete
        });
    });
}

function refreshChainPreview() {
    if (!chainGraphLoaded || !list) return;
    list.replaceChildren(...renderChainGraph(chainGraph));
}

function routeOrder(workflow) {
    const nodes = workflow.nodes || [];
    const byId = new Map(nodes.map((node) => [node.id, node]));
    const outgoing = new Map();
    for (const edge of workflow.edges || []) {
        if (!outgoing.has(edge.from)) outgoing.set(edge.from, []);
        outgoing.get(edge.from).push(edge.to);
    }

    const supplier = nodes.find((node) => node.role === "supplier");
    const ordered = [];
    const visited = new Set();
    let current = supplier;
    while (current && !visited.has(current.id)) {
        ordered.push(current);
        visited.add(current.id);
        const nextId = (outgoing.get(current.id) || [])[0];
        current = nextId ? byId.get(nextId) : null;
    }
    for (const node of nodes) {
        if (!visited.has(node.id)) ordered.push(node);
    }
    return ordered;
}

function workflowPreviewRouteOrder(workflow) {
    const nodes = workflow.nodes || [];
    const ordered = routeOrder(workflow);
    const byId = new Map(nodes.map((node) => [node.id, node]));
    const outgoing = new Map();
    for (const edge of workflow.edges || []) {
        if (!outgoing.has(edge.from)) outgoing.set(edge.from, []);
        outgoing.get(edge.from).push(edge.to);
    }

    const reachable = new Set();
    const supplier = nodes.find((node) => node.role === "supplier");
    const supermarket = nodes.find((node) => node.role === "supermarket");
    if (!supplier || !supermarket) return [];
    let current = supplier;
    while (current && !reachable.has(current.id)) {
        reachable.add(current.id);
        if (current.id === supermarket.id) break;
        const nextId = (outgoing.get(current.id) || [])[0];
        current = nextId ? byId.get(nextId) : null;
    }

    if (!reachable.has(supermarket.id)) return [];
    return ordered.filter((node) => reachable.has(node.id));
}

function workflowInteger(value, fallback = 0) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? Math.round(numeric) : fallback;
}

function normalizeWorkflowPositions(workflow = workflowData) {
    if (!workflow) return;
    for (const node of workflow.nodes || []) {
        node.x = workflowInteger(node.x);
        node.y = workflowInteger(node.y);
    }
}

function normalizeWorkflow(workflow) {
    return {
        routeId: workflow.routeId || "",
        accounts: (workflow.accounts || []).map((account) => ({ ...account })),
        nodes: (workflow.nodes || []).map((node) => ({
            id: node.id,
            nodeType: node.nodeType || "transport",
            label: node.label || node.id,
            role: node.role || "logistics",
            username: node.username || "",
            x: workflowInteger(node.x),
            y: workflowInteger(node.y),
            stepIndex: workflowInteger(node.stepIndex, -1)
        })),
        edges: (workflow.edges || []).map((edge) => ({
            from: edge.from,
            to: edge.to
        }))
    };
}

function workflowSnapshot(workflow = workflowData) {
    if (!workflow) return "";
    return JSON.stringify({
        routeId: workflow.routeId || "",
        accounts: (workflow.accounts || []).map((account) => ({ ...account })),
        nodes: (workflow.nodes || []).map((node) => ({ ...node })),
        edges: (workflow.edges || []).map((edge) => ({ ...edge }))
    });
}

function updateWorkflowHistoryControls() {
    workflowUndo.disabled = workflowHistory.past.length === 0;
    workflowRedo.disabled = workflowHistory.future.length === 0;
}

function resetWorkflowHistory() {
    workflowHistory = { past: [], future: [] };
    updateWorkflowHistoryControls();
}

function captureWorkflowHistory() {
    const snapshot = workflowSnapshot();
    if (!snapshot) return;
    workflowHistory.past.push(snapshot);
    workflowHistory.past = workflowHistory.past.slice(-40);
    workflowHistory.future = [];
    updateWorkflowHistoryControls();
}

function captureWorkflowDragHistory(snapshot) {
    if (!snapshot || snapshot === workflowSnapshot()) return;
    workflowHistory.past.push(snapshot);
    workflowHistory.past = workflowHistory.past.slice(-40);
    workflowHistory.future = [];
    updateWorkflowHistoryControls();
}

function restoreWorkflowSnapshot(snapshot) {
    if (!snapshot) return;
    workflowData = normalizeWorkflow(JSON.parse(snapshot));
    workflowLayoutInitialized = true;
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    renderWorkflow(workflowData);
    markWorkflowDraftChanged("Workflow history changed. Synchronizing the route draft...");
}

function undoWorkflowEdit() {
    if (!workflowData || workflowHistory.past.length === 0) return;
    const current = workflowSnapshot();
    const previous = workflowHistory.past.pop();
    workflowHistory.future.push(current);
    restoreWorkflowSnapshot(previous);
    updateWorkflowHistoryControls();
}

function redoWorkflowEdit() {
    if (!workflowData || workflowHistory.future.length === 0) return;
    const current = workflowSnapshot();
    const next = workflowHistory.future.pop();
    workflowHistory.past.push(current);
    restoreWorkflowSnapshot(next);
    updateWorkflowHistoryControls();
}

function workflowValidation(workflow) {
    const nodes = workflow.nodes || [];
    const edges = workflow.edges || [];
    if (nodes.length < 2) {
        return { valid: false, error: "A route needs at least a Supplier and a Supermarket." };
    }

    const byId = new Map();
    const assignedAccounts = new Set();
    let supplier = null;
    let supermarket = null;
    for (const node of nodes) {
        if (!node.id || !node.label || byId.has(node.id)) {
            return { valid: false, error: "Route node IDs and labels must be unique and non-empty." };
        }
        if (!["supplier", "logistics", "warehouse", "supermarket"].includes(node.role)) {
            return { valid: false, error: "The route contains an unsupported node role." };
        }
        if (!node.username) {
            return { valid: false, error: `${node.label} needs an assigned account.` };
        }
        const account = (workflow.accounts || []).find((candidate) =>
            candidate.active !== false &&
            candidate.username === node.username &&
            candidate.role === node.role
        );
        if ((workflow.accounts || []).length > 0 && !account) {
            return {
                valid: false,
                error: `${node.username} cannot operate the ${node.label} stage.`
            };
        }
        if (assignedAccounts.has(node.username)) {
            return {
                valid: false,
                error: `${node.username} is already assigned to another route stage.`
            };
        }
        assignedAccounts.add(node.username);
        if (node.role === "supplier") supplier = supplier ? null : node;
        if (node.role === "supermarket") supermarket = supermarket ? null : node;
        byId.set(node.id, node);
    }
    if (!supplier || !supermarket ||
        nodes.filter((node) => node.role === "supplier").length !== 1 ||
        nodes.filter((node) => node.role === "supermarket").length !== 1) {
        return { valid: false, error: "A route must have exactly one Supplier and one Supermarket." };
    }

    const incoming = new Map();
    const outgoing = new Map();
    const edgeKeys = new Set();
    for (const edge of edges) {
        const key = `${edge.from}\u0000${edge.to}`;
        if (!byId.has(edge.from) || !byId.has(edge.to) || edge.from === edge.to) {
            return { valid: false, error: "Every connection must join two different route nodes." };
        }
        if (edgeKeys.has(key)) {
            return { valid: false, error: "Duplicate connections are not allowed." };
        }
        edgeKeys.add(key);
        incoming.set(edge.to, (incoming.get(edge.to) || 0) + 1);
        outgoing.set(edge.from, (outgoing.get(edge.from) || 0) + 1);
    }
    if ((incoming.get(supplier.id) || 0) !== 0 ||
        (outgoing.get(supermarket.id) || 0) !== 0) {
        return { valid: false, error: "Supplier must start the route and Supermarket must end it." };
    }

    const nextById = new Map();
    for (const edge of edges) {
        if (nextById.has(edge.from)) {
            return { valid: false, error: "Each route node can have only one outgoing connection." };
        }
        nextById.set(edge.from, edge.to);
    }
    const visited = new Set();
    let current = supplier.id;
    while (true) {
        if (visited.has(current)) {
            return { valid: false, error: "A route cannot contain a cycle." };
        }
        visited.add(current);
        if (current === supermarket.id) break;
        const next = nextById.get(current);
        if (!next) {
            return { valid: false, error: "Every route node must lead to the Supermarket." };
        }
        current = next;
    }
    if (visited.size !== nodes.length) {
        return { valid: false, error: "Every route node must be connected to the same route." };
    }
    return { valid: true, error: "" };
}

const WORKFLOW_NODE_WIDTH = 184;
const WORKFLOW_NODE_HEIGHT = 108;
const WORKFLOW_MARGIN = 72;
const WORKFLOW_NODE_GAP = 88;
const WORKFLOW_MIN_ZOOM = 0.45;
const WORKFLOW_MAX_ZOOM = 1.25;
const WORKFLOW_ZOOM_BUTTON_STEP = 0.05;

function workflowCanvasLayout(workflow, force = false) {
    const ordered = routeOrder(workflow);
    const hasSavedLayout = workflowLayoutInitialized || ordered.some((node) =>
        node.x !== 0 || node.y !== 0
    );

    if (force || !hasSavedLayout) {
        const sceneHeight = 320;
        ordered.forEach((node, index) => {
            node.x = WORKFLOW_MARGIN + index * (WORKFLOW_NODE_WIDTH + WORKFLOW_NODE_GAP);
            node.y = Math.round((sceneHeight - WORKFLOW_NODE_HEIGHT) / 2);
            node.stepIndex = index;
        });
        workflowLayoutInitialized = true;
    } else {
        ordered.forEach((node, index) => {
            node.stepIndex = index;
        });
    }

    const contentMinX = ordered.length === 0
        ? 0
        : Math.min(...ordered.map((node) => node.x));
    const contentMinY = ordered.length === 0
        ? 0
        : Math.min(...ordered.map((node) => node.y));
    const contentMaxX = ordered.length === 0
        ? WORKFLOW_NODE_WIDTH
        : Math.max(...ordered.map((node) => node.x + WORKFLOW_NODE_WIDTH));
    const contentMaxY = ordered.length === 0
        ? WORKFLOW_NODE_HEIGHT
        : Math.max(...ordered.map((node) => node.y + WORKFLOW_NODE_HEIGHT));
    const minX = Math.min(0, contentMinX) - WORKFLOW_MARGIN;
    const minY = Math.min(0, contentMinY) - WORKFLOW_MARGIN;
    const maxX = contentMaxX + WORKFLOW_MARGIN;
    const maxY = contentMaxY + WORKFLOW_MARGIN;
    const bounds = {
        minX,
        minY,
        width: Math.max(900, maxX - minX),
        height: Math.max(320, maxY - minY)
    };
    return {
        positions: ordered.map((node) => ({
            node,
            x: node.x - bounds.minX,
            y: node.y - bounds.minY,
            width: WORKFLOW_NODE_WIDTH,
            height: WORKFLOW_NODE_HEIGHT
        })),
        bounds
    };
}

function workflowPoint(event) {
    const rect = workflowCanvas.getBoundingClientRect();
    const sceneX = (event.clientX - rect.left - workflowViewport.x) /
        workflowViewport.zoom;
    const sceneY = (event.clientY - rect.top - workflowViewport.y) /
        workflowViewport.zoom;
    return {
        x: sceneX + workflowSceneBounds.minX,
        y: sceneY + workflowSceneBounds.minY
    };
}

function workflowNodePositionAvailable(x, y, padding = 16) {
    return !(workflowData?.nodes || []).some((node) =>
        x < node.x + WORKFLOW_NODE_WIDTH + padding &&
        x + WORKFLOW_NODE_WIDTH + padding > node.x &&
        y < node.y + WORKFLOW_NODE_HEIGHT + padding &&
        y + WORKFLOW_NODE_HEIGHT + padding > node.y
    );
}

function workflowNewNodePosition() {
    const centerX = (workflowCanvas.clientWidth / 2 - workflowViewport.x) /
        workflowViewport.zoom + workflowSceneBounds.minX;
    const centerY = (workflowCanvas.clientHeight / 2 - workflowViewport.y) /
        workflowViewport.zoom + workflowSceneBounds.minY;
    const stepX = WORKFLOW_NODE_WIDTH + 28;
    const stepY = WORKFLOW_NODE_HEIGHT + 28;
    const candidates = [[0, 0]];

    for (let radius = 1; radius <= 4; radius++) {
        candidates.push(
            [radius, 0],
            [-radius, 0],
            [0, radius],
            [0, -radius],
            [radius, radius],
            [-radius, radius],
            [radius, -radius],
            [-radius, -radius]
        );
    }

    for (const [column, row] of candidates) {
        const x = Math.round(
            centerX - WORKFLOW_NODE_WIDTH / 2 + column * stepX
        );
        const y = Math.round(
            centerY - WORKFLOW_NODE_HEIGHT / 2 + row * stepY
        );
        if (workflowNodePositionAvailable(x, y)) return { x, y };
    }

    return {
        x: Math.round(centerX - WORKFLOW_NODE_WIDTH / 2),
        y: Math.round(centerY - WORKFLOW_NODE_HEIGHT / 2)
    };
}

function workflowSvgElement(tag, attributes = {}) {
    const element = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [name, value] of Object.entries(attributes)) {
        element.setAttribute(name, value);
    }
    return element;
}

function workflowEdgePath(from, to) {
    const startX = from.x + from.width;
    const startY = from.y + from.height / 2;
    const endX = to.x;
    const endY = to.y + to.height / 2;
    const curve = Math.max(46, Math.abs(endX - startX) * 0.45);
    return `M ${startX} ${startY} C ${startX + curve} ${startY}, ${endX - curve} ${endY}, ${endX} ${endY}`;
}

function workflowEdgePoint(from, to, progress = 0.5) {
    const startX = from.x + from.width;
    const startY = from.y + from.height / 2;
    const endX = to.x;
    const endY = to.y + to.height / 2;
    const curve = Math.max(46, Math.abs(endX - startX) * 0.45);
    const control1 = { x: startX + curve, y: startY };
    const control2 = { x: endX - curve, y: endY };
    const inverse = 1 - progress;
    return {
        x: inverse ** 3 * startX +
            3 * inverse ** 2 * progress * control1.x +
            3 * inverse * progress ** 2 * control2.x +
            progress ** 3 * endX,
        y: inverse ** 3 * startY +
            3 * inverse ** 2 * progress * control1.y +
            3 * inverse * progress ** 2 * control2.y +
            progress ** 3 * endY
    };
}

function workflowPositionMap() {
    return new Map((workflowData?.nodes || []).map((node) => [node.id, {
        node,
        x: node.x,
        y: node.y,
        width: WORKFLOW_NODE_WIDTH,
        height: WORKFLOW_NODE_HEIGHT
    }]));
}

function workflowRoleLabel(role) {
    return String(role || "route").replace(/(^|[-_])([a-z])/g, (_, prefix, letter) =>
        prefix + letter.toUpperCase()
    );
}

function workflowRoleForType(type) {
    return type === "warehouse" ? "warehouse" : "logistics";
}

function workflowAccountsForRole(role) {
    return (workflowData?.accounts || [])
        .filter((account) => account.active !== false && account.role === role)
        .sort((left, right) => left.username.localeCompare(right.username));
}

function availableWorkflowAccountsForRole(role, includeUsername = "") {
    const used = new Set((workflowData?.nodes || [])
        .filter((node) => node.role === role && node.username !== includeUsername)
        .map((node) => node.username));
    return workflowAccountsForRole(role).filter((account) =>
        account.username === includeUsername || !used.has(account.username)
    );
}

function nextWorkflowAccount(role) {
    const used = new Set((workflowData?.nodes || [])
        .filter((node) => node.role === role)
        .map((node) => node.username));
    return workflowAccountsForRole(role).find((account) =>
        !used.has(account.username)
    )?.username || "";
}

function nextWorkflowNodeSequence(type) {
    const labelPrefix = type === "warehouse" ? "Warehouse" : "Transport";
    let maximum = 0;
    for (const node of workflowData?.nodes || []) {
        if (node.nodeType !== type) continue;
        const match = String(node.label || "").match(
            new RegExp(`^${labelPrefix}\\s+(\\d+)$`, "i")
        );
        if (match) maximum = Math.max(maximum, Number(match[1]));
    }
    return maximum + 1;
}

function createWorkflowNodeId(prefix) {
    const uniquePart = window.crypto?.randomUUID
        ? window.crypto.randomUUID()
        : `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
    return `${prefix}-${uniquePart}`;
}

function populateWorkflowNodeAccounts(role, preferred = "") {
    if (!workflowNodeAccount) return "";
    const accounts = availableWorkflowAccountsForRole(role, preferred);
    workflowNodeAccount.replaceChildren();
    for (const account of accounts) {
        const option = document.createElement("option");
        option.value = account.username;
        option.textContent = `${account.username} · ${account.displayName || account.organizationId}`;
        workflowNodeAccount.append(option);
    }
    const selected = accounts.some((account) => account.username === preferred)
        ? preferred
        : (nextWorkflowAccount(role) || accounts[0]?.username || "");
    workflowNodeAccount.value = selected;
    workflowNodeAccount.disabled = accounts.length === 0;
    return selected;
}

function syncWorkflowNodeAccountControl() {
    populateWorkflowNodeAccounts(workflowRoleForType(workflowNodeType.value));
}

function createWorkflowNodeElement(position) {
    const { node, x, y } = position;
    const element = document.createElement("article");
    element.className = "workflow-node";
    element.dataset.nodeId = node.id;
    element.style.left = `${x}px`;
    element.style.top = `${y}px`;
    element.setAttribute("aria-label", `${node.label}, ${workflowRoleLabel(node.role)}`);
    if (node.id === workflowSelectedNodeId) element.classList.add("selected");
    if (workflowPointerState?.mode === "node" &&
        workflowPointerState.nodeId === node.id) {
        element.classList.add("is-dragging");
    }

    const title = document.createElement("strong");
    title.className = "workflow-node-title";
    title.textContent = node.label;
    const role = document.createElement("span");
    role.className = "workflow-node-role";
    role.textContent = workflowRoleLabel(node.role);
    const username = document.createElement("span");
    username.className = "workflow-node-user";
    username.textContent = node.username || "Unassigned";
    const state = document.createElement("span");
    state.className = "workflow-node-state";
    state.setAttribute("aria-hidden", "true");

    const target = document.createElement("button");
    target.type = "button";
    target.className = "workflow-handle workflow-handle-target";
    target.dataset.handle = "target";
    target.dataset.nodeId = node.id;
    target.title = "Incoming connection";
    target.setAttribute("aria-label", `Connect to ${node.label}`);
    target.hidden = node.role === "supplier";
    if (workflowPointerState?.mode === "connect" &&
        workflowPointerState.targetId === node.id) {
        target.classList.add(
            workflowConnectionError(workflowPointerState.sourceId, node.id)
                ? "is-invalid-target"
                : "is-connect-target"
        );
    }

    const source = document.createElement("button");
    source.type = "button";
    source.className = "workflow-handle workflow-handle-source";
    source.dataset.handle = "source";
    source.dataset.nodeId = node.id;
    source.title = "Outgoing connection";
    source.setAttribute("aria-label", `Connect from ${node.label}`);
    source.hidden = node.role === "supermarket";

    element.append(title, role, username, state, target, source);
    element.addEventListener("pointerdown", (event) => {
        if (event.target.closest(".workflow-handle")) return;
        startWorkflowNodeDrag(event, node.id);
    });
    target.addEventListener("pointerdown", (event) => {
        event.preventDefault();
        event.stopPropagation();
    });
    source.addEventListener("pointerdown", (event) => {
        startWorkflowConnection(event, node.id);
    });
    return element;
}

function workflowConnectionError(fromId, toId) {
    const from = workflowData.nodes.find((node) => node.id === fromId);
    const to = workflowData.nodes.find((node) => node.id === toId);
    if (!from || !to) return "The selected nodes are unavailable.";
    if (from.role === "supermarket") return "Supermarket must remain the final node.";
    if (to.role === "supplier") return "Supplier must remain the first node.";
    if (fromId === toId) return "A node cannot connect to itself.";
    if (workflowData.edges.some((edge) => edge.from === fromId && edge.to === toId)) {
        return "That connection already exists.";
    }
    if (workflowData.edges.some((edge) => edge.from === fromId)) {
        return "Each route node can have only one outgoing connection.";
    }
    if (workflowData.edges.some((edge) => edge.to === toId)) {
        return "Each route node can have only one incoming connection.";
    }
    const candidate = {
        ...workflowData,
        edges: [...workflowData.edges, { from: fromId, to: toId }]
    };
    const validation = workflowValidation(candidate);
    if (!validation.valid && validation.error.includes("cycle")) return validation.error;
    return "";
}

function applyWorkflowViewport() {
    workflowScene.style.transform =
        `translate(${workflowViewport.x}px, ${workflowViewport.y}px) scale(${workflowViewport.zoom})`;
    workflowZoomLevel.textContent = `${Math.round(workflowViewport.zoom * 100)}%`;
}

function renderWorkflow(workflow, options = {}) {
    const isNewWorkflow = workflow !== workflowData;
    workflowData = isNewWorkflow ? normalizeWorkflow(workflow) : workflowData;
    if (isNewWorkflow) {
        workflowLayoutInitialized = false;
        resetWorkflowHistory();
    }
    const previousBounds = workflowSceneBounds;
    const layout = workflowCanvasLayout(workflowData, options.forceLayout === true);
    if (workflowViewportReady) {
        workflowViewport.x +=
            (layout.bounds.minX - previousBounds.minX) * workflowViewport.zoom;
        workflowViewport.y +=
            (layout.bounds.minY - previousBounds.minY) * workflowViewport.zoom;
    }
    workflowSceneBounds = layout.bounds;
    workflowScene.style.width = `${layout.bounds.width}px`;
    workflowScene.style.height = `${layout.bounds.height}px`;
    workflowEdgeLayer.setAttribute("width", layout.bounds.width);
    workflowEdgeLayer.setAttribute("height", layout.bounds.height);
    workflowEdgeLayer.setAttribute(
        "viewBox",
        `0 0 ${layout.bounds.width} ${layout.bounds.height}`
    );
    workflowEdgeLayer.replaceChildren();
    workflowNodeLayer.replaceChildren();

    const defs = workflowSvgElement("defs");
    const marker = workflowSvgElement("marker", {
        id: "workflow-arrow",
        markerWidth: "8",
        markerHeight: "8",
        refX: "7",
        refY: "4",
        orient: "auto",
        markerUnits: "strokeWidth"
    });
    marker.append(workflowSvgElement("path", {
        d: "M 0 0 L 8 4 L 0 8 z",
        fill: "#55d6b0"
    }));
    defs.append(marker);
    workflowEdgeLayer.append(defs);

    const positions = layout.positions;
    const positionById = new Map(
        positions.map((position) => [position.node.id, position])
    );
    for (const [index, edge] of workflowData.edges.entries()) {
        const from = positionById.get(edge.from);
        const to = positionById.get(edge.to);
        if (!from || !to) continue;
        const path = workflowSvgElement("path", {
            d: workflowEdgePath(from, to),
            class: "workflow-edge" +
                (index === workflowSelectedEdgeIndex ? " selected" : ""),
            "marker-end": "url(#workflow-arrow)"
        });
        path.dataset.edgeIndex = String(index);
        path.addEventListener("pointerdown", (event) => {
            event.preventDefault();
            event.stopPropagation();
            workflowSelectedNodeId = "";
            workflowSelectedEdgeIndex = index;
            renderWorkflow(workflowData);
        });
        workflowEdgeLayer.append(path);

        const remove = workflowSvgElement("g", {
            class: "workflow-edge-delete",
            role: "button",
            tabindex: "0",
            focusable: "true",
            "pointer-events": "all",
            "aria-label": `Remove connection from ${from.node.label} to ${to.node.label}`
        });
        const removePoint = workflowEdgePoint(from, to);
        remove.setAttribute("transform", `translate(${removePoint.x} ${removePoint.y})`);
        remove.dataset.edgeIndex = String(index);
        remove.append(
            workflowSvgElement("circle", { cx: "0", cy: "0", r: "11" }),
            workflowSvgElement("text", {
                x: "0",
                y: "0",
                "aria-hidden": "true"
            })
        );
        remove.querySelector("text").textContent = "×";
        const removeConnection = (event) => {
            event.preventDefault();
            event.stopPropagation();
            deleteWorkflowEdgeAt(index);
        };
        remove.addEventListener("pointerdown", (event) => {
            event.stopPropagation();
        });
        remove.addEventListener("click", removeConnection);
        remove.addEventListener("keydown", (event) => {
            if (event.key !== "Enter" && event.key !== " ") return;
            removeConnection(event);
        });
        workflowEdgeLayer.append(remove);
    }

    if (workflowPointerState?.mode === "connect") {
        const source = positionById.get(workflowPointerState.sourceId);
        if (source && workflowPointerState.currentPoint) {
            const previewTarget = {
                x: workflowPointerState.currentPoint.x - layout.bounds.minX,
                y: workflowPointerState.currentPoint.y - layout.bounds.minY -
                    WORKFLOW_NODE_HEIGHT / 2,
                width: 0,
                height: WORKFLOW_NODE_HEIGHT
            };
            workflowEdgeLayer.append(workflowSvgElement("path", {
                d: workflowEdgePath(source, previewTarget),
                class: "workflow-edge-preview"
            }));
        }
    }

    for (const position of positions) {
        workflowNodeLayer.append(createWorkflowNodeElement(position));
    }

    applyWorkflowViewport();
    if (!workflowViewportReady || options.fitView === true) {
        fitWorkflowView();
    }

    const validation = workflowValidation(workflowData);
    syncWorkflowNodeAccountControl();
    workflowRouteBadge.textContent = workflowData.routeId || "Route unavailable";
    workflowRouteBadge.className = "badge " + (validation.valid ? "verified" : "pending");
    workflowDeleteNode.disabled = !workflowSelectedNodeId;
    workflowDeleteEdge.disabled = workflowSelectedEdgeIndex < 0;
    workflowStatus.textContent = validation.valid
        ? `${workflowData.nodes.length} route node(s), ${workflowData.edges.length} connection(s). Drag nodes or use the handles to edit the route.`
        : `Route error: ${validation.error}`;
    workflowStatus.className = "status " + (validation.valid ? "success" : "error");
    refreshChainPreview();
}

function fitWorkflowView() {
    if (!workflowData) return;
    const width = workflowCanvas.clientWidth;
    const height = workflowCanvas.clientHeight;
    const bounds = workflowSceneBounds;
    const nodes = workflowData.nodes || [];
    if (!width || !height || !bounds.width || !bounds.height || !nodes.length) return;
    const padding = 36;
    const contentMinX = Math.min(...nodes.map((node) => node.x));
    const contentMinY = Math.min(...nodes.map((node) => node.y));
    const contentMaxX = Math.max(...nodes.map((node) =>
        node.x + WORKFLOW_NODE_WIDTH
    ));
    const contentMaxY = Math.max(...nodes.map((node) =>
        node.y + WORKFLOW_NODE_HEIGHT
    ));
    const sceneMinX = contentMinX - padding - bounds.minX;
    const sceneMinY = contentMinY - padding - bounds.minY;
    const sceneWidth = contentMaxX - contentMinX + padding * 2;
    const sceneHeight = contentMaxY - contentMinY + padding * 2;
    const zoom = Math.max(
        WORKFLOW_MIN_ZOOM,
        Math.min(
            WORKFLOW_MAX_ZOOM,
            (width - padding * 2) / sceneWidth,
            (height - padding * 2) / sceneHeight
        )
    );
    workflowViewport.zoom = zoom;
    workflowViewport.x = (width - sceneWidth * zoom) / 2 - sceneMinX * zoom;
    workflowViewport.y = (height - sceneHeight * zoom) / 2 - sceneMinY * zoom;
    workflowViewportReady = true;
    applyWorkflowViewport();
}

function setWorkflowZoom(nextZoom) {
    if (!workflowData) return;
    const zoom = Math.max(WORKFLOW_MIN_ZOOM, Math.min(WORKFLOW_MAX_ZOOM, nextZoom));
    const centerX = workflowCanvas.clientWidth / 2;
    const centerY = workflowCanvas.clientHeight / 2;
    const sceneCenterX = (centerX - workflowViewport.x) / workflowViewport.zoom;
    const sceneCenterY = (centerY - workflowViewport.y) / workflowViewport.zoom;
    workflowViewport.zoom = zoom;
    workflowViewport.x = centerX - sceneCenterX * zoom;
    workflowViewport.y = centerY - sceneCenterY * zoom;
    workflowViewportReady = true;
    applyWorkflowViewport();
}

function recalculateWorkflowLayout(force = true) {
    if (!workflowData) return;
    workflowCanvasLayout(workflowData, force);
    for (const [index, node] of routeOrder(workflowData).entries()) {
        node.stepIndex = index;
    }
}

function workflowNodePayload(node) {
    return [
        node.id,
        node.nodeType,
        node.label,
        node.role,
        node.username,
        workflowInteger(node.x),
        workflowInteger(node.y),
        node.stepIndex
    ].map((value) => encodeURIComponent(String(value ?? ""))).join("|");
}

function workflowRequestBody(
    workflow = workflowData,
    batchId = workflowBatchId,
    draft = false
) {
    const fields = {
        batchId,
        nodes: (workflow?.nodes || []).map(workflowNodePayload).join(";"),
        edges: (workflow?.edges || []).map((edge) =>
            encodeURIComponent(edge.from) + "|" + encodeURIComponent(edge.to)
        ).join(";")
    };
    if (draft) fields.draft = "true";
    return new URLSearchParams(fields);
}

function invalidateSnapshotForRouteChange(
    message = "Route changed. The previous Snapshot is no longer valid."
) {
    renderSnapshotBatches([]);
    setSnapshotStatus(message, "pending");
}

function markWorkflowDraftChanged(
    message = "Route changed. Synchronizing the new route draft..."
) {
    if (!workflowData) return;
    workflowRouteDirty = true;
    invalidateSnapshotForRouteChange(
        "Route changed. The previous Snapshot is no longer valid."
    );
    workflowStatus.textContent = message;
    workflowStatus.className = "status pending";
    scheduleWorkflowDraftSync();
}

function scheduleWorkflowDraftSync() {
    if (!session || !workflowData) return;
    if (workflowAutoSaveInFlight) {
        workflowAutoSaveQueued = true;
        return;
    }
    if (workflowAutoSaveTimer) window.clearTimeout(workflowAutoSaveTimer);
    workflowAutoSaveTimer = window.setTimeout(() => {
        workflowAutoSaveTimer = 0;
        syncWorkflowDraft();
    }, 250);
}

async function syncWorkflowDraft() {
    if (!session || !workflowData) return;
    if (workflowAutoSaveInFlight) {
        workflowAutoSaveQueued = true;
        return;
    }

    recalculateWorkflowLayout(false);
    normalizeWorkflowPositions(workflowData);
    const sentSnapshot = workflowSnapshot();
    const sentBatchId = workflowBatchId;
    workflowAutoSaveInFlight = true;
    workflowAutoSaveQueued = false;
    workflowStatus.textContent = "Synchronizing route draft...";
    workflowStatus.className = "status pending";
    let routeConflictReloaded = false;
    let synchronizedSnapshot = sentSnapshot;
    try {
        const response = await fetch("/api/workflow", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: "Bearer " + session.token
            },
            body: workflowRequestBody(workflowData, sentBatchId, true).toString()
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            const error = new Error(result.error || "Unable to synchronize route draft.");
            error.routeConflict = response.status === 409;
            throw error;
        }

        if (workflowBatchId === sentBatchId && workflowSnapshot() === sentSnapshot) {
            workflowData.routeId = result.routeId || workflowData.routeId;
            workflowRouteBadge.textContent = workflowData.routeId || "Route unavailable";
            synchronizedSnapshot = workflowSnapshot();
            workflowRouteDirty = false;
            await Promise.all([
                loadWorkflowView(sentBatchId),
                loadSnapshotCandidates()
            ]);
            // The local save response and these three reads already include
            // every event emitted by this draft synchronization.
            liveRefreshQueued = false;
            liveRefreshQueuedType = "";
            workflowStatus.textContent = "Route draft synchronized automatically.";
            workflowStatus.className = "status success";
        }
    } catch (error) {
        const message = error.message;
        const canRestore = error.routeConflict === true &&
            session && workflowBatchId === sentBatchId &&
            workflowSnapshot() === sentSnapshot;
        if (canRestore) {
            const restored = await loadWorkflowView(
                sentBatchId, { discardLocalDraft: true });
            if (restored) {
                routeConflictReloaded = true;
                workflowStatus.textContent = message;
                workflowStatus.className = "status error";
            }
        }
        if (!routeConflictReloaded) {
            workflowStatus.textContent = message;
            workflowStatus.className = "status error";
            window.setTimeout(() => {
                if (session && workflowRouteDirty) scheduleWorkflowDraftSync();
            }, 1500);
        }
    } finally {
        workflowAutoSaveInFlight = false;
        if (workflowBatchId === sentBatchId &&
            !routeConflictReloaded &&
            (workflowAutoSaveQueued || workflowSnapshot() !== synchronizedSnapshot)) {
            workflowAutoSaveQueued = false;
            scheduleWorkflowDraftSync();
        }
    }
}

async function loadWorkflowScopes() {
    if (!session) return;
    try {
        const response = await fetch("/api/batches", {
            headers: { Authorization: "Bearer " + session.token }
        });
        const batches = await readJsonResponse(response);
        if (!response.ok) {
            throw new Error(batches.error || "Unable to load route scopes.");
        }
        if (!Array.isArray(batches)) {
            throw new Error("The route scope response is malformed.");
        }
        const selected = workflowBatchSelect.value;
        workflowBatchSelect.replaceChildren();
        const defaultOption = document.createElement("option");
        defaultOption.value = "";
        defaultOption.textContent = "Default route";
        workflowBatchSelect.append(defaultOption);
        for (const batch of batches) {
            const option = document.createElement("option");
            option.value = batch.batchId;
            option.textContent = batch.batchId + " · " + batch.product;
            workflowBatchSelect.append(option);
        }
        workflowBatchSelect.value = [...workflowBatchSelect.options]
            .some((option) => option.value === selected) ? selected : "";
    } catch (error) {
        workflowStatus.textContent = error.message;
        workflowStatus.className = "status error";
    }
}

async function loadWorkflowView(
    batchId = workflowBatchSelect.value,
    { discardLocalDraft = false } = {}
) {
    if (!session) return;
    const loadId = ++workflowLoadId;
    const requestedBatchId = batchId || "";
    const localWorkflowAtStart = workflowSnapshot();
    workflowBatchId = requestedBatchId;
    workflowStatus.textContent = "Loading preset route...";
    workflowStatus.className = "status pending";
    statusLine.textContent = "Loading supply-chain workflow...";
    statusLine.className = "status pending";

    try {
        const query = requestedBatchId
            ? "?batchId=" + encodeURIComponent(requestedBatchId)
            : "";
        const response = await fetch("/api/workflow-view" + query, {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const view = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            throw new Error(view.error ||
                `Workflow view request failed: ${response.status}`);
        }
        const workflow = view.workflow;
        const graph = view.chain;
        if (!workflow || !Array.isArray(workflow.nodes) ||
            !Array.isArray(workflow.edges)) {
            throw new Error("The workflow response is malformed.");
        }
        if (!graph || !Array.isArray(graph.nodes) || !Array.isArray(graph.edges)) {
            throw new Error("The chain response is malformed.");
        }
        if (loadId !== workflowLoadId || workflowBatchId !== requestedBatchId ||
            (!discardLocalDraft && (workflowRouteDirty ||
                workflowSnapshot() !== localWorkflowAtStart))) {
            return false;
        }

        chainGraph = graph;
        chainGraphLoaded = true;
        workflowRouteDirty = false;
        renderWorkflow(workflow);
        await loadConfirmationPolicy();
        const activeBatchId = activeWorkflowPreviewBatchId(graph);
        const activeRecordCount = activeBatchId
            ? graph.nodes.filter((node) => node.batchId === activeBatchId).length
            : 0;
        const routePreviewCount = activeBatchId && workflowData
            ? workflowPreviewRouteOrder(workflowData).length
            : 0;
        const activePreviewCount = routePreviewCount > 0
            ? routePreviewCount
            : activeRecordCount;
        const visibleNodeCount = graph.nodes.length - activeRecordCount + activePreviewCount;
        const visibleBatchIds = new Set(graph.nodes.map((node) => node.batchId));
        if (activeBatchId && activePreviewCount > 0) visibleBatchIds.add(activeBatchId);
        statusLine.textContent = visibleNodeCount === 0
            ? "No supply-chain workflow yet."
            : `Loaded ${visibleNodeCount} node(s) across ${visibleBatchIds.size} batch(es).`;
        statusLine.className = "status success";
        return true;
    } catch (error) {
        if (loadId === workflowLoadId && workflowBatchId === requestedBatchId) {
            workflowStatus.textContent = error.message;
            workflowStatus.className = "status error";
            statusLine.textContent = error.message;
            statusLine.className = "status error";
        }
        return false;
    }
}

function setSnapshotStatus(message, state = "") {
    snapshotStatus.textContent = message;
    snapshotStatus.className = state ? `status ${state}` : "status";
}

function setSnapshotRefreshPolicyStatus(message, state = "") {
    snapshotRefreshPolicyStatus.textContent = message;
    snapshotRefreshPolicyStatus.className = state ? `status ${state}` : "status";
}

function updateSnapshotControls() {
    const busy = Boolean(snapshotOperation) || snapshotRefreshInFlight;
    const hasBatches = snapshotBatches.length > 0;
    const hasProduct = Boolean(selectedSnapshotBatch()?.product);
    snapshotBatchSelect.disabled = busy || !hasBatches;
    generateSnapshotButton.disabled = busy || !hasBatches;
    publishSnapshotButton.disabled = busy || !publicationCandidate;
    snapshotRefreshValue.disabled = busy || !hasProduct ||
        !snapshotRefreshPoliciesAvailable;
    snapshotRefreshUnit.disabled = busy || !hasProduct ||
        !snapshotRefreshPoliciesAvailable;
}

function beginSnapshotOperation(operation) {
    if (snapshotOperation) return false;
    snapshotOperation = operation;
    updateSnapshotControls();
    return true;
}

function finishSnapshotOperation() {
    snapshotOperation = "";
    updateSnapshotControls();

    if (liveRefreshQueued && !liveRefreshInFlight && !liveRefreshBlocked()) {
        const queuedType = liveRefreshQueuedType;
        liveRefreshQueued = false;
        liveRefreshQueuedType = "";
        window.queueMicrotask(() => queueLiveRefresh(queuedType));
        return;
    }
    if (snapshotRefreshQueued && !snapshotRefreshInFlight) {
        snapshotRefreshQueued = false;
        window.queueMicrotask(() => loadSnapshotCandidates());
    }
}

function formatRefreshInterval(seconds) {
    const value = Number(seconds);
    if (!Number.isInteger(value) || value <= 0) return "Unknown interval";
    const units = [
        { seconds: 86400, label: "day" },
        { seconds: 3600, label: "hour" },
        { seconds: 60, label: "minute" }
    ];
    const unit = units.find((item) => value % item.seconds === 0) || units[2];
    const amount = unit.seconds === 60
        ? Math.max(1, Math.ceil(value / 60))
        : value / unit.seconds;
    return `${amount} ${unit.label}${amount === 1 ? "" : "s"}`;
}

function refreshIntervalEditor(seconds) {
    const value = Number(seconds);
    const units = [
        { seconds: 86400, value: "days" },
        { seconds: 3600, value: "hours" },
        { seconds: 60, value: "minutes" }
    ];
    const unit = units.find((item) => Number.isInteger(value) && value % item.seconds === 0)
        || units[units.length - 1];
    return {
        value: Math.max(1, Number.isInteger(value)
            ? Math.ceil(value / unit.seconds)
            : 1),
        unit: unit.value
    };
}

function snapshotRefreshIntervalSeconds() {
    const multipliers = { minutes: 60, hours: 3600, days: 86400 };
    const amount = Number(snapshotRefreshValue.value);
    const multiplier = multipliers[snapshotRefreshUnit.value];
    const seconds = amount * multiplier;
    if (!Number.isInteger(amount) || amount <= 0 || !Number.isInteger(seconds) ||
        seconds < 60 || seconds > 365 * 24 * 60 * 60) {
        throw new Error("Enter a whole-number interval from 1 minute to 365 days.");
    }
    return seconds;
}

function selectedSnapshotRefreshPolicy() {
    const product = String(selectedSnapshotBatch()?.product || "");
    return snapshotRefreshPolicies.find((policy) => policy.product === product);
}

function syncSnapshotRefreshEditor({ force = false } = {}) {
    const batch = selectedSnapshotBatch();
    const product = String(batch?.product || "");
    if (!product) {
        snapshotRefreshEditorProduct = "";
        snapshotRefreshProduct.textContent = "Select a completed batch";
        snapshotRefreshValue.value = "1";
        snapshotRefreshUnit.value = "hours";
        updateSnapshotControls();
        return;
    }

    const policy = selectedSnapshotRefreshPolicy();
    const intervalSeconds = Number(policy?.intervalSeconds) > 0
        ? Number(policy.intervalSeconds)
        : snapshotRefreshDefaultSeconds;
    snapshotRefreshProduct.textContent = policy?.configured
        ? `${product} · currently ${formatRefreshInterval(intervalSeconds)}`
        : `${product} · default ${formatRefreshInterval(snapshotRefreshDefaultSeconds)}`;
    if (!force && snapshotRefreshEditorProduct === product) {
        updateSnapshotControls();
        return;
    }

    const editor = refreshIntervalEditor(intervalSeconds);
    snapshotRefreshValue.value = String(editor.value);
    snapshotRefreshUnit.value = editor.unit;
    snapshotRefreshEditorProduct = product;
    updateSnapshotControls();
}

function applySnapshotRefreshPolicies(result) {
    snapshotRefreshPolicies = Array.isArray(result.products) ? result.products : [];
    const defaultSeconds = Number(result.defaultIntervalSeconds);
    snapshotRefreshDefaultSeconds = Number.isInteger(defaultSeconds) && defaultSeconds >= 60
        ? defaultSeconds
        : 3600;
    snapshotRefreshPoliciesAvailable = true;
    syncSnapshotRefreshEditor({ force: true });
}

async function loadSnapshotRefreshPolicies() {
    if (!session) return;
    try {
        const response = await fetch("/api/snapshot/refresh-policies", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        if (!Array.isArray(result.products)) {
            throw new Error("The Snapshot refresh policy response is malformed.");
        }
        applySnapshotRefreshPolicies(result);
        setSnapshotRefreshPolicyStatus("");
    } catch (error) {
        snapshotRefreshPoliciesAvailable = false;
        syncSnapshotRefreshEditor({ force: true });
        setSnapshotRefreshPolicyStatus(error.message, "error");
    }
}

async function saveSnapshotRefreshPolicyIfChanged() {
    if (!snapshotRefreshPoliciesAvailable) return false;
    const batch = selectedSnapshotBatch();
    const product = String(batch?.product || "");
    if (!product) throw new Error("Select a completed batch first.");

    const intervalSeconds = snapshotRefreshIntervalSeconds();
    const policy = selectedSnapshotRefreshPolicy();
    const currentSeconds = Number(policy?.intervalSeconds) > 0
        ? Number(policy.intervalSeconds)
        : snapshotRefreshDefaultSeconds;
    if (intervalSeconds === currentSeconds) return false;

    setSnapshotRefreshPolicyStatus(`Saving ${product}: ${formatRefreshInterval(intervalSeconds)}...`,
        "pending");
    const response = await fetch("/api/snapshot/refresh-policies", {
        method: "POST",
        headers: {
            "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
            Authorization: `Bearer ${session.token}`
        },
        body: new URLSearchParams({
            product,
            intervalSeconds: String(intervalSeconds)
        }).toString()
    });
    const result = await readJsonResponse(response);
    if (response.status === 401 || response.status === 403) {
        clearSession();
        throw new Error("Control-panel session expired or insufficient permissions.");
    }
    if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
    if (!Array.isArray(result.products)) {
        throw new Error("The updated Snapshot refresh policy response is malformed.");
    }
    applySnapshotRefreshPolicies(result);
    setSnapshotRefreshPolicyStatus(
        `Automatic refresh set to ${formatRefreshInterval(intervalSeconds)} for ${product}.`,
        "success"
    );
    return true;
}

function selectedSnapshotBatch() {
    return snapshotBatches.find((batch) => batch.batchId === snapshotBatchSelect.value);
}

function clearSnapshotPreview() {
    snapshotPreview.hidden = true;
    publicationCandidate = null;
    snapshotPreviewBatchId = "";
    snapshotPreviewRouteFingerprint = "";
    snapshotPreviewPrivateHash = "";
    snapshotPublishStatus.textContent = "";
    updateSnapshotControls();
}

function renderSnapshotEvidence({ clearPreview = true } = {}) {
    const hadExistingOptions = Boolean(
        snapshotEvidenceList.querySelector('input[name="selectedEvidence"]')
    );
    const selectedEvidence = new Set(
        [...snapshotEvidenceList.querySelectorAll(
            'input[name="selectedEvidence"]:checked'
        )].map((input) => input.value)
    );
    snapshotEvidenceList.replaceChildren();
    if (clearPreview) clearSnapshotPreview();
    const batch = selectedSnapshotBatch();
    const evidence = batch?.evidence || [];
    if (evidence.length === 0) {
        const empty = document.createElement("p");
        empty.className = "status";
        empty.textContent = "No approved public evidence is attached to this batch.";
        snapshotEvidenceList.append(empty);
        return;
    }

    for (const item of evidence) {
        const option = document.createElement("label");
        option.className = "snapshot-evidence-option";
        const input = document.createElement("input");
        input.type = "checkbox";
        input.name = "selectedEvidence";
        input.value = `${item.stage}|${item.category}|${item.cid}`;
        input.checked = !clearPreview && hadExistingOptions
            ? selectedEvidence.has(input.value)
            : Boolean(item.selectedByDefault);
        const content = document.createElement("span");
        const title = document.createElement("strong");
        title.textContent = `${item.label} · ${item.stage}`;
        const cid = document.createElement("code");
        cid.textContent = item.cid;
        content.append(title, cid);
        option.append(input, content);
        snapshotEvidenceList.append(option);
    }
}

function renderSnapshotBatches(batches) {
    const previousSelection = snapshotBatchSelect.value;
    const previousPreviewBatchId = snapshotPreviewBatchId;
    const previewStillEligible = !snapshotPreview.hidden &&
        batches.some((batch) =>
            batch.batchId === previousPreviewBatchId &&
            batch.routeFingerprint === snapshotPreviewRouteFingerprint &&
            batch.finalPrivateBlockHash === snapshotPreviewPrivateHash
        );
    if (!previewStillEligible) clearSnapshotPreview();
    snapshotBatches = batches;
    snapshotBatchSelect.replaceChildren();
    for (const batch of batches) {
        const option = document.createElement("option");
        option.value = batch.batchId;
        option.textContent = `${batch.batchId} · ${batch.product}`;
        snapshotBatchSelect.append(option);
    }

    const hasBatches = batches.length > 0;
    snapshotBatchCount.textContent = hasBatches
        ? `${batches.length} eligible batch${batches.length === 1 ? "" : "es"}`
        : "No eligible batches";
    snapshotBatchCount.className = hasBatches ? "badge verified" : "badge pending";
    const selection = previewStillEligible
        ? previousPreviewBatchId
        : previousSelection;
    if ([...snapshotBatchSelect.options].some((option) => option.value === selection)) {
        snapshotBatchSelect.value = selection;
    }
    syncSnapshotRefreshEditor();
    renderSnapshotEvidence({ clearPreview: false });
    updateSnapshotControls();
    setSnapshotStatus(hasBatches
        ? "Select a completed batch and review its public evidence."
        : "Complete and verify every assigned route stage before generating a snapshot.",
        hasBatches ? "success" : "pending");
}

async function loadSnapshotCandidates() {
    if (!session) return;
    if (snapshotOperation) {
        snapshotRefreshQueued = true;
        return;
    }
    if (snapshotRefreshInFlight) {
        snapshotRefreshQueued = true;
        return;
    }
    snapshotRefreshInFlight = true;
    snapshotRefreshQueued = false;
    const routeSnapshotAtStart = workflowSnapshot();
    snapshotBatchCount.textContent = "Loading batches";
    snapshotBatchCount.className = "badge pending";
    updateSnapshotControls();
    try {
        const response = await fetch("/api/snapshot/eligible-batches", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        if (!Array.isArray(result.batches)) {
            throw new Error("The snapshot candidate response is malformed.");
        }
        if (workflowRouteDirty || routeSnapshotAtStart !== workflowSnapshot()) return;
        renderSnapshotBatches(result.batches);
    } catch (error) {
        if (!workflowRouteDirty && routeSnapshotAtStart === workflowSnapshot()) {
            renderSnapshotBatches([]);
            setSnapshotStatus(error.message, "error");
        }
    } finally {
        snapshotRefreshInFlight = false;
        updateSnapshotControls();
        if (snapshotRefreshQueued && !snapshotOperation) {
            snapshotRefreshQueued = false;
            window.queueMicrotask(() => loadSnapshotCandidates());
        }
    }
}

function renderSnapshotPreview(result) {
    snapshotId.textContent = result.snapshotId;
    snapshotPublicRoot.textContent = result.publicRoot;
    snapshotPrivateHash.textContent = result.finalPrivateBlockHash;
    snapshotFieldCount.textContent =
        `${result.publicFieldCount} fields · ${result.selectedEvidenceCount} evidence CID(s)`;
    snapshotManifestJson.textContent = JSON.stringify(result.manifest, null, 2);
    snapshotExcludedFields.replaceChildren();
    for (const field of result.excludedFields || []) {
        const item = document.createElement("li");
        item.textContent = field;
        snapshotExcludedFields.append(item);
    }
    publicationCandidate = result.publicationCandidate;
    snapshotPreviewBatchId = snapshotBatchSelect.value;
    snapshotPreviewRouteFingerprint = result.routeFingerprint || "";
    snapshotPreviewPrivateHash = result.finalPrivateBlockHash || "";
    snapshotPublishStatus.textContent = publicationCandidate
        ? "Preview ready for administrator publication."
        : "Publication data is unavailable.";
    snapshotPublishStatus.className = publicationCandidate
        ? "status pending"
        : "status error";
    snapshotPreview.hidden = false;
    updateSnapshotControls();
}

async function publishSnapshot() {
    if (!session || !publicationCandidate) return;
    if (!beginSnapshotOperation("publish")) return;
    snapshotPublishStatus.textContent = "Submitting snapshot to the local public chain...";
    snapshotPublishStatus.className = "status pending";
    try {
        const response = await fetch("/api/snapshot/publish", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
                Authorization: `Bearer ${session.token}`
            },
            body: JSON.stringify(publicationCandidate)
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        snapshotPublishStatus.textContent =
            `Published in block ${result.blockNumber}. Open the QR display page on port 8084.`;
        snapshotPublishStatus.className = "status success";
        publicationCandidate = null;
    } catch (error) {
        snapshotPublishStatus.textContent = error.message;
        snapshotPublishStatus.className = "status error";
    } finally {
        finishSnapshotOperation();
    }
}

async function generateSnapshotPreview(event) {
    event.preventDefault();
    if (!session || !snapshotBatchSelect.value) return;
    if (!beginSnapshotOperation("preview")) return;
    const batchId = snapshotBatchSelect.value;
    const selectedEvidence = [...snapshotEvidenceList.querySelectorAll(
        'input[name="selectedEvidence"]:checked'
    )].map((input) => input.value);

    clearSnapshotPreview();
    try {
        if (snapshotRefreshPoliciesAvailable) {
            setSnapshotStatus("Checking the automatic refresh interval...", "pending");
            await saveSnapshotRefreshPolicyIfChanged();
        }
        setSnapshotStatus("Generating a private, non-published preview...", "pending");
        const response = await fetch("/api/snapshot/preview", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: `Bearer ${session.token}`
            },
            body: new URLSearchParams({
                batchId,
                selectedEvidence: selectedEvidence.join(",")
            }).toString()
        });
        const result = await readJsonResponse(response);
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        renderSnapshotPreview(result);
        setSnapshotStatus("Snapshot preview generated locally. Nothing was published.", "success");
    } catch (error) {
        setSnapshotStatus(error.message, "error");
    } finally {
        finishSnapshotOperation();
    }
}

async function loadDashboard() {
    await loadWorkflowScopes();
    await loadWorkflowView(workflowBatchSelect.value);
    await Promise.all([
        loadSnapshotCandidates(),
        loadSnapshotRefreshPolicies()
    ]);
}

function startWorkflowNodeDrag(event, nodeId) {
    if (!workflowData || event.button !== 0) return;
    const node = workflowData.nodes.find((item) => item.id === nodeId);
    if (!node) return;
    event.preventDefault();
    event.stopPropagation();
    const point = workflowPoint(event);
    workflowSelectedNodeId = nodeId;
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = {
        mode: "node",
        pointerId: event.pointerId,
        nodeId,
        offsetX: point.x - node.x,
        offsetY: point.y - node.y,
        moved: false,
        before: workflowSnapshot()
    };
    workflowCanvas.setPointerCapture(event.pointerId);
    renderWorkflow(workflowData);
}

function startWorkflowConnection(event, nodeId) {
    if (!workflowData || event.button !== 0) return;
    const node = workflowData.nodes.find((item) => item.id === nodeId);
    if (!node || node.role === "supermarket") return;
    event.preventDefault();
    event.stopPropagation();
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = {
        mode: "connect",
        pointerId: event.pointerId,
        sourceId: nodeId,
        targetId: "",
        currentPoint: workflowPoint(event)
    };
    workflowCanvas.setPointerCapture(event.pointerId);
    renderWorkflow(workflowData);
}

function setWorkflowRouteError(message) {
    workflowStatus.textContent = `Route error: ${message}`;
    workflowStatus.className = "status error";
}

function deleteWorkflowEdgeAt(index) {
    if (!workflowData || index < 0 || index >= workflowData.edges.length) return;
    captureWorkflowHistory();
    workflowData.edges.splice(index, 1);
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    renderWorkflow(workflowData);
    markWorkflowDraftChanged("Connection removed. Synchronizing the route draft...");
}

function deleteSelectedWorkflowEdge() {
    deleteWorkflowEdgeAt(workflowSelectedEdgeIndex);
}

function deleteSelectedWorkflowNode() {
    if (!workflowData || !workflowSelectedNodeId) return;
    const selected = workflowData.nodes.find((node) =>
        node.id === workflowSelectedNodeId
    );
    if (!selected || selected.role === "supplier" || selected.role === "supermarket") {
        setWorkflowRouteError("Supplier and Supermarket are required route endpoints.");
        return;
    }
    const predecessorIds = workflowData.edges
        .filter((edge) => edge.to === selected.id)
        .map((edge) => edge.from);
    const successorIds = workflowData.edges
        .filter((edge) => edge.from === selected.id)
        .map((edge) => edge.to);

    captureWorkflowHistory();
    workflowData.nodes = workflowData.nodes.filter((node) =>
        node.id !== workflowSelectedNodeId
    );
    workflowData.edges = workflowData.edges.filter((edge) =>
        edge.from !== workflowSelectedNodeId && edge.to !== workflowSelectedNodeId
    );

    // Preserve a linear route when a middle stage is removed.
    if (predecessorIds.length === 1 && successorIds.length === 1) {
        const from = predecessorIds[0];
        const to = successorIds[0];
        const alreadyConnected = workflowData.edges.some((edge) =>
            edge.from === from && edge.to === to
        );
        if (from !== to && !alreadyConnected) {
            workflowData.edges.push({ from, to });
        }
    }

    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    renderWorkflow(workflowData);
    markWorkflowDraftChanged("Route node removed. Synchronizing the route draft...");
}

function finishWorkflowPointer(event) {
    if (!workflowPointerState || workflowPointerState.pointerId !== event.pointerId) return;
    const state = workflowPointerState;
    let connectionChanged = false;
    if (workflowCanvas.hasPointerCapture(event.pointerId)) {
        workflowCanvas.releasePointerCapture(event.pointerId);
    }

    if (state.mode === "connect") {
        const targetElement = document.elementFromPoint(event.clientX, event.clientY)
            ?.closest(".workflow-handle-target");
        const targetId = targetElement?.dataset.nodeId || state.targetId;
        if (targetId) {
            const error = workflowConnectionError(state.sourceId, targetId);
            if (error) {
                setWorkflowRouteError(error);
            } else {
                captureWorkflowHistory();
                workflowData.edges.push({ from: state.sourceId, to: targetId });
                workflowSelectedNodeId = targetId;
                connectionChanged = true;
            }
        }
    }

    if (state.mode === "node" && state.moved) {
        captureWorkflowDragHistory(state.before);
    }
    workflowPointerState = null;
    workflowCanvas.classList.remove("is-panning");
    renderWorkflow(workflowData);
    if (connectionChanged) {
        markWorkflowDraftChanged("Connection added. Synchronizing the route draft...");
    } else if (state.mode === "node" && state.moved) {
        scheduleWorkflowDraftSync();
    }
}

let workflowResizeFrame = 0;
function scheduleWorkflowResize() {
    if (workflowResizeFrame) return;
    workflowResizeFrame = requestAnimationFrame(() => {
        workflowResizeFrame = 0;
        if (workflowData) renderWorkflow(workflowData);
    });
}

window.addEventListener("resize", scheduleWorkflowResize);
if (window.ResizeObserver) {
    new ResizeObserver(scheduleWorkflowResize).observe(workflowCanvas);
}

loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", logout);
workflowBatchSelect.addEventListener("change", () => {
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    workflowViewportReady = false;
    loadWorkflowView(workflowBatchSelect.value);
});

workflowCanvas.addEventListener("pointerdown", (event) => {
    if (!workflowData || event.button !== 0) return;
    if (event.target.closest(".workflow-canvas-controls")) return;
    if (event.target.closest(".workflow-node") ||
        event.target.closest(".workflow-edge")) return;
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = {
        mode: "pan",
        pointerId: event.pointerId,
        startX: event.clientX,
        startY: event.clientY,
        viewportX: workflowViewport.x,
        viewportY: workflowViewport.y
    };
    workflowCanvas.setPointerCapture(event.pointerId);
    workflowCanvas.classList.add("is-panning");
    renderWorkflow(workflowData);
});

workflowCanvas.addEventListener("pointermove", (event) => {
    if (!workflowData || !workflowPointerState ||
        workflowPointerState.pointerId !== event.pointerId) return;

    const state = workflowPointerState;
    if (state.mode === "pan") {
        workflowViewport.x = state.viewportX + event.clientX - state.startX;
        workflowViewport.y = state.viewportY + event.clientY - state.startY;
        applyWorkflowViewport();
        event.preventDefault();
        return;
    }

    if (state.mode === "node") {
        const point = workflowPoint(event);
        const node = workflowData.nodes.find((item) => item.id === state.nodeId);
        if (!node) return;
        node.x = Math.round(point.x - state.offsetX);
        node.y = Math.round(point.y - state.offsetY);
        state.moved = true;
        renderWorkflow(workflowData);
        event.preventDefault();
        return;
    }

    if (state.mode === "connect") {
        state.currentPoint = workflowPoint(event);
        const targetElement = document.elementFromPoint(event.clientX, event.clientY)
            ?.closest(".workflow-handle-target");
        state.targetId = targetElement?.dataset.nodeId || "";
        renderWorkflow(workflowData);
        event.preventDefault();
    }
});

workflowCanvas.addEventListener("pointerup", finishWorkflowPointer);
workflowCanvas.addEventListener("pointercancel", finishWorkflowPointer);
workflowCanvas.addEventListener("wheel", (event) => {
    if (!workflowData) return;
    const rect = workflowCanvas.getBoundingClientRect();
    const cursorX = event.clientX - rect.left;
    const cursorY = event.clientY - rect.top;
    const isPinchZoom = event.ctrlKey || event.metaKey;
    if (!isPinchZoom) {
        event.preventDefault();
        workflowViewport.x -= event.deltaX;
        workflowViewport.y -= event.deltaY;
        workflowViewportReady = true;
        applyWorkflowViewport();
        return;
    }

    event.preventDefault();
    const unit = event.deltaMode === 1
        ? 16
        : event.deltaMode === 2 ? workflowCanvas.clientHeight : 1;
    workflowWheelState.pendingDelta = Math.max(
        -240,
        Math.min(240, workflowWheelState.pendingDelta + event.deltaY * unit)
    );
    workflowWheelState.anchor = { x: cursorX, y: cursorY };
    if (!workflowWheelState.frame) {
        workflowWheelState.frame = requestAnimationFrame(() => {
            workflowWheelState.frame = 0;
            const delta = workflowWheelState.pendingDelta;
            workflowWheelState.pendingDelta = 0;
            const anchor = workflowWheelState.anchor;
            if (!anchor || !delta) return;
            const sceneX = (anchor.x - workflowViewport.x) / workflowViewport.zoom;
            const sceneY = (anchor.y - workflowViewport.y) / workflowViewport.zoom;
            const nextZoom = workflowViewport.zoom * Math.exp(-delta * 0.0035);
            workflowViewport.zoom = Math.max(
                WORKFLOW_MIN_ZOOM,
                Math.min(WORKFLOW_MAX_ZOOM, nextZoom)
            );
            workflowViewport.x = anchor.x - sceneX * workflowViewport.zoom;
            workflowViewport.y = anchor.y - sceneY * workflowViewport.zoom;
            workflowViewportReady = true;
            applyWorkflowViewport();
        });
    }
}, { passive: false });

workflowZoomOut.addEventListener("click", () =>
    setWorkflowZoom(workflowViewport.zoom - WORKFLOW_ZOOM_BUTTON_STEP));
workflowZoomIn.addEventListener("click", () =>
    setWorkflowZoom(workflowViewport.zoom + WORKFLOW_ZOOM_BUTTON_STEP));
workflowFitView.addEventListener("click", () => fitWorkflowView());

workflowNodeType.addEventListener("change", () => {
    populateWorkflowNodeAccounts(workflowRoleForType(workflowNodeType.value));
});

workflowDeleteEdge.addEventListener("click", deleteSelectedWorkflowEdge);
workflowDeleteNode.addEventListener("click", deleteSelectedWorkflowNode);
workflowAutoLayout.addEventListener("click", () => {
    if (!workflowData) return;
    const before = workflowSnapshot();
    captureWorkflowHistory();
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    workflowViewportReady = false;
    recalculateWorkflowLayout(true);
    renderWorkflow(workflowData, { fitView: true });
    if (workflowSnapshot() !== before) scheduleWorkflowDraftSync();
});
workflowAddNode.addEventListener("click", () => {
    if (!workflowData) return;
    const type = workflowNodeType.value;
    const role = workflowRoleForType(type);
    const prefix = type === "warehouse" ? "warehouse" : "transport";
    const usedAccounts = new Set(workflowData.nodes
        .filter((node) => node.role === role)
        .map((node) => node.username));
    const selectedAccount = workflowNodeAccount.value;
    const username = selectedAccount && !usedAccounts.has(selectedAccount)
        ? selectedAccount
        : nextWorkflowAccount(role);
    if (!username) {
        setWorkflowRouteError(`No active ${workflowRoleLabel(role)} account is available.`);
        return;
    }
    const count = nextWorkflowNodeSequence(type);
    const nodeId = createWorkflowNodeId(prefix);
    const node = {
        id: nodeId,
        nodeType: type,
        label: (type === "warehouse" ? "Warehouse " : "Transport ") + count,
        role,
        username,
        x: 0,
        y: 0,
        stepIndex: -1
    };

    captureWorkflowHistory();
    const position = workflowNewNodePosition();

    node.x = workflowInteger(position.x);
    node.y = workflowInteger(position.y);
    workflowData.nodes.push(node);
    workflowSelectedNodeId = nodeId;
    workflowSelectedEdgeIndex = -1;
    workflowLayoutInitialized = true;
    renderWorkflow(workflowData);
    markWorkflowDraftChanged("New route node added. The previous Snapshot is no longer valid.");
});
workflowResetRoute.addEventListener("click", () => {
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    workflowViewportReady = false;
    workflowRouteDirty = false;
    if (workflowAutoSaveTimer) {
        window.clearTimeout(workflowAutoSaveTimer);
        workflowAutoSaveTimer = 0;
    }
    loadWorkflowView(workflowBatchSelect.value, { discardLocalDraft: true });
});
workflowUndo.addEventListener("click", undoWorkflowEdit);
workflowRedo.addEventListener("click", redoWorkflowEdit);
confirmationPolicyForm.addEventListener("submit", saveConfirmationPolicy);
document.addEventListener("keydown", (event) => {
    if (["INPUT", "TEXTAREA", "SELECT"].includes(event.target.tagName)) return;
    const modifier = event.ctrlKey || event.metaKey;
    if (modifier && event.key.toLowerCase() === "z") {
        event.preventDefault();
        if (event.shiftKey) redoWorkflowEdit();
        else undoWorkflowEdit();
        return;
    }
    if (modifier && event.key.toLowerCase() === "y") {
        event.preventDefault();
        redoWorkflowEdit();
        return;
    }
    if (event.key !== "Delete" && event.key !== "Backspace") return;
    if (workflowSelectedEdgeIndex >= 0) {
        event.preventDefault();
        deleteSelectedWorkflowEdge();
    } else if (workflowSelectedNodeId) {
        event.preventDefault();
        deleteSelectedWorkflowNode();
    }
});
snapshotBatchSelect.addEventListener("change", () => {
    snapshotRefreshEditorProduct = "";
    syncSnapshotRefreshEditor({ force: true });
    renderSnapshotEvidence();
});
snapshotPreviewForm.addEventListener("submit", generateSnapshotPreview);
publishSnapshotButton.addEventListener("click", publishSnapshot);
restoreSession();
