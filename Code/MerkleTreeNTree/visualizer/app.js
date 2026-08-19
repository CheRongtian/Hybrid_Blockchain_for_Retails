const state = {
  queue: [],
  playing: true,
  speed: 5,
  processing: false,
  eventCount: 0,
  lastEvent: null,
};

const $ = (selector) => document.querySelector(selector);

function shortHash(value) {
  if (!value) return "–";
  return value.length > 18 ? `${value.slice(0, 9)}…${value.slice(-7)}` : value;
}

function setStatus(event) {
  const status = $("#build-status");
  const badge = $("#status-badge");
  const message = $("#event-message");
  const type = event.type || "event";
  const success = type === "build_succeeded" || type === "proof_verified";
  const failed = type === "build_failed" || type === "proof_failed";
  status.textContent = type.replaceAll("_", " ");
  message.textContent = event.message || "";
  badge.className = `badge ${success ? "success" : failed ? "failed" : "running"}`;
  badge.textContent = success ? "Success" : failed ? "Failed" : "Building";
}

function renderMetrics(event) {
  $("#arity").textContent = event.arity || "–";
  $("#event-count").textContent = String(state.eventCount);
  const nodes = event.nodes || [];
  const leaves = nodes.filter((node) => node.leaf && !node.padding).length;
  const levels = nodes.map((node) => node.level);
  $("#leaf-count").textContent = leaves ? String(leaves) : "–";
  $("#height").textContent = levels.length ? String(Math.max(...levels) + 1) : "–";
  $("#root-hash").textContent = event.rootHash || "–";
  $("#level-label").textContent = `Level ${event.level ?? "–"}`;
}

function appendLog(event) {
  const item = document.createElement("li");
  if (event.type === "build_failed" || event.type === "proof_failed") item.className = "failed";
  const title = document.createElement("strong");
  title.textContent = event.type.replaceAll("_", " ");
  item.append(title, document.createTextNode(` — ${event.message || ""}`));
  $("#event-log").append(item);
  const log = $("#event-log");
  log.scrollTop = log.scrollHeight;
}

function svgElement(name, attributes = {}) {
  const element = document.createElementNS("http://www.w3.org/2000/svg", name);
  for (const [key, value] of Object.entries(attributes)) element.setAttribute(key, value);
  return element;
}

