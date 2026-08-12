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
const sessionKey = "supply-chain-control-session";
let session = null;
let workflowData = null;

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

function renderWorkflow(workflow) {
    workflowData = workflow;
    const parentWidth = workflowCanvas.parentElement.clientWidth || 900;
    const width = Math.max(parentWidth, 760);
    const height = 240;
    const scale = window.devicePixelRatio || 1;
    workflowCanvas.width = width * scale;
    workflowCanvas.height = height * scale;
    workflowCanvas.style.height = `${height}px`;

    const context = workflowCanvas.getContext("2d");
    context.setTransform(scale, 0, 0, scale, 0, 0);
    context.clearRect(0, 0, width, height);

    const nodeWidth = 152;
    const nodeHeight = 92;
    const margin = 36;
    const gap = workflow.nodes.length > 1
        ? (width - margin * 2 - nodeWidth * workflow.nodes.length) /
          (workflow.nodes.length - 1)
        : 0;
    const positions = workflow.nodes.map((node, index) => ({
        node,
        x: margin + index * (nodeWidth + gap),
        y: (height - nodeHeight) / 2
    }));

    context.lineWidth = 3;
    context.strokeStyle = "#55d6b0";
    context.fillStyle = "#55d6b0";
    for (const edge of workflow.edges) {
        const from = positions.find((item) => item.node.id === edge.from);
        const to = positions.find((item) => item.node.id === edge.to);
        if (!from || !to) continue;
        const fromX = from.x + nodeWidth;
        const fromY = from.y + nodeHeight / 2;
        const toX = to.x;
        const toY = to.y + nodeHeight / 2;
        context.beginPath();
        context.moveTo(fromX, fromY);
        context.lineTo(toX - 10, toY);
        context.stroke();
        drawArrow(context, toX, toY, toX - 10, toY);
    }

    for (const position of positions) {
        const { node, x, y } = position;
        drawRoundedRect(context, x, y, nodeWidth, nodeHeight, 14);
        context.fillStyle = "#0b1727";
        context.fill();
        context.strokeStyle = "#2d4960";
        context.lineWidth = 1.5;
        context.stroke();

        context.fillStyle = "#e5edf8";
        context.font = "700 16px system-ui, sans-serif";
        context.fillText(node.label, x + 14, y + 28);
        context.fillStyle = "#73cef4";
        context.font = "600 13px system-ui, sans-serif";
        context.fillText(node.role, x + 14, y + 51);
        context.fillStyle = "#91a2b9";
        context.font = "12px system-ui, sans-serif";
        context.fillText(node.username, x + 14, y + 73);
    }

    workflowStatus.textContent = `${workflow.nodes.length} preset stages connected.`;
    workflowStatus.className = "status success";
}

async function loadWorkflow() {
    if (!session) return;
    workflowStatus.textContent = "Loading preset route...";
    workflowStatus.className = "status pending";

    try {
        const response = await fetch("/api/workflow", {
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

async function loadDashboard() {
    await Promise.all([loadWorkflow(), loadChains()]);
}

window.addEventListener("resize", () => {
    if (workflowData) renderWorkflow(workflowData);
});

refreshButton.addEventListener("click", loadDashboard);
loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", logout);
restoreSession();
