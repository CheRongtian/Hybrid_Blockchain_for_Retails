const loginCard = document.querySelector("#login-card");
const loginForm = document.querySelector("#login-form");
const loginStatus = document.querySelector("#login-status");
const rememberLogin = document.querySelector("#remember-login");
const dashboard = document.querySelector("#dashboard");
const identityStatus = document.querySelector("#identity-status");
const logoutButton = document.querySelector("#logout-button");
const list = document.querySelector("#record-list");
const statusLine = document.querySelector("#load-status");
const refreshButton = document.querySelector("#refresh-button");
const workflowCanvas = document.querySelector("#workflow-canvas");
const workflowStatus = document.querySelector("#workflow-status");
const workflowRouteBadge = document.querySelector("#workflow-route-badge");
const workflowBatchSelect = document.querySelector("#workflow-batch-select");
const workflowNodeType = document.querySelector("#workflow-node-type");
const workflowAddNode = document.querySelector("#workflow-add-node");
const workflowDeleteNode = document.querySelector("#workflow-delete-node");
const workflowDeleteEdge = document.querySelector("#workflow-delete-edge");
const workflowAutoLayout = document.querySelector("#workflow-auto-layout");
const workflowResetRoute = document.querySelector("#workflow-reset-route");
const workflowSave = document.querySelector("#workflow-save");
const workflowNodeEditor = document.querySelector("#workflow-node-editor");
const workflowNodeLabel = document.querySelector("#workflow-node-label");
const workflowApplyNodeLabel = document.querySelector("#workflow-apply-node-label");
const confirmationPolicyForm = document.querySelector("#confirmation-policy-form");
const rolePolicyList = document.querySelector("#role-policy-list");
const policyRoleCount = document.querySelector("#policy-role-count");
const policyStatus = document.querySelector("#policy-status");
const savePolicyButton = document.querySelector("#save-policy-button");
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
const policyRoles = [
    ["supplier", "Supplier"],
    ["logistics", "Logistics"],
    ["warehouse", "Warehouse"],
    ["supermarket", "Supermarket"]
];
let session = null;
let workflowData = null;
let workflowBatchId = "";
let workflowSelectedNodeId = "";
let workflowSelectedEdgeIndex = -1;
let workflowPointerState = null;
let snapshotBatches = [];
let publicationCandidate = null;

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

function clearSession() {
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
    workflowBatchSelect.value = "";
    workflowCanvas.replaceChildren();
    snapshotBatches = [];
    snapshotBatchSelect.replaceChildren();
    snapshotEvidenceList.replaceChildren();
    snapshotPreview.hidden = true;
    publicationCandidate = null;
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
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);
        if (result.user.role !== "admin") throw new Error("This account does not have control-panel access.");

        saveSession(result, rememberLogin.checked);
        loginForm.reset();
        showSession(result);
        loadDashboard();
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
        const user = await response.json();
        if (!response.ok || user.role !== "admin") throw new Error("Session expired");
        showSession({ token: stored.token, user });
        loadDashboard();
    } catch {
        clearSession();
    }
}

function setPolicyStatus(message, state = "") {
    policyStatus.textContent = message;
    policyStatus.className = state ? `status ${state}` : "status";
}

function policyOption(role, suffix, label) {
    const wrapper = document.createElement("label");
    wrapper.className = "policy-option";
    const input = document.createElement("input");
    input.type = "checkbox";
    input.name = role + suffix;
    input.value = "true";
    input.setAttribute("aria-label", `${role} ${label}`);
    const text = document.createElement("span");
    text.textContent = label;
    wrapper.append(input, text);
    return wrapper;
}

function renderConfirmationPolicies(policies) {
    const policiesByRole = new Map(
        policies.map((policy) => [policy.role, policy])
    );
    rolePolicyList.replaceChildren();
    const header = document.createElement("div");
    header.className = "policy-matrix-header";
    for (const label of ["Role", "Typed name", "Handwritten", "Face"]) {
        const cell = document.createElement("span");
        cell.textContent = label;
        header.append(cell);
    }
    rolePolicyList.append(header);
    for (const [role, label] of policyRoles) {
        const policy = policiesByRole.get(role) || {};
        const row = document.createElement("div");
        row.className = "role-policy";
        const roleName = document.createElement("strong");
        roleName.className = "policy-role-name";
        roleName.textContent = label;
        const typed = policyOption(role, "TypedName", "Typed name");
        const handwritten = policyOption(role, "Handwritten", "Handwritten");
        const face = policyOption(role, "Face", "Face");
        typed.querySelector("input").checked = Boolean(policy.typedName);
        handwritten.querySelector("input").checked = Boolean(policy.handwritten);
        face.querySelector("input").checked = Boolean(policy.face);
        row.append(roleName, typed, handwritten, face);
        rolePolicyList.append(row);
    }
    policyRoleCount.textContent = `${policiesByRole.size} roles configured`;
}

