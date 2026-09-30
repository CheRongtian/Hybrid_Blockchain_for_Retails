import { computed, onBeforeUnmount, onMounted, reactive, watch } from "vue";

const SESSION_KEY = "supply-chain-control-session";
const NODE_WIDTH = 184;
const NODE_HEIGHT = 108;
const NODE_GAP = 88;
const ROUTE_MARGIN = 72;

const roleLabels = {
  supplier: "Supplier",
  logistics: "Logistics",
  warehouse: "Warehouse",
  supermarket: "Supermarket",
};

const clone = (value) => JSON.parse(JSON.stringify(value));
const integer = (value, fallback = 0) => {
  const numeric = Number(value);
  return Number.isFinite(numeric) ? Math.round(numeric) : fallback;
};

const readJson = async (response) => {
  const text = await response.text();
  try {
    return text ? JSON.parse(text) : {};
  } catch {
    throw new Error("The control server returned invalid JSON. Rebuild and restart it.");
  }
};

const normalizeWorkflow = (workflow = {}) => ({
  routeId: workflow.routeId || "",
  accounts: Array.isArray(workflow.accounts)
    ? workflow.accounts.map((account) => ({ ...account }))
    : [],
  nodes: Array.isArray(workflow.nodes)
    ? workflow.nodes.map((node) => ({
        id: node.id,
        nodeType: node.nodeType || "transport",
        label: node.label || node.id,
        role: node.role || "logistics",
        username: node.username || "",
        x: integer(node.x),
        y: integer(node.y),
        stepIndex: integer(node.stepIndex, -1),
      }))
    : [],
  edges: Array.isArray(workflow.edges)
    ? workflow.edges.map((edge) => ({ from: edge.from, to: edge.to }))
    : [],
});

const routeOrder = (workflow) => {
  const nodes = workflow?.nodes || [];
  const byId = new Map(nodes.map((node) => [node.id, node]));
  const outgoing = new Map((workflow?.edges || []).map((edge) => [edge.from, edge.to]));
  const ordered = [];
  const visited = new Set();
  let current = nodes.find((node) => node.role === "supplier");
  while (current && !visited.has(current.id)) {
    ordered.push(current);
    visited.add(current.id);
    current = byId.get(outgoing.get(current.id));
  }
  nodes.forEach((node) => {
    if (!visited.has(node.id)) ordered.push(node);
  });
  return ordered;
};

const validateWorkflow = (workflow) => {
  const nodes = workflow?.nodes || [];
  const edges = workflow?.edges || [];
  if (nodes.length < 2) return { valid: false, error: "A route needs a Supplier and a Supermarket." };

  const byId = new Map();
  const assigned = new Set();
  const suppliers = nodes.filter((node) => node.role === "supplier");
  const supermarkets = nodes.filter((node) => node.role === "supermarket");
  if (suppliers.length !== 1 || supermarkets.length !== 1) {
    return { valid: false, error: "A route must have exactly one Supplier and one Supermarket." };
  }
  for (const node of nodes) {
    if (!node.id || !node.label || byId.has(node.id)) {
      return { valid: false, error: "Route node IDs and labels must be unique and non-empty." };
    }
    if (!node.username) return { valid: false, error: `${node.label} needs an assigned account.` };
    if (assigned.has(node.username)) {
      return { valid: false, error: `${node.username} is already assigned to another route stage.` };
    }
    assigned.add(node.username);
    byId.set(node.id, node);
  }

  const incoming = new Map();
  const outgoing = new Map();
  const edgeKeys = new Set();
  for (const edge of edges) {
    const key = `${edge.from}\u0000${edge.to}`;
    if (!byId.has(edge.from) || !byId.has(edge.to) || edge.from === edge.to) {
      return { valid: false, error: "Every connection must join two different route nodes." };
    }
    if (edgeKeys.has(key)) return { valid: false, error: "Duplicate connections are not allowed." };
    if (outgoing.has(edge.from)) {
      return { valid: false, error: "Each route node can have only one outgoing connection." };
    }
    if (incoming.has(edge.to)) {
      return { valid: false, error: "Each route node can have only one incoming connection." };
    }
    edgeKeys.add(key);
    outgoing.set(edge.from, edge.to);
    incoming.set(edge.to, edge.from);
  }

  const supplier = suppliers[0];
  const supermarket = supermarkets[0];
  if (incoming.has(supplier.id) || outgoing.has(supermarket.id)) {
    return { valid: false, error: "Supplier must start the route and Supermarket must end it." };
  }
  const visited = new Set();
  let current = supplier.id;
  while (current) {
    if (visited.has(current)) return { valid: false, error: "A route cannot contain a cycle." };
    visited.add(current);
    if (current === supermarket.id) break;
    current = outgoing.get(current);
    if (!current) return { valid: false, error: "Every route node must lead to the Supermarket." };
  }
  if (visited.size !== nodes.length) {
    return { valid: false, error: "Every route node must be connected to the same route." };
  }
  return { valid: true, error: "" };
};

const formatInterval = (seconds) => {
  const value = Number(seconds);
  if (!Number.isInteger(value) || value <= 0) return "Unknown interval";
  const units = [
    { seconds: 86400, label: "day" },
    { seconds: 3600, label: "hour" },
    { seconds: 60, label: "minute" },
  ];
  const unit = units.find((item) => value % item.seconds === 0) || units[2];
  const amount = unit.seconds === 60 ? Math.max(1, Math.ceil(value / 60)) : value / unit.seconds;
  return `${amount} ${unit.label}${amount === 1 ? "" : "s"}`;
};