function renderTree(event) {
  const canvas = $("#tree-canvas");
  const nodes = event.nodes || [];
  if (nodes.length === 0) {
    canvas.setAttribute("viewBox", "0 0 900 320");
    canvas.replaceChildren();
    return;
  }

  const levels = new Map();
  for (const node of nodes) {
    if (!levels.has(node.level)) levels.set(node.level, []);
    levels.get(node.level).push(node);
  }
  for (const group of levels.values()) group.sort((left, right) => left.index - right.index);

  const maxNodes = Math.max(...[...levels.values()].map((group) => group.length));
  const maxLevel = Math.max(...nodes.map((node) => node.level));
  const nodeWidth = 142;
  const nodeHeight = 72;
  const gapX = 24;
  const gapY = 66;
  const width = Math.max(900, maxNodes * (nodeWidth + gapX) + 60);
  const height = Math.max(320, (maxLevel + 1) * (nodeHeight + gapY) + 60);
  canvas.setAttribute("viewBox", `0 0 ${width} ${height}`);
  canvas.setAttribute("width", String(width));
  canvas.setAttribute("height", String(height));

  const positions = new Map();
  for (const [level, group] of levels) {
    const rowWidth = group.length * nodeWidth + (group.length - 1) * gapX;
    const start = Math.max(30, (width - rowWidth) / 2);
    const y = height - 42 - (level + 1) * (nodeHeight + gapY);
    group.forEach((node, index) => {
      positions.set(node.id, {
        x: start + index * (nodeWidth + gapX),
        y,
        centerX: start + index * (nodeWidth + gapX) + nodeWidth / 2,
        centerY: y + nodeHeight / 2,
      });
    });
  }

  const edges = svgElement("g");
  for (const parent of nodes.filter((node) => !node.leaf)) {
    const parentPosition = positions.get(parent.id);
    for (const childId of parent.children || []) {
      const childPosition = positions.get(childId);
      if (!parentPosition || !childPosition) continue;
      const child = nodes.find((node) => node.id === childId);
      edges.append(svgElement("line", {
        x1: parentPosition.centerX,
        y1: parentPosition.y + nodeHeight,
        x2: childPosition.centerX,
        y2: childPosition.y,
        class: child?.padding ? "tree-edge padding-edge" : "tree-edge",
      }));
    }
  }

  const nodeGroup = svgElement("g");
  for (const node of nodes) {
    const position = positions.get(node.id);
    if (!position) continue;
    const group = svgElement("g", {
      class: `tree-node ${node.leaf ? "leaf" : "parent"} ${node.padding ? "padding" : ""}`,
      transform: `translate(${position.x}, ${position.y})`,
    });
    const rect = svgElement("rect", { width: nodeWidth, height: nodeHeight, rx: 10 });
    const title = svgElement("text", { x: 12, y: 21, class: "node-title" });
    title.textContent = node.padding ? `Padding #${node.id}` : `${node.leaf ? "Leaf" : "Parent"} #${node.id}`;
    const hash = svgElement("text", { x: 12, y: 41, class: "node-hash" });
    hash.textContent = shortHash(node.hash);
    const kind = svgElement("text", { x: 12, y: 59, class: "node-kind" });
    kind.textContent = `L${node.level} · ${node.leaf ? "data" : `${node.children.length} children`}`;
    group.append(rect, title, hash, kind);
    const fullHash = node.hash || "No hash";
    group.setAttribute("aria-label", fullHash);
    const tooltip = document.createElementNS("http://www.w3.org/2000/svg", "title");
    tooltip.textContent = `${fullHash}${node.padding ? ` · duplicate of #${node.duplicateOf}` : ""}`;
    group.append(tooltip);
    nodeGroup.append(group);
  }
  canvas.replaceChildren(edges, nodeGroup);
}

function updateProof(event) {
  if (event.type === "proof_generated") {
    $("#proof-status").textContent = "Proof generated";
    $("#proof-message").textContent = event.message;
  }
  if (event.type === "proof_verified" || event.type === "proof_failed") {
    $("#proof-status").textContent = event.type === "proof_verified" ? "Verified ✓" : "Verification failed";
    $("#proof-message").textContent = event.message;
  }
}

function applyEvent(event) {
  state.eventCount += 1;
  state.lastEvent = event;
  setStatus(event);
  renderMetrics(event);
  renderTree(event);
  updateProof(event);
  appendLog(event);
}

function pump() {
  if (!state.playing || state.processing || state.queue.length === 0) return;
  state.processing = true;
  const event = state.queue.shift();
  applyEvent(event);
  const delay = Math.max(40, 760 - state.speed * 70);
  window.setTimeout(() => {
    state.processing = false;
    pump();
  }, delay);
}

$("#pause-button").addEventListener("click", () => {
  state.playing = !state.playing;
  $("#pause-button").textContent = state.playing ? "Pause" : "Resume";
  pump();
});

$("#step-button").addEventListener("click", () => {
  if (state.processing || state.queue.length === 0) return;
  applyEvent(state.queue.shift());
});

$("#restart-button").addEventListener("click", () => window.location.reload());
$("#speed-range").addEventListener("input", (event) => {
  state.speed = Number(event.target.value);
});

const events = new EventSource("/events");
events.onmessage = (message) => {
  try {
    state.queue.push(JSON.parse(message.data));
    pump();
  } catch {
    $("#event-message").textContent = "Received an invalid build event.";
  }
};
events.onerror = () => {
  $("#event-message").textContent = "The event stream is reconnecting…";
};