async function loadConfirmationPolicy() {
    if (!session) return;
    try {
        const response = await fetch("/api/confirmation-policy", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const result = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        renderConfirmationPolicies(result.policies || []);
        setPolicyStatus("Role policies loaded.", "success");
    } catch (error) {
        setPolicyStatus(error.message, "error");
    }
}

async function saveConfirmationPolicy(event) {
    event.preventDefault();
    if (!session) return;
    for (const [role, label] of policyRoles) {
        const methods = confirmationPolicyForm.querySelectorAll(
            `input[name^="${role}"]:checked`
        );
        if (methods.length === 0) {
            setPolicyStatus(`Enable at least one method for ${label}.`, "error");
            return;
        }
    }

    savePolicyButton.disabled = true;
    setPolicyStatus("Saving confirmation policy...", "pending");
    try {
        const response = await fetch("/api/confirmation-policy", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: `Bearer ${session.token}`
            },
            body: new URLSearchParams(new FormData(confirmationPolicyForm)).toString()
        });
        const result = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        renderConfirmationPolicies(result.policies || []);
        setPolicyStatus("Role confirmation policies saved.", "success");
    } catch (error) {
        setPolicyStatus(error.message, "error");
    } finally {
        savePolicyButton.disabled = false;
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

    const summaryHeader = document.createElement("div");
    summaryHeader.className = "merkle-tree-summary-header";
    const summaryTitle = document.createElement("strong");
    summaryTitle.textContent = `Merkle Tree ${record.blockID}`;
    const summaryCount = document.createElement("span");
    summaryCount.textContent = `${record.merkleTree?.leafCount || 0} leaves`;
    summaryHeader.append(summaryTitle, summaryCount);
    section.append(summaryHeader);

    const rootSummary = document.createElement("div");
    rootSummary.className = record.merkleTree?.consistent
        ? "merkle-tree-summary-root verified-node"
        : "merkle-tree-summary-root failed-node";
    rootSummary.textContent = record.merkleTree?.consistent
        ? "Verified root"
        : "Root check failed";
    section.append(rootSummary);

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
    dialogRoot.textContent = "Select a node to inspect its details.";
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

function renderChainNode(record) {
    const node = document.createElement("div");
    node.className = "chain-node";

    const header = document.createElement("div");
    header.className = "chain-node-header";
    const title = document.createElement("strong");
    title.textContent = `Block ${record.blockID}`;
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

    const details = document.createElement("details");
    details.className = "chain-node-details";
    const detailsSummary = document.createElement("summary");
    detailsSummary.textContent = "Open block details";
    const detailsBody = document.createElement("div");
    detailsBody.className = "chain-node-details-body";
    appendNodeLine(detailsBody, "Harvest Date", record.batchHarvestDate);
    appendNodeLine(detailsBody, "Farm Location", record.batchFarmLocation);
    appendNodeLine(detailsBody, "Location Summary", record.locationSummary);

    const eventData = document.createElement("pre");
    eventData.className = "event-data";
    eventData.textContent = JSON.stringify(record.eventData || {}, null, 2);
    detailsBody.append(eventData);

    const cidText = record.ipfsRefs?.length
        ? record.ipfsRefs.map((reference) => `${reference.category}: ${reference.cid}`).join("\n")
        : "None";
    appendNodeLine(detailsBody, "CID References", cidText);
    appendNodeLine(detailsBody, "Parent Block", record.parentBlockId >= 0
        ? `Block ${record.parentBlockId}`
        : "Genesis");
    appendNodeLine(detailsBody, "Parent Hash", shortHash(record.parentBlockHash || "GENESIS"));
    details.append(detailsSummary, detailsBody);
    node.append(details);
    node.append(appendMerkleTree(record));
    return node;
}

function renderChain(batchId, nodes, edges) {
    const card = document.createElement("article");
    card.className = "record-card chain-card";

    const header = document.createElement("header");
    const title = document.createElement("h2");
    title.textContent = `Batch ${batchId}`;
    const badge = document.createElement("span");
    const completed = nodes.some((node) => node.chainStatus === "completed");
    badge.className = completed ? "badge verified" : "badge pending";
    badge.textContent = completed ? "Completed" : "In Progress";
    header.append(title, badge);

    const chainLabel = document.createElement("p");
    chainLabel.className = "chain-structure-label";
    chainLabel.textContent = "Linked Block Chain · each block contains an independent Merkle Tree";

    const flow = document.createElement("div");
    flow.className = "chain-flow";
    const sortedNodes = [...nodes].sort((left, right) => left.blockID - right.blockID);
    sortedNodes.forEach((node, index) => {
        if (index > 0) {
            const previous = sortedNodes[index - 1];
            const connected = edges.some((edge) =>
                edge.from === previous.blockID && edge.to === node.blockID);
            const arrow = document.createElement("span");
            arrow.className = "chain-arrow";
            arrow.textContent = connected ? "→" : "·";
            flow.append(arrow);
        }
        flow.append(renderChainNode(node));
    });

    const links = document.createElement("p");
    links.className = "chain-links";
    links.textContent = edges.length > 0
        ? `Connections: ${edges.map((edge) => `Block ${edge.from} → Block ${edge.to}`).join(" · ")}`
        : "Genesis block";

    card.append(header, chainLabel, flow, links);
    return card;
}

function renderChainGraph(graph) {
    const chains = new Map();
    for (const node of graph.nodes) {
        if (!chains.has(node.batchId)) chains.set(node.batchId, []);
        chains.get(node.batchId).push(node);
    }

    return [...chains.entries()].map(([batchId, nodes]) => {
        const blockIds = new Set(nodes.map((node) => node.blockID));
        const edges = graph.edges.filter((edge) =>
            blockIds.has(edge.from) && blockIds.has(edge.to));
        return renderChain(batchId, nodes, edges);
    });
}

function drawRoundedRect(context, x, y, width, height, radius) {
    const corner = Math.min(radius, width / 2, height / 2);
    context.beginPath();
    context.moveTo(x + corner, y);
    context.arcTo(x + width, y, x + width, y + height, corner);
    context.arcTo(x + width, y + height, x, y + height, corner);
    context.arcTo(x, y + height, x, y, corner);
    context.arcTo(x, y, x + width, y, corner);
    context.closePath();
}

function drawArrow(context, fromX, fromY, toX, toY) {
    const angle = Math.atan2(toY - fromY, toX - fromX);
    const size = 9;
    context.beginPath();
    context.moveTo(toX, toY);
    context.lineTo(
        toX - size * Math.cos(angle - Math.PI / 6),
        toY - size * Math.sin(angle - Math.PI / 6)
    );
    context.lineTo(
        toX - size * Math.cos(angle + Math.PI / 6),
        toY - size * Math.sin(angle + Math.PI / 6)
    );
    context.closePath();
    context.fill();
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

function normalizeWorkflow(workflow) {
    return {
        routeId: workflow.routeId || "",
        nodes: (workflow.nodes || []).map((node) => ({
            id: node.id,
            nodeType: node.nodeType || "transport",
            label: node.label || node.id,
            role: node.role || "logistics",
            username: node.username || "",
            x: Number.isFinite(Number(node.x)) ? Number(node.x) : 0,
            y: Number.isFinite(Number(node.y)) ? Number(node.y) : 0,
            stepIndex: Number.isFinite(Number(node.stepIndex))
                ? Number(node.stepIndex)
                : -1
        })),
        edges: (workflow.edges || []).map((edge) => ({
            from: edge.from,
            to: edge.to
        }))
    };
}

function workflowValidation(workflow) {
    const nodes = workflow.nodes || [];
    const edges = workflow.edges || [];
    if (nodes.length < 2) {
        return { valid: false, error: "A route needs at least a Supplier and a Supermarket." };
    }

    const byId = new Map();
    let supplier = null;
    let supermarket = null;
    for (const node of nodes) {
        if (!node.id || !node.label || byId.has(node.id)) {
            return { valid: false, error: "Route node IDs and labels must be unique and non-empty." };
        }
        if (!["supplier", "logistics", "warehouse", "supermarket"].includes(node.role)) {
            return { valid: false, error: "The route contains an unsupported node role." };
        }
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

function workflowCanvasLayout(workflow, width, force = false) {
    const ordered = routeOrder(workflow);
    const nodeWidth = 164;
    const nodeHeight = 96;
    const margin = 28;
    const columnGap = 34;
    const rowGap = 42;
    const columns = Math.max(
        1,
        Math.floor((width - margin * 2 + columnGap) / (nodeWidth + columnGap))
    );
    const hasSavedLayout = ordered.length > 0 && ordered.every((node) =>
        Number.isFinite(node.x) && Number.isFinite(node.y) &&
        (node.x !== 0 || node.y !== 0)
    );
    const savedLayoutFits = hasSavedLayout && ordered.every((node) =>
        node.x >= 0 && node.y >= 0 &&
        node.x + nodeWidth <= width && node.y + nodeHeight <= 600
    );

    if (force || !savedLayoutFits) {
        ordered.forEach((node, index) => {
            const column = index % columns;
            const row = Math.floor(index / columns);
            node.x = margin + column * (nodeWidth + columnGap);
            node.y = margin + row * (nodeHeight + rowGap);
            node.stepIndex = index;
        });
    }

    const requiredHeight = ordered.length === 0
        ? 250
        : Math.max(...ordered.map((node) => node.y + nodeHeight + margin), 250);
    return {
        positions: ordered.map((node) => ({
            node,
            x: node.x,
            y: node.y,
            width: nodeWidth,
            height: nodeHeight
        })),
        height: requiredHeight
    };
}

function workflowPoint(event) {
    const rect = workflowCanvas.getBoundingClientRect();
    const scale = window.devicePixelRatio || 1;
    return {
        x: (event.clientX - rect.left) * (workflowCanvas.width / scale) / rect.width,
        y: (event.clientY - rect.top) * (workflowCanvas.height / scale) / rect.height
    };
}

function workflowNodeAt(point) {
    return workflowCanvas._workflowPositions?.find((position) =>
        point.x >= position.x && point.x <= position.x + position.width &&
        point.y >= position.y && point.y <= position.y + position.height
    ) || null;
}

function distanceToSegment(point, start, end) {
    const dx = end.x - start.x;
    const dy = end.y - start.y;
    if (dx === 0 && dy === 0) {
        return Math.hypot(point.x - start.x, point.y - start.y);
    }
    const ratio = Math.max(0, Math.min(1,
        ((point.x - start.x) * dx + (point.y - start.y) * dy) / (dx * dx + dy * dy)
    ));
    return Math.hypot(
        point.x - (start.x + ratio * dx),
        point.y - (start.y + ratio * dy)
    );
}

function workflowEdgeAt(point) {
    if (!workflowData || !workflowCanvas._workflowPositions) return -1;
    const positionById = new Map(
        workflowCanvas._workflowPositions.map((position) => [position.node.id, position])
    );
    let closest = -1;
    let closestDistance = 10;
    workflowData.edges.forEach((edge, index) => {
        const from = positionById.get(edge.from);
        const to = positionById.get(edge.to);
        if (!from || !to) return;
        const distance = distanceToSegment(
            point,
            { x: from.x + from.width, y: from.y + from.height / 2 },
            { x: to.x, y: to.y + to.height / 2 }
        );
        if (distance < closestDistance) {
            closest = index;
            closestDistance = distance;
        }
    });
    return closest;
}

function workflowNodeForSelection() {
    return workflowData?.nodes.find((node) => node.id === workflowSelectedNodeId) || null;
}

function updateWorkflowEditor() {
    const selected = workflowNodeForSelection();
    workflowNodeEditor.hidden = !selected;
    if (selected) workflowNodeLabel.value = selected.label;
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

function drawWorkflowNode(context, position, selected) {
    const { node, x, y, width, height } = position;
    drawRoundedRect(context, x, y, width, height, 14);
    context.fillStyle = selected ? "#102c42" : "#0b1727";
    context.fill();
    context.strokeStyle = selected ? "#9acbff" : "#2d4960";
    context.lineWidth = selected ? 3 : 1.5;
    context.stroke();

    context.fillStyle = "#e5edf8";
    context.font = "700 15px system-ui, sans-serif";
    context.fillText(node.label, x + 13, y + 27);
    context.fillStyle = "#73cef4";
    context.font = "600 13px system-ui, sans-serif";
    context.fillText(node.role, x + 13, y + 50);
    context.fillStyle = "#91a2b9";
    context.font = "12px system-ui, sans-serif";
    context.fillText(node.username || "Unassigned", x + 13, y + 73);
    context.fillStyle = "#55d6b0";
    context.beginPath();
    context.arc(x + width - 16, y + 17, 5, 0, Math.PI * 2);
    context.fill();
}

function renderWorkflow(workflow) {
    workflowData = workflow === workflowData ? workflowData : normalizeWorkflow(workflow);
    const parentWidth = workflowCanvas.parentElement.clientWidth || 900;
    const width = Math.max(parentWidth, 640);
    const layout = workflowCanvasLayout(workflowData, width);
    const height = Math.max(250, layout.height);
    const scale = window.devicePixelRatio || 1;
    workflowCanvas.width = width * scale;
    workflowCanvas.height = height * scale;
    workflowCanvas.style.height = height + "px";

    const context = workflowCanvas.getContext("2d");
    context.setTransform(scale, 0, 0, scale, 0, 0);
    context.clearRect(0, 0, width, height);
    const positions = layout.positions;
    workflowCanvas._workflowPositions = positions;
    const positionById = new Map(
        positions.map((position) => [position.node.id, position])
    );

    context.lineWidth = 3;
    context.fillStyle = "#55d6b0";
    for (const [index, edge] of workflowData.edges.entries()) {
        const from = positionById.get(edge.from);
        const to = positionById.get(edge.to);
        if (!from || !to) continue;
        const fromX = from.x + from.width;
        const fromY = from.y + from.height / 2;
        const toX = to.x;
        const toY = to.y + to.height / 2;
        context.beginPath();
        context.moveTo(fromX, fromY);
        context.lineTo(toX - 10, toY);
        context.strokeStyle = index === workflowSelectedEdgeIndex ? "#9acbff" : "#55d6b0";
        context.lineWidth = index === workflowSelectedEdgeIndex ? 5 : 3;
        context.stroke();
        drawArrow(context, toX, toY, toX - 10, toY);
    }
    for (const position of positions) {
        drawWorkflowNode(
            context,
            position,
            position.node.id === workflowSelectedNodeId
        );
    }

    const validation = workflowValidation(workflowData);
    workflowRouteBadge.textContent = workflowData.routeId || "Route unavailable";
    workflowRouteBadge.className = "badge " + (validation.valid ? "verified" : "pending");
    workflowDeleteNode.disabled = !workflowSelectedNodeId;
    workflowDeleteEdge.disabled = workflowSelectedEdgeIndex < 0;
    workflowSave.disabled = !validation.valid;
    updateWorkflowEditor();
    workflowStatus.textContent = validation.valid
        ? `${workflowData.nodes.length} route node(s), ${workflowData.edges.length} connection(s). Drag nodes, click a line to remove it, or select two nodes to connect them.`
        : `Route error: ${validation.error}`;
    workflowStatus.className = "status " + (validation.valid ? "success" : "error");
}

function recalculateWorkflowLayout(force = true) {
    if (!workflowData) return;
    const width = Math.max(workflowCanvas.parentElement.clientWidth || 900, 640);
    const layout = workflowCanvasLayout(workflowData, width, force);
    for (const [index, node] of routeOrder(workflowData).entries()) {
        node.stepIndex = index;
    }
    return layout;
}

function workflowNodePayload(node) {
    return [
        node.id,
        node.nodeType,
        node.label,
        node.role,
        node.username,
        node.x,
        node.y,
        node.stepIndex
    ].map((value) => encodeURIComponent(String(value ?? ""))).join("|");
}

async function loadWorkflowScopes() {
    if (!session) return;
    try {
        const response = await fetch("/api/batches", {
            headers: { Authorization: "Bearer " + session.token }
        });
        const batches = await response.json();
        if (!response.ok) {
            throw new Error(batches.error || "Unable to load route scopes.");
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

async function saveWorkflow() {
    if (!session || !workflowData) return;
    const validation = workflowValidation(workflowData);
    if (!validation.valid) {
        workflowStatus.textContent = `Route error: ${validation.error}`;
        workflowStatus.className = "status error";
        workflowSave.disabled = true;
        return;
    }
    recalculateWorkflowLayout(false);
    workflowSave.disabled = true;
    workflowStatus.textContent = "Saving route...";
    workflowStatus.className = "status pending";
    const body = new URLSearchParams({
        batchId: workflowBatchId,
        nodes: workflowData.nodes.map(workflowNodePayload).join(";"),
        edges: workflowData.edges.map((edge) =>
            encodeURIComponent(edge.from) + "|" + encodeURIComponent(edge.to)
        ).join(";")
    });
    try {
        const response = await fetch("/api/workflow", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: "Bearer " + session.token
            },
            body: body.toString()
        });
        const result = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || "Unable to save route.");
        await loadWorkflowScopes();
        await loadWorkflow(workflowBatchId);
        workflowStatus.textContent = "Route saved for " +
            (workflowBatchId || "the default route") + ".";
        workflowStatus.className = "status success";
    } catch (error) {
        workflowStatus.textContent = error.message;
        workflowStatus.className = "status error";
    } finally {
        workflowSave.disabled = !workflowValidation(workflowData).valid;
    }
}

async function loadWorkflow(batchId = workflowBatchSelect.value) {
    if (!session) return;
    workflowBatchId = batchId || "";
    workflowStatus.textContent = "Loading preset route...";
    workflowStatus.className = "status pending";

    try {
        const query = workflowBatchId
            ? "?batchId=" + encodeURIComponent(workflowBatchId)
            : "";
        const response = await fetch("/api/workflow" + query, {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const workflow = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            throw new Error(workflow.error || `Request failed: ${response.status}`);
        }
        renderWorkflow(workflow);
    } catch (error) {
        workflowStatus.textContent = error.message;
        workflowStatus.className = "status error";
    }
}

async function loadChains() {
    if (!session) return;
    refreshButton.disabled = true;
    statusLine.textContent = "Loading supply-chain workflow...";
    statusLine.className = "status pending";

    try {
        const response = await fetch("/api/chains", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const graph = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            throw new Error(graph.error || `Request failed: ${response.status}`);
        }

        list.replaceChildren(...renderChainGraph(graph));
        statusLine.textContent = graph.nodes.length === 0
            ? "No supply-chain workflow yet."
            : `Loaded ${graph.nodes.length} node(s) across ${new Set(graph.nodes.map((node) => node.batchId)).size} batch(es).`;
        statusLine.className = "status success";
    } catch (error) {
        list.replaceChildren();
        statusLine.textContent = error.message;
        statusLine.className = "status error";
    } finally {
        refreshButton.disabled = false;
    }
}

function setSnapshotStatus(message, state = "") {
    snapshotStatus.textContent = message;
    snapshotStatus.className = state ? `status ${state}` : "status";
}

function selectedSnapshotBatch() {
    return snapshotBatches.find((batch) => batch.batchId === snapshotBatchSelect.value);
}

function renderSnapshotEvidence() {
    snapshotEvidenceList.replaceChildren();
    snapshotPreview.hidden = true;
    publicationCandidate = null;
    publishSnapshotButton.disabled = true;
    snapshotPublishStatus.textContent = "";
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
        input.checked = Boolean(item.selectedByDefault);
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
    snapshotBatches = batches;
    snapshotBatchSelect.replaceChildren();
    for (const batch of batches) {
        const option = document.createElement("option");
        option.value = batch.batchId;
        option.textContent = `${batch.batchId} · ${batch.product}`;
        snapshotBatchSelect.append(option);
    }

    const hasBatches = batches.length > 0;
    snapshotBatchSelect.disabled = !hasBatches;
    generateSnapshotButton.disabled = !hasBatches;
    snapshotBatchCount.textContent = hasBatches
        ? `${batches.length} eligible batch${batches.length === 1 ? "" : "es"}`
        : "No eligible batches";
    snapshotBatchCount.className = hasBatches ? "badge verified" : "badge pending";
    renderSnapshotEvidence();
    setSnapshotStatus(hasBatches
        ? "Select a completed batch and review its public evidence."
        : "Complete and verify all four route stages before generating a snapshot.",
        hasBatches ? "success" : "pending");
}

async function loadSnapshotCandidates() {
    if (!session) return;
    snapshotBatchCount.textContent = "Loading batches";
    snapshotBatchCount.className = "badge pending";
    generateSnapshotButton.disabled = true;
    try {
        const response = await fetch("/api/snapshot/eligible-batches", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const result = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        renderSnapshotBatches(result.batches || []);
    } catch (error) {
        renderSnapshotBatches([]);
        setSnapshotStatus(error.message, "error");
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
    publishSnapshotButton.disabled = !publicationCandidate;
    snapshotPublishStatus.textContent = publicationCandidate
        ? "Preview ready for administrator publication."
        : "Publication data is unavailable.";
    snapshotPublishStatus.className = publicationCandidate
        ? "status pending"
        : "status error";
    snapshotPreview.hidden = false;
}

async function publishSnapshot() {
    if (!session || !publicationCandidate) return;
    publishSnapshotButton.disabled = true;
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
        const result = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) throw new Error(result.error || `Request failed: ${response.status}`);
        snapshotPublishStatus.textContent = `Published in block ${result.blockNumber}.`;
        snapshotPublishStatus.className = "status success";
    } catch (error) {
        snapshotPublishStatus.textContent = error.message;
        snapshotPublishStatus.className = "status error";
        publishSnapshotButton.disabled = false;
    }
}

async function generateSnapshotPreview(event) {
    event.preventDefault();
    if (!session || !snapshotBatchSelect.value) return;
    const selectedEvidence = [...snapshotEvidenceList.querySelectorAll(
        'input[name="selectedEvidence"]:checked'
    )].map((input) => input.value);

    generateSnapshotButton.disabled = true;
    snapshotPreview.hidden = true;
    publicationCandidate = null;
    publishSnapshotButton.disabled = true;
    snapshotPublishStatus.textContent = "";
    setSnapshotStatus("Generating a private, non-published preview...", "pending");
    try {
        const response = await fetch("/api/snapshot/preview", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: `Bearer ${session.token}`
            },
            body: new URLSearchParams({
                batchId: snapshotBatchSelect.value,
                selectedEvidence: selectedEvidence.join(",")
            }).toString()
        });
        const result = await response.json();
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
        generateSnapshotButton.disabled = snapshotBatches.length === 0;
    }
}

async function loadDashboard() {
    await loadWorkflowScopes();
    await Promise.all([
        loadWorkflow(workflowBatchSelect.value),
        loadChains(),
        loadConfirmationPolicy(),
        loadSnapshotCandidates()
    ]);
}

window.addEventListener("resize", () => {
    if (workflowData) renderWorkflow(workflowData);
});

refreshButton.addEventListener("click", loadDashboard);
loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", logout);
workflowBatchSelect.addEventListener("change", () => {
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    loadWorkflow(workflowBatchSelect.value);
});
workflowCanvas.addEventListener("pointerdown", (event) => {
    if (!workflowData || !workflowCanvas._workflowPositions) return;
    const point = workflowPoint(event);
    const position = workflowNodeAt(point);
    workflowSelectedEdgeIndex = -1;

    if (position) {
        if (workflowSelectedNodeId && workflowSelectedNodeId !== position.node.id) {
            const error = workflowConnectionError(
                workflowSelectedNodeId,
                position.node.id
            );
            if (error) {
                renderWorkflow(workflowData);
                workflowStatus.textContent = `Route error: ${error}`;
                workflowStatus.className = "status error";
                return;
            }
            workflowData.edges.push({
                from: workflowSelectedNodeId,
                to: position.node.id
            });
            workflowSelectedNodeId = position.node.id;
            workflowPointerState = null;
            renderWorkflow(workflowData);
            return;
        }

        workflowSelectedNodeId = position.node.id;
        workflowPointerState = {
            pointerId: event.pointerId,
            nodeId: position.node.id,
            startX: point.x,
            startY: point.y,
            offsetX: point.x - position.x,
            offsetY: point.y - position.y,
            dragging: false
        };
        workflowCanvas.setPointerCapture(event.pointerId);
        workflowCanvas.style.cursor = "grabbing";
        renderWorkflow(workflowData);
        return;
    }

    const edgeIndex = workflowEdgeAt(point);
    if (edgeIndex >= 0) {
        workflowSelectedNodeId = "";
        workflowSelectedEdgeIndex = edgeIndex;
        renderWorkflow(workflowData);
        return;
    }

    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    renderWorkflow(workflowData);
});
workflowCanvas.addEventListener("pointermove", (event) => {
    if (!workflowData || !workflowPointerState ||
        workflowPointerState.pointerId !== event.pointerId) return;

    const point = workflowPoint(event);
    const moved = Math.hypot(
        point.x - workflowPointerState.startX,
        point.y - workflowPointerState.startY
    );
    if (!workflowPointerState.dragging && moved < 4) return;
    workflowPointerState.dragging = true;

    const node = workflowData.nodes.find((item) =>
        item.id === workflowPointerState.nodeId
    );
    if (!node) return;
    const layout = workflowCanvas._workflowPositions?.[0];
    const nodeWidth = layout?.width || 164;
    const nodeHeight = layout?.height || 96;
    const scale = window.devicePixelRatio || 1;
    const canvasWidth = workflowCanvas.width / scale;
    const canvasHeight = workflowCanvas.height / scale;
    node.x = Math.max(
        12,
        Math.min(Math.max(12, canvasWidth - nodeWidth - 12), point.x - workflowPointerState.offsetX)
    );
    node.y = Math.max(
        12,
        Math.min(Math.max(12, canvasHeight - nodeHeight - 12), point.y - workflowPointerState.offsetY)
    );
    renderWorkflow(workflowData);
    event.preventDefault();
});
function finishWorkflowPointer(event) {
    if (!workflowPointerState || workflowPointerState.pointerId !== event.pointerId) return;
    if (workflowCanvas.hasPointerCapture(event.pointerId)) {
        workflowCanvas.releasePointerCapture(event.pointerId);
    }
    workflowPointerState = null;
    workflowCanvas.style.cursor = "crosshair";
    renderWorkflow(workflowData);
}
workflowCanvas.addEventListener("pointerup", finishWorkflowPointer);
workflowCanvas.addEventListener("pointercancel", finishWorkflowPointer);
workflowDeleteEdge.addEventListener("click", () => {
    if (!workflowData || workflowSelectedEdgeIndex < 0) return;
    workflowData.edges.splice(workflowSelectedEdgeIndex, 1);
    workflowSelectedEdgeIndex = -1;
    renderWorkflow(workflowData);
});
workflowAutoLayout.addEventListener("click", () => {
    if (!workflowData) return;
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    recalculateWorkflowLayout(true);
    renderWorkflow(workflowData);
});
workflowApplyNodeLabel.addEventListener("click", () => {
    const selected = workflowNodeForSelection();
    const label = workflowNodeLabel.value.trim();
    if (!selected) return;
    if (!label) {
        workflowStatus.textContent = "Route error: the node label cannot be empty.";
        workflowStatus.className = "status error";
        return;
    }
    selected.label = label;
    renderWorkflow(workflowData);
});
workflowAddNode.addEventListener("click", () => {
    if (!workflowData) return;
    const type = workflowNodeType.value;
    const role = type === "warehouse" ? "warehouse" : "logistics";
    const prefix = type === "warehouse" ? "warehouse" : "transport";
    let count = workflowData.nodes.filter((node) => node.nodeType === type).length + 1;
    let nodeId = prefix + "-" + count;
    while (workflowData.nodes.some((node) => node.id === nodeId)) {
        nodeId = prefix + "-" + (++count);
    }
    workflowData.nodes.push({
        id: nodeId,
        nodeType: type,
        label: (type === "warehouse" ? "Warehouse " : "Transport ") + count,
        role,
        username: role === "warehouse" ? "warehouse01" : "logistics01",
        x: 0,
        y: 0,
        stepIndex: -1
    });
    workflowSelectedNodeId = nodeId;
    workflowSelectedEdgeIndex = -1;
    recalculateWorkflowLayout();
    renderWorkflow(workflowData);
});
workflowDeleteNode.addEventListener("click", () => {
    if (!workflowData || !workflowSelectedNodeId) return;
    const selected = workflowData.nodes.find((node) =>
        node.id === workflowSelectedNodeId
    );
    if (!selected || selected.role === "supplier" || selected.role === "supermarket") {
        workflowStatus.textContent = "Supplier and Supermarket are required route endpoints.";
        workflowStatus.className = "status error";
        return;
    }
    workflowData.nodes = workflowData.nodes.filter((node) =>
        node.id !== workflowSelectedNodeId
    );
    workflowData.edges = workflowData.edges.filter((edge) =>
        edge.from !== workflowSelectedNodeId && edge.to !== workflowSelectedNodeId
    );
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    recalculateWorkflowLayout();
    renderWorkflow(workflowData);
});
workflowResetRoute.addEventListener("click", () => {
    workflowSelectedNodeId = "";
    workflowSelectedEdgeIndex = -1;
    workflowPointerState = null;
    loadWorkflow(workflowBatchSelect.value);
});
workflowSave.addEventListener("click", saveWorkflow);
confirmationPolicyForm.addEventListener("submit", saveConfirmationPolicy);
snapshotBatchSelect.addEventListener("change", renderSnapshotEvidence);
snapshotPreviewForm.addEventListener("submit", generateSnapshotPreview);
publishSnapshotButton.addEventListener("click", publishSnapshot);
restoreSession();