const intervalEditor = (seconds) => {
  const value = Number(seconds);
  const units = [
    { seconds: 86400, value: "days" },
    { seconds: 3600, value: "hours" },
    { seconds: 60, value: "minutes" },
  ];
  const unit = units.find((item) => Number.isInteger(value) && value % item.seconds === 0) || units[2];
  return reactive({
    value: Math.max(1, Number.isInteger(value) ? Math.ceil(value / unit.seconds) : 1),
    unit: unit.value,
  });
};

const localDateTime = (isoValue) => {
  if (!isoValue) return "";
  const date = new Date(isoValue);
  if (Number.isNaN(date.getTime())) return "";
  const pad = (part) => String(part).padStart(2, "0");
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}` +
    `T${pad(date.getHours())}:${pad(date.getMinutes())}`;
};

export function useControlCenter() {
  const state = reactive({
    session: null,
    auth: { username: "", password: "", remember: false, busy: false, status: "", kind: "" },
    scopes: [],
    selectedScope: "",
    workflow: null,
    selectedNodeId: "",
    selectedEdgeIndex: -1,
    nodeType: "transport",
    nodeAccount: "",
    connectFrom: "",
    connectTo: "",
    workflowStatus: "Loading route…",
    workflowStatusKind: "pending",
    routeDirty: false,
    routeSaving: false,
    history: { past: [], future: [] },
    chain: { nodes: [], edges: [] },
    recordsStatus: "Loading supply-chain workflow…",
    recordsStatusKind: "pending",
    policies: [],
    policyBusy: false,
    policyStatus: "",
    policyStatusKind: "",
    snapshot: {
      candidates: [],
      selectedBatchId: "",
      selectedEvidence: [],
      refreshPolicies: [],
      availabilityWindows: [],
      defaultIntervalSeconds: 3600,
      schedulesAvailable: false,
      refreshValue: 1,
      refreshUnit: "hours",
      availableFrom: "",
      availableUntil: "",
      scheduleStatus: "",
      scheduleStatusKind: "",
      status: "",
      statusKind: "",
      preview: null,
      publicationCandidate: null,
      operation: "",
      publishStatus: "",
      publishStatusKind: "",
      published: false,
    },
  });

  let liveEvents = null;
  let saveTimer = 0;
  let refreshTimer = 0;
  let workflowRequest = 0;
  let liveRefreshInFlight = false;
  let pendingLiveRefreshType = "";
  let snapshotEditorProduct = "";
  let snapshotEditorBatchId = "";

  const identity = computed(() => state.session
    ? `${state.session.user.username} · ${state.session.user.role} · ${state.session.user.organizationId}`
    : "");
  const workflowValidation = computed(() => validateWorkflow(state.workflow));
  const orderedNodes = computed(() => routeOrder(state.workflow));
  const selectedNode = computed(() => state.workflow?.nodes.find((node) => node.id === state.selectedNodeId) || null);
  const selectedBatch = computed(() => state.snapshot.candidates.find((batch) =>
    batch.batchId === state.snapshot.selectedBatchId) || null);
  const evidence = computed(() => selectedBatch.value?.evidence || []);
  const nodeAccounts = computed(() => {
    const role = state.nodeType === "warehouse" ? "warehouse" : "logistics";
    const used = new Set((state.workflow?.nodes || []).map((node) => node.username));
    return (state.workflow?.accounts || []).filter((account) =>
      account.active !== false && account.role === role && !used.has(account.username));
  });
  const connectionSources = computed(() => (state.workflow?.nodes || []).filter((node) =>
    node.role !== "supermarket" &&
    !state.workflow.edges.some((edge) => edge.from === node.id)));
  const connectionTargets = computed(() => (state.workflow?.nodes || []).filter((node) =>
    node.role !== "supplier" && node.id !== state.connectFrom &&
    !state.workflow.edges.some((edge) => edge.to === node.id)));
  const recordBatches = computed(() => {
    const groups = new Map();
    for (const record of state.chain.nodes || []) {
      const batchId = record.batchId || "Unassigned";
      if (!groups.has(batchId)) groups.set(batchId, []);
      groups.get(batchId).push(record);
    }
    for (const scope of state.scopes) {
      if (!groups.has(scope.batchId)) groups.set(scope.batchId, []);
    }
    if (state.selectedScope && !groups.has(state.selectedScope)) groups.set(state.selectedScope, []);
    return [...groups.entries()]
      .filter(([batchId]) => !state.selectedScope || batchId === state.selectedScope)
      .map(([batchId, records]) => {
        const scope = state.scopes.find((item) => item.batchId === batchId);
        const orderedRecords = records.sort((left, right) => {
          const leftStep = Number(left.routeStepIndex);
          const rightStep = Number(right.routeStepIndex);
          if (leftStep >= 0 && rightStep >= 0 && leftStep !== rightStep) return leftStep - rightStep;
          return Number(left.blockID) - Number(right.blockID);
        });
        const pendingNodes = Array.isArray(scope?.pendingRouteNodes) ? scope.pendingRouteNodes : [];
        const recordsVerified = orderedRecords.length > 0 && orderedRecords.every((record) =>
          record.verified === true && record.signatureVerified === true);
        return {
          batchId,
          product: scope?.product || orderedRecords[0]?.product || "",
          records: orderedRecords,
          pendingNodes,
          routeError: scope?.routeError || "",
          complete: Boolean(scope?.routeReady || scope?.status === "completed" ||
            (recordsVerified && pendingNodes.length === 0)),
        };
      });
  });

  const clearSession = () => {
    liveEvents?.close();
    liveEvents = null;
    if (saveTimer) window.clearTimeout(saveTimer);
    if (refreshTimer) window.clearTimeout(refreshTimer);
    liveRefreshInFlight = false;
    pendingLiveRefreshType = "";
    snapshotEditorProduct = "";
    snapshotEditorBatchId = "";
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
    state.session = null;
    state.workflow = null;
    state.scopes = [];
    state.selectedScope = "";
    state.chain = { nodes: [], edges: [] };
    state.policies = [];
    state.snapshot.candidates = [];
    state.snapshot.preview = null;
    state.snapshot.publicationCandidate = null;
  };

  const request = async (url, options = {}) => {
    const headers = new Headers(options.headers || {});
    if (state.session?.token) headers.set("Authorization", `Bearer ${state.session.token}`);
    const response = await fetch(url, { ...options, headers });
    const payload = await readJson(response);
    if (response.status === 401 || response.status === 403) {
      clearSession();
      throw new Error("Control-panel session expired or insufficient permissions.");
    }
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
    return payload;
  };

  const saveSession = (result) => {
    sessionStorage.removeItem(SESSION_KEY);
    localStorage.removeItem(SESSION_KEY);
    (state.auth.remember ? localStorage : sessionStorage).setItem(SESSION_KEY, JSON.stringify(result));
  };

  const login = async () => {
    if (state.auth.busy) return;
    state.auth.busy = true;
    state.auth.status = "Authenticating administrator…";
    state.auth.kind = "pending";
    try {
      const response = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: new URLSearchParams({
          username: state.auth.username,
          password: state.auth.password,
          remember: state.auth.remember ? "true" : "false",
        }).toString(),
      });
      const result = await readJson(response);
      if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);
      if (result.user?.role !== "admin") throw new Error("This account does not have control-panel access.");
      state.session = result;
      saveSession(result);
      state.auth.password = "";
      await loadDashboard();
      startLiveUpdates();
    } catch (error) {
      state.auth.status = error.message;
      state.auth.kind = "error";
    } finally {
      state.auth.busy = false;
    }
  };

  const restoreSession = async () => {
    const saved = localStorage.getItem(SESSION_KEY) || sessionStorage.getItem(SESSION_KEY);
    if (!saved) return;
    try {
      const stored = JSON.parse(saved);
      const response = await fetch("/api/auth/me", {
        headers: { Authorization: `Bearer ${stored.token}` },
      });
      const user = await readJson(response);
      if (!response.ok || user.role !== "admin") throw new Error("Session expired");
      state.session = { token: stored.token, user };
      await loadDashboard();
      startLiveUpdates();
    } catch {
      clearSession();
    }
  };

  const logout = async () => {
    const token = state.session?.token;
    if (token) {
      try {
        await fetch("/api/auth/logout", {
          method: "POST",
          headers: { Authorization: `Bearer ${token}` },
        });
      } catch {
        // Local session removal still succeeds when the server is unavailable.
      }
    }
    clearSession();
  };

  const ensureLayout = (workflow, force = false) => {
    if (!workflow) return;
    const ordered = routeOrder(workflow);
    const hasLayout = ordered.some((node) => node.x !== 0 || node.y !== 0);
    if (force || !hasLayout) {
      ordered.forEach((node, index) => {
        node.x = ROUTE_MARGIN + index * (NODE_WIDTH + NODE_GAP);
        node.y = 106;
        node.stepIndex = index;
      });
    } else {
      ordered.forEach((node, index) => { node.stepIndex = index; });
    }
  };

  const workflowSnapshot = () => state.workflow ? JSON.stringify(state.workflow) : "";
  const captureHistory = () => {
    const snapshot = workflowSnapshot();
    if (!snapshot) return;
    state.history.past.push(snapshot);
    state.history.past = state.history.past.slice(-40);
    state.history.future = [];
  };

  const restoreWorkflow = (snapshot) => {
    if (!snapshot) return;
    state.workflow = normalizeWorkflow(JSON.parse(snapshot));
    state.selectedNodeId = "";
    state.selectedEdgeIndex = -1;
    markWorkflowChanged("Workflow history changed. Synchronizing the route draft…");
  };

  const undo = () => {
    if (!state.workflow || state.history.past.length === 0) return;
    const current = workflowSnapshot();
    const previous = state.history.past.pop();
    state.history.future.push(current);
    restoreWorkflow(previous);
  };
  const redo = () => {
    if (!state.workflow || state.history.future.length === 0) return;
    const current = workflowSnapshot();
    const next = state.history.future.pop();
    state.history.past.push(current);
    restoreWorkflow(next);
  };

  const workflowNodePayload = (node) => [
    node.id, node.nodeType, node.label, node.role, node.username,
    integer(node.x), integer(node.y), integer(node.stepIndex, -1),
  ].map((value) => encodeURIComponent(String(value ?? ""))).join("|");

  const workflowBody = () => new URLSearchParams({
    batchId: state.selectedScope,
    nodes: (state.workflow?.nodes || []).map(workflowNodePayload).join(";"),
    edges: (state.workflow?.edges || []).map((edge) =>
      `${encodeURIComponent(edge.from)}|${encodeURIComponent(edge.to)}`).join(";"),
    draft: "true",
  });

  const clearPreview = (message = "") => {
    state.snapshot.preview = null;
    state.snapshot.publicationCandidate = null;
    state.snapshot.published = false;
    state.snapshot.publishStatus = "";
    state.snapshot.publishStatusKind = "";
    if (message) {
      state.snapshot.status = message;
      state.snapshot.statusKind = "pending";
    }
  };

  function markWorkflowChanged(message = "Route changed. Synchronizing the route draft…") {
    state.routeDirty = true;
    state.workflowStatus = message;
    state.workflowStatusKind = "pending";
    state.snapshot.candidates = [];
    clearPreview("Route changed. The previous Snapshot is no longer valid.");
    if (saveTimer) window.clearTimeout(saveTimer);
    saveTimer = window.setTimeout(syncWorkflow, 250);
  }

  const syncWorkflow = async () => {
    if (!state.session || !state.workflow || state.routeSaving) return;
    ensureLayout(state.workflow);
    state.routeSaving = true;
    state.workflowStatus = "Synchronizing route draft…";
    state.workflowStatusKind = "pending";
    const sentScope = state.selectedScope;
    const sentSnapshot = workflowSnapshot();
    try {
      const result = await request("/api/workflow", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: workflowBody().toString(),
      });
      if (state.selectedScope === sentScope && workflowSnapshot() === sentSnapshot) {
        state.workflow.routeId = result.routeId || state.workflow.routeId;
        state.routeDirty = false;
        state.workflowStatus = "Route draft synchronized automatically.";
        state.workflowStatusKind = "success";
        await Promise.all([loadWorkflowView(sentScope, true), loadSnapshotCandidates()]);
      }
    } catch (error) {
      state.workflowStatus = error.message;
      state.workflowStatusKind = "error";
      if (state.session && state.routeDirty) {
        saveTimer = window.setTimeout(syncWorkflow, 1500);
      }
    } finally {
      state.routeSaving = false;
      flushPendingLiveRefresh();
    }
  };

  const loadScopes = async () => {
    try {
      const batches = await request("/api/batches");
      if (!Array.isArray(batches)) throw new Error("The route scope response is malformed.");
      state.scopes = batches;
      if (!batches.some((batch) => batch.batchId === state.selectedScope)) state.selectedScope = "";
    } catch (error) {
      state.workflowStatus = error.message;
      state.workflowStatusKind = "error";
    }
  };

  const loadWorkflowView = async (scope = state.selectedScope, discardLocal = false) => {
    if (!state.session) return false;
    const requestId = ++workflowRequest;
    state.selectedScope = scope || "";
    state.workflowStatus = "Loading route…";
    state.workflowStatusKind = "pending";
    state.recordsStatus = "Loading supply-chain workflow…";
    state.recordsStatusKind = "pending";
    try {
      const suffix = state.selectedScope ? `?batchId=${encodeURIComponent(state.selectedScope)}` : "";
      const view = await request(`/api/workflow-view${suffix}`);
      if (requestId !== workflowRequest || (!discardLocal && state.routeDirty)) return false;
      if (!view.workflow || !Array.isArray(view.workflow.nodes) || !view.chain || !Array.isArray(view.chain.nodes)) {
        throw new Error("The workflow response is malformed.");
      }
      const workflow = normalizeWorkflow(view.workflow);
      ensureLayout(workflow);
      state.workflow = workflow;
      state.chain = view.chain;
      state.routeDirty = false;
      state.selectedNodeId = "";
      state.selectedEdgeIndex = -1;
      state.history = { past: [], future: [] };
      const batchCount = new Set(view.chain.nodes.map((node) => node.batchId).filter(Boolean)).size;
      state.recordsStatus = view.chain.nodes.length
        ? `Loaded ${view.chain.nodes.length} node(s) across ${batchCount} batch(es).`
        : "No supply-chain workflow yet.";
      state.recordsStatusKind = "success";
      const validation = validateWorkflow(workflow);
      state.workflowStatus = validation.valid
        ? `${workflow.nodes.length} route node(s), ${workflow.edges.length} connection(s).`
        : `Route error: ${validation.error}`;
      state.workflowStatusKind = validation.valid ? "success" : "error";
      selectDefaultControls();
      await loadPolicies();
      return true;
    } catch (error) {
      if (requestId === workflowRequest) {
        state.workflowStatus = error.message;
        state.workflowStatusKind = "error";
        state.recordsStatus = error.message;
        state.recordsStatusKind = "error";
      }
      return false;
    }
  };

  const selectDefaultControls = () => {
    state.nodeAccount = nodeAccounts.value[0]?.username || "";
    state.connectFrom = connectionSources.value[0]?.id || "";
    state.connectTo = connectionTargets.value[0]?.id || "";
  };

  const selectScope = async (scope) => {
    if (saveTimer) window.clearTimeout(saveTimer);
    state.routeDirty = false;
    state.selectedNodeId = "";
    state.selectedEdgeIndex = -1;
    await loadWorkflowView(scope, true);
  };

  const setNodeType = (type) => {
    state.nodeType = type;
    state.nodeAccount = nodeAccounts.value[0]?.username || "";
  };

  const addNode = () => {
    if (!state.workflow) return;
    const role = state.nodeType === "warehouse" ? "warehouse" : "logistics";
    const account = state.nodeAccount || nodeAccounts.value[0]?.username;
    if (!account) {
      state.workflowStatus = `No active ${roleLabels[role]} account is available.`;
      state.workflowStatusKind = "error";
      return;
    }
    captureHistory();
    const sameType = state.workflow.nodes.filter((node) => node.nodeType === state.nodeType).length + 1;
    const prefix = state.nodeType === "warehouse" ? "warehouse" : "transport";
    let sequence = sameType;
    let id = `${prefix}-${sequence}`;
    while (state.workflow.nodes.some((node) => node.id === id)) id = `${prefix}-${++sequence}`;
    const last = orderedNodes.value.at(-1);
    state.workflow.nodes.push({
      id,
      nodeType: state.nodeType,
      label: `${state.nodeType === "warehouse" ? "Warehouse" : "Transport"} ${sequence}`,
      role,
      username: account,
      x: last ? last.x + NODE_WIDTH + NODE_GAP : ROUTE_MARGIN,
      y: last?.y ?? 106,
      stepIndex: state.workflow.nodes.length,
    });
    state.selectedNodeId = id;
    state.selectedEdgeIndex = -1;
    selectDefaultControls();
    markWorkflowChanged("New route node added. Synchronizing the route draft…");
  };

  const toggleNode = (nodeId) => {
    state.selectedNodeId = state.selectedNodeId === nodeId ? "" : nodeId;
    state.selectedEdgeIndex = -1;
  };
  const selectNode = (nodeId) => {
    state.selectedNodeId = nodeId;
    state.selectedEdgeIndex = -1;
  };
  const clearSelection = () => {
    state.selectedNodeId = "";
    state.selectedEdgeIndex = -1;
  };
  const selectEdge = (index) => {
    state.selectedEdgeIndex = state.selectedEdgeIndex === index ? -1 : index;
    state.selectedNodeId = "";
  };

  const commitNodeMove = (beforeSnapshot) => {
    if (beforeSnapshot && beforeSnapshot !== workflowSnapshot()) {
      state.history.past.push(beforeSnapshot);
      state.history.past = state.history.past.slice(-40);
      state.history.future = [];
      markWorkflowChanged("Route position changed. Synchronizing the route draft…");
    }
  };

  const deleteNode = () => {
    const node = selectedNode.value;
    if (!state.workflow || !node) return;
    if (["supplier", "supermarket"].includes(node.role)) {
      state.workflowStatus = "Supplier and Supermarket are required route endpoints.";
      state.workflowStatusKind = "error";
      return;
    }
    captureHistory();
    const predecessors = state.workflow.edges.filter((edge) => edge.to === node.id).map((edge) => edge.from);
    const successors = state.workflow.edges.filter((edge) => edge.from === node.id).map((edge) => edge.to);
    state.workflow.nodes = state.workflow.nodes.filter((item) => item.id !== node.id);
    state.workflow.edges = state.workflow.edges.filter((edge) => edge.from !== node.id && edge.to !== node.id);
    if (predecessors.length === 1 && successors.length === 1) {
      state.workflow.edges.push({ from: predecessors[0], to: successors[0] });
    }
    state.selectedNodeId = "";
    markWorkflowChanged("Route node removed. Synchronizing the route draft…");
  };

  const deleteEdge = (index = state.selectedEdgeIndex) => {
    if (!state.workflow || index < 0 || index >= state.workflow.edges.length) return;
    captureHistory();
    state.workflow.edges.splice(index, 1);
    state.selectedEdgeIndex = -1;
    markWorkflowChanged("Connection removed. Synchronizing the route draft…");
  };

  const connectionError = (fromId, toId) => {
    const from = state.workflow?.nodes.find((node) => node.id === fromId);
    const to = state.workflow?.nodes.find((node) => node.id === toId);
    if (!from || !to) return "Select two available route nodes.";
    if (from.id === to.id) return "A node cannot connect to itself.";
    if (from.role === "supermarket" || to.role === "supplier") return "Supplier starts the route and Supermarket ends it.";
    if (state.workflow.edges.some((edge) => edge.from === fromId)) return "The source already has an outgoing connection.";
    if (state.workflow.edges.some((edge) => edge.to === toId)) return "The target already has an incoming connection.";
    return "";
  };

  const connectNodes = (fromId = state.connectFrom, toId = state.connectTo) => {
    if (!state.workflow) return;
    const error = connectionError(fromId, toId);
    if (error) {
      state.workflowStatus = error;
      state.workflowStatusKind = "error";
      return;
    }
    captureHistory();
    state.workflow.edges.push({ from: fromId, to: toId });
    state.selectedNodeId = toId;
    state.selectedEdgeIndex = -1;
    selectDefaultControls();
    markWorkflowChanged("Connection added. Synchronizing the route draft…");
  };

  const autoArrange = () => {
    if (!state.workflow) return;
    captureHistory();
    ensureLayout(state.workflow, true);
    clearSelection();
    markWorkflowChanged("Route arranged. Synchronizing the route draft…");
  };

  const reloadRoute = async () => {
    if (saveTimer) window.clearTimeout(saveTimer);
    state.routeDirty = false;
    await loadWorkflowView(state.selectedScope, true);
  };

  const loadPolicies = async () => {
    if (!state.workflow) return;
    state.policies = [];
    state.policyStatus = "Loading route-node policies…";
    state.policyStatusKind = "pending";
    try {
      const query = new URLSearchParams();
      if (state.selectedScope) query.set("batchId", state.selectedScope);
      else if (state.workflow.routeId) query.set("routeId", state.workflow.routeId);
      const result = await request(`/api/confirmation-policy${query.size ? `?${query}` : ""}`);
      if (!Array.isArray(result.policies)) throw new Error("The route-node policy response is malformed.");
      state.policies = result.policies.map((policy) => ({ ...policy }));
      state.policyStatus = state.policies.length
        ? "Select at least one confirmation method for every connected route node."
        : "Connect route nodes before configuring confirmation methods.";
      state.policyStatusKind = state.policies.length ? "success" : "pending";
    } catch (error) {
      state.policyStatus = error.message;
      state.policyStatusKind = "error";
    }
  };

  const savePolicies = async () => {
    if (!state.workflow || state.policyBusy) return;
    const invalid = state.policies.find((policy) =>
      !policy.typedName && !policy.handwritten && !policy.face);
    if (invalid) {
      state.policyStatus = `Select at least one method for ${invalid.nodeLabel || invalid.nodeId}.`;
      state.policyStatusKind = "error";
      return;
    }
    state.policyBusy = true;
    state.policyStatus = "Saving confirmation methods…";
    state.policyStatusKind = "pending";
    try {
      const encoded = state.policies.map((policy) => [
        policy.nodeId,
        policy.typedName ? "true" : "false",
        policy.handwritten ? "true" : "false",
        policy.face ? "true" : "false",
      ].join("|")).join(";");
      const result = await request("/api/confirmation-policy", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: new URLSearchParams({
          batchId: state.selectedScope,
          routeId: state.workflow.routeId || "",
          policies: encoded,
        }).toString(),
      });
      state.policies = Array.isArray(result.policies) ? result.policies.map((policy) => ({ ...policy })) : state.policies;
      state.policyStatus = "Route-node confirmation methods saved.";
      state.policyStatusKind = "success";
    } catch (error) {
      state.policyStatus = error.message;
      state.policyStatusKind = "error";
    } finally {
      state.policyBusy = false;
    }
  };

  const syncSnapshotEditor = ({ force = false, resetEvidence = false } = {}) => {
    const batch = selectedBatch.value;
    const product = String(batch?.product || "");
    const batchId = String(batch?.batchId || "");
    if (!product || !batchId) {
      snapshotEditorProduct = "";
      snapshotEditorBatchId = "";
      state.snapshot.refreshValue = 1;
      state.snapshot.refreshUnit = "hours";
      state.snapshot.availableFrom = "";
      state.snapshot.availableUntil = "";
      if (resetEvidence) state.snapshot.selectedEvidence = [];
      return;
    }

    const selectionChanged = snapshotEditorProduct !== product || snapshotEditorBatchId !== batchId;
    if (!force && !selectionChanged) return;

    const policy = state.snapshot.refreshPolicies.find((item) => item.product === product);
    const window = state.snapshot.availabilityWindows.find((item) => item.batchId === batchId);
    const seconds = Number(policy?.intervalSeconds) > 0
      ? Number(policy.intervalSeconds)
      : state.snapshot.defaultIntervalSeconds;
    const editor = intervalEditor(seconds);
    state.snapshot.refreshValue = editor.value;
    state.snapshot.refreshUnit = editor.unit;
    state.snapshot.availableFrom = localDateTime(window?.availableFrom);
    state.snapshot.availableUntil = localDateTime(window?.availableUntil);
    if (resetEvidence || selectionChanged) {
      state.snapshot.selectedEvidence = (batch.evidence || [])
        .filter((item) => item.selectedByDefault)
        .map((item) => `${item.stage}|${item.category}|${item.cid}`);
    }
    snapshotEditorProduct = product;
    snapshotEditorBatchId = batchId;
  };

  const loadSnapshotSchedules = async () => {
    try {
      const result = await request("/api/snapshot/refresh-policies");
      if (!Array.isArray(result.products) || !Array.isArray(result.batchWindows)) {
        throw new Error("The Snapshot schedule response is malformed.");
      }
      state.snapshot.refreshPolicies = result.products;
      state.snapshot.availabilityWindows = result.batchWindows;
      state.snapshot.defaultIntervalSeconds = Number(result.defaultIntervalSeconds) || 3600;
      state.snapshot.schedulesAvailable = true;
      state.snapshot.scheduleStatus = "";
      state.snapshot.scheduleStatusKind = "";
      syncSnapshotEditor({ force: true });
    } catch (error) {
      state.snapshot.schedulesAvailable = false;
      state.snapshot.scheduleStatus = error.message;
      state.snapshot.scheduleStatusKind = "error";
    }
  };

  const loadSnapshotCandidates = async () => {
    if (!state.session || state.routeDirty) return;
    try {
      const result = await request("/api/snapshot/eligible-batches");
      if (!Array.isArray(result.batches)) throw new Error("The snapshot candidate response is malformed.");
      const preview = state.snapshot.preview;
      const previewBatch = preview
        ? result.batches.find((batch) => batch.batchId === preview.batchId)
        : null;
      const previewStillValid = Boolean(preview && previewBatch &&
        previewBatch.routeFingerprint === preview.routeFingerprint &&
        previewBatch.finalPrivateBlockHash === preview.finalPrivateBlockHash);
      const previousSelection = state.snapshot.selectedBatchId;

      if (preview && !previewStillValid) {
        clearPreview("The batch or route changed. Generate a new Snapshot preview.");
      }
      state.snapshot.candidates = result.batches;
      const preferredSelection = previewStillValid ? preview.batchId : previousSelection;
      if (result.batches.some((batch) => batch.batchId === preferredSelection)) {
        state.snapshot.selectedBatchId = preferredSelection;
        syncSnapshotEditor();
      } else {
        state.snapshot.selectedBatchId = result.batches[0]?.batchId || "";
      }
      if (!state.snapshot.preview) {
        state.snapshot.status = result.batches.length
          ? "Select a completed batch and review its public evidence."
          : "Complete and verify every assigned route stage before generating a snapshot.";
        state.snapshot.statusKind = result.batches.length ? "success" : "pending";
      }
    } catch (error) {
      state.snapshot.candidates = [];
      clearPreview();
      state.snapshot.status = error.message;
      state.snapshot.statusKind = "error";
    }
  };

  const intervalSeconds = () => {
    const multipliers = { minutes: 60, hours: 3600, days: 86400 };
    const amount = Number(state.snapshot.refreshValue);
    const seconds = amount * multipliers[state.snapshot.refreshUnit];
    if (!Number.isInteger(amount) || amount <= 0 || seconds < 60 || seconds > 365 * 86400) {
      throw new Error("Enter a whole-number interval from 1 minute to 365 days.");
    }
    return seconds;
  };

  const availabilityIso = (value, label) => {
    if (!value) throw new Error(`${label} is required.`);
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) throw new Error(`Enter a valid ${label.toLowerCase()}.`);
    date.setSeconds(0, 0);
    return date.toISOString();
  };

  const saveSnapshotSchedule = async () => {
    const batch = selectedBatch.value;
    if (!batch) throw new Error("Select a completed batch first.");
    const seconds = intervalSeconds();
    const availableFrom = availabilityIso(state.snapshot.availableFrom, "Available from");
    const availableUntil = availabilityIso(state.snapshot.availableUntil, "Available until");
    if (Date.parse(availableUntil) <= Date.parse(availableFrom)) {
      throw new Error("Available until must be later than available from.");
    }
    const policy = state.snapshot.refreshPolicies.find((item) => item.product === batch.product);
    const window = state.snapshot.availabilityWindows.find((item) => item.batchId === batch.batchId);
    const currentSeconds = Number(policy?.intervalSeconds) > 0
      ? Number(policy.intervalSeconds)
      : state.snapshot.defaultIntervalSeconds;
    if (policy?.configured && currentSeconds === seconds &&
      window?.availableFrom === availableFrom && window?.availableUntil === availableUntil) {
      state.snapshot.scheduleStatus = `${batch.product} refresh: ${formatInterval(seconds)}. Availability unchanged.`;
      state.snapshot.scheduleStatusKind = "success";
      return false;
    }
    const result = await request("/api/snapshot/refresh-policies", {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
      body: new URLSearchParams({
        product: batch.product,
        intervalSeconds: String(seconds),
        batchId: batch.batchId,
        availableFrom,
        availableUntil,
      }).toString(),
    });
    state.snapshot.refreshPolicies = result.products || [];
    state.snapshot.availabilityWindows = result.batchWindows || [];
    state.snapshot.scheduleStatus = `${batch.product} refresh: ${formatInterval(seconds)}. Availability saved.`;
    state.snapshot.scheduleStatusKind = "success";
    return true;
  };

  const invalidateSnapshot = () => {
    if (state.snapshot.preview || state.snapshot.publicationCandidate || state.snapshot.published) {
      clearPreview("Snapshot inputs changed. Generate a new preview.");
    }
  };

  const generateSnapshotPreview = async () => {
    if (!selectedBatch.value || state.snapshot.operation) return;
    state.snapshot.operation = "preview";
    state.snapshot.status = "Saving the public availability schedule…";
    state.snapshot.statusKind = "pending";
    try {
      await saveSnapshotSchedule();
      state.snapshot.status = "Generating a private, non-published preview…";
      const result = await request("/api/snapshot/preview", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: new URLSearchParams({
          batchId: selectedBatch.value.batchId,
          selectedEvidence: state.snapshot.selectedEvidence.join(","),
        }).toString(),
      });
      state.snapshot.preview = result;
      state.snapshot.publicationCandidate = result.publicationCandidate || null;
      state.snapshot.published = false;
      state.snapshot.publishStatus = result.publicationCandidate
        ? "Preview ready for administrator publication."
        : "Publication data is unavailable.";
      state.snapshot.publishStatusKind = result.publicationCandidate ? "pending" : "error";
      state.snapshot.status = "Snapshot preview generated locally. Nothing was published.";
      state.snapshot.statusKind = "success";
    } catch (error) {
      state.snapshot.status = error.message;
      state.snapshot.statusKind = "error";
    } finally {
      state.snapshot.operation = "";
      flushPendingLiveRefresh();
    }
  };

  const publishSnapshot = async () => {
    if (!state.snapshot.publicationCandidate || state.snapshot.operation) return;
    state.snapshot.operation = "publish";
    state.snapshot.publishStatus = "Submitting snapshot to the local public chain…";
    state.snapshot.publishStatusKind = "pending";
    try {
      const result = await request("/api/snapshot/publish", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(state.snapshot.publicationCandidate),
      });
      state.snapshot.publishStatus = `Published in block ${result.blockNumber}. Open the QR display page on port 8084.`;
      state.snapshot.publishStatusKind = "success";
      state.snapshot.publicationCandidate = null;
      state.snapshot.published = true;
    } catch (error) {
      state.snapshot.publishStatus = error.message;
      state.snapshot.publishStatusKind = "error";
    } finally {
      state.snapshot.operation = "";
      flushPendingLiveRefresh();
    }
  };

  const loadDashboard = async () => {
    await loadScopes();
    await loadWorkflowView(state.selectedScope, true);
    await Promise.all([loadSnapshotSchedules(), loadSnapshotCandidates()]);
  };

  const liveRefreshPriority = {
    snapshot_schedule_changed: 1,
    snapshot_published: 2,
    batch_changed: 3,
    route_changed: 4,
  };

  function queueLiveRefreshType(eventType) {
    if ((liveRefreshPriority[eventType] || 0) >=
      (liveRefreshPriority[pendingLiveRefreshType] || 0)) {
      pendingLiveRefreshType = eventType;
    }
  }

  function liveRefreshBlocked() {
    return state.routeDirty || state.routeSaving || Boolean(state.snapshot.operation);
  }

  async function runLiveRefresh(eventType) {
    if (!state.session) return;
    if (liveRefreshBlocked() || liveRefreshInFlight) {
      queueLiveRefreshType(eventType);
      return;
    }

    liveRefreshInFlight = true;
    try {
      const routeChanged = eventType === "route_changed";
      const chainChanged = routeChanged || eventType === "batch_changed";
      if (routeChanged) {
        await loadScopes();
        await loadWorkflowView(state.selectedScope);
      } else if (chainChanged) {
        await loadWorkflowView(state.selectedScope);
      }

      const refreshes = [];
      if (eventType !== "snapshot_schedule_changed") refreshes.push(loadSnapshotCandidates());
      if (eventType === "snapshot_schedule_changed") refreshes.push(loadSnapshotSchedules());
      await Promise.all(refreshes);
    } finally {
      liveRefreshInFlight = false;
      flushPendingLiveRefresh();
    }
  }

  function flushPendingLiveRefresh() {
    if (!pendingLiveRefreshType || liveRefreshInFlight || liveRefreshBlocked()) return;
    const eventType = pendingLiveRefreshType;
    pendingLiveRefreshType = "";
    window.queueMicrotask(() => runLiveRefresh(eventType));
  }

  const refreshFromEvent = (eventType) => {
    queueLiveRefreshType(eventType);
    if (refreshTimer) window.clearTimeout(refreshTimer);
    refreshTimer = window.setTimeout(() => {
      refreshTimer = 0;
      flushPendingLiveRefresh();
    }, 180);
  };

  const startLiveUpdates = () => {
    liveEvents?.close();
    liveEvents = new EventSource("/api/events");
    liveEvents.onmessage = (event) => {
      if (!event.data) return;
      try {
        const payload = JSON.parse(event.data);
        if (["route_changed", "batch_changed", "snapshot_published", "snapshot_schedule_changed"].includes(payload.type)) {
          refreshFromEvent(payload.type);
        }
      } catch {
        // API refreshes remain available if a broadcast payload is malformed.
      }
    };
  };

  const onKeydown = (event) => {
    if (["INPUT", "TEXTAREA", "SELECT"].includes(event.target?.tagName)) return;
    const modifier = event.ctrlKey || event.metaKey;
    if (modifier && event.key.toLowerCase() === "z") {
      event.preventDefault();
      event.shiftKey ? redo() : undo();
    } else if (modifier && event.key.toLowerCase() === "y") {
      event.preventDefault();
      redo();
    } else if (["Delete", "Backspace"].includes(event.key)) {
      if (state.selectedEdgeIndex >= 0) deleteEdge();
      else if (state.selectedNodeId) deleteNode();
    }
  };

  watch(nodeAccounts, (accounts) => {
    if (!accounts.some((account) => account.username === state.nodeAccount)) {
      state.nodeAccount = accounts[0]?.username || "";
    }
  });
  watch(connectionSources, (nodes) => {
    if (!nodes.some((node) => node.id === state.connectFrom)) state.connectFrom = nodes[0]?.id || "";
  });
  watch(connectionTargets, (nodes) => {
    if (!nodes.some((node) => node.id === state.connectTo)) state.connectTo = nodes[0]?.id || "";
  });
  watch(() => state.snapshot.selectedBatchId, (batchId, previousBatchId) => {
    if (batchId === previousBatchId) return;
    syncSnapshotEditor({ resetEvidence: true });
    if (previousBatchId) invalidateSnapshot();
  });

  onMounted(() => {
    window.addEventListener("keydown", onKeydown);
    void restoreSession();
  });
  onBeforeUnmount(() => {
    window.removeEventListener("keydown", onKeydown);
    liveEvents?.close();
    if (saveTimer) window.clearTimeout(saveTimer);
    if (refreshTimer) window.clearTimeout(refreshTimer);
  });

  return reactive({
    state,
    identity,
    roleLabels,
    workflowValidation,
    orderedNodes,
    selectedNode,
    selectedBatch,
    evidence,
    nodeAccounts,
    connectionSources,
    connectionTargets,
    recordBatches,
    login,
    logout,
    selectScope,
    setNodeType,
    addNode,
    toggleNode,
    selectNode,
    clearSelection,
    selectEdge,
    commitNodeMove,
    deleteNode,
    deleteEdge,
    connectNodes,
    autoArrange,
    reloadRoute,
    undo,
    redo,
    savePolicies,
    invalidateSnapshot,
    generateSnapshotPreview,
    publishSnapshot,
    workflowSnapshot,
  });
}
