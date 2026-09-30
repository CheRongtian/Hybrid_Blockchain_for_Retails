<script setup>
import { computed, nextTick, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";

const props = defineProps({ control: { type: Object, required: true } });

const NODE_WIDTH = 184;
const NODE_HEIGHT = 108;
const MARGIN = 72;
const MIN_ZOOM = 0.45;
const MAX_ZOOM = 1.25;

const canvas = ref(null);
const viewport = reactive({ x: 0, y: 0, zoom: 1 });
const pointer = reactive({
  mode: "",
  pointerId: -1,
  nodeId: "",
  sourceId: "",
  targetId: "",
  startX: 0,
  startY: 0,
  originX: 0,
  originY: 0,
  viewportX: 0,
  viewportY: 0,
  currentX: 0,
  currentY: 0,
  moved: false,
  wasSelected: false,
  before: "",
});
let resizeObserver = null;

const workflow = computed(() => props.control.state.workflow);
const bounds = computed(() => {
  const nodes = workflow.value?.nodes || [];
  const minX = Math.min(0, ...nodes.map((node) => node.x)) - MARGIN;
  const minY = Math.min(0, ...nodes.map((node) => node.y)) - MARGIN;
  const maxX = Math.max(NODE_WIDTH, ...nodes.map((node) => node.x + NODE_WIDTH)) + MARGIN;
  const maxY = Math.max(NODE_HEIGHT, ...nodes.map((node) => node.y + NODE_HEIGHT)) + MARGIN;
  return {
    minX,
    minY,
    width: Math.max(900, maxX - minX),
    height: Math.max(320, maxY - minY),
  };
});
const positions = computed(() => (workflow.value?.nodes || []).map((node) => ({
  node,
  x: node.x - bounds.value.minX,
  y: node.y - bounds.value.minY,
})));
const positionMap = computed(() => new Map(positions.value.map((item) => [item.node.id, item])));
const edges = computed(() => (workflow.value?.edges || []).map((edge, index) => {
  const from = positionMap.value.get(edge.from);
  const to = positionMap.value.get(edge.to);
  if (!from || !to) return null;
  const startX = from.x + NODE_WIDTH;
  const startY = from.y + NODE_HEIGHT / 2;
  const endX = to.x;
  const endY = to.y + NODE_HEIGHT / 2;
  const bend = Math.max(46, Math.abs(endX - startX) * 0.42);
  return {
    edge,
    index,
    path: `M ${startX} ${startY} C ${startX + bend} ${startY}, ${endX - bend} ${endY}, ${endX} ${endY}`,
    removeX: (startX + endX) / 2,
    removeY: (startY + endY) / 2,
  };
}).filter(Boolean));
const previewPath = computed(() => {
  if (pointer.mode !== "connect") return "";
  const source = positionMap.value.get(pointer.sourceId);
  if (!source) return "";
  const startX = source.x + NODE_WIDTH;
  const startY = source.y + NODE_HEIGHT / 2;
  const bend = Math.max(46, Math.abs(pointer.currentX - startX) * 0.42);
  return `M ${startX} ${startY} C ${startX + bend} ${startY}, ${pointer.currentX - bend} ${pointer.currentY}, ${pointer.currentX} ${pointer.currentY}`;
});
const sceneStyle = computed(() => ({
  width: `${bounds.value.width}px`,
  height: `${bounds.value.height}px`,
  transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})`,
}));

const nodeStyle = (position) => ({ left: `${position.x}px`, top: `${position.y}px` });
const pointInScene = (event) => {
  const rect = canvas.value.getBoundingClientRect();
  return {
    x: (event.clientX - rect.left - viewport.x) / viewport.zoom,
    y: (event.clientY - rect.top - viewport.y) / viewport.zoom,
  };
};

const fit = () => {
  if (!canvas.value || positions.value.length === 0) return;
  const width = canvas.value.clientWidth;
  const height = canvas.value.clientHeight;
  const minX = Math.min(...positions.value.map((item) => item.x));
  const minY = Math.min(...positions.value.map((item) => item.y));
  const maxX = Math.max(...positions.value.map((item) => item.x + NODE_WIDTH));
  const maxY = Math.max(...positions.value.map((item) => item.y + NODE_HEIGHT));
  const padding = 42;
  const contentWidth = maxX - minX + padding * 2;
  const contentHeight = maxY - minY + padding * 2;
  viewport.zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM,
    (width - padding * 2) / contentWidth,
    (height - padding * 2) / contentHeight));
  viewport.x = (width - contentWidth * viewport.zoom) / 2 - (minX - padding) * viewport.zoom;
  viewport.y = (height - contentHeight * viewport.zoom) / 2 - (minY - padding) * viewport.zoom;
};

const setZoom = (next) => {
  if (!canvas.value) return;
  const centerX = canvas.value.clientWidth / 2;
  const centerY = canvas.value.clientHeight / 2;
  const sceneX = (centerX - viewport.x) / viewport.zoom;
  const sceneY = (centerY - viewport.y) / viewport.zoom;
  viewport.zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM, next));
  viewport.x = centerX - sceneX * viewport.zoom;
  viewport.y = centerY - sceneY * viewport.zoom;
};

const resetPointer = () => {
  pointer.mode = "";
  pointer.pointerId = -1;
  pointer.nodeId = "";
  pointer.sourceId = "";
  pointer.targetId = "";
  pointer.moved = false;
  canvas.value?.classList.remove("is-panning");
};

const startNode = (event, node) => {
  if (event.button !== 0 || event.target.closest(".workflow-handle")) return;
  event.preventDefault();
  event.stopPropagation();
  pointer.mode = "node";
  pointer.pointerId = event.pointerId;
  pointer.nodeId = node.id;
  pointer.startX = event.clientX;
  pointer.startY = event.clientY;
  pointer.originX = node.x;
  pointer.originY = node.y;
  pointer.moved = false;
  pointer.wasSelected = props.control.state.selectedNodeId === node.id;
  pointer.before = props.control.workflowSnapshot();
  props.control.selectNode(node.id);
  canvas.value.setPointerCapture(event.pointerId);
};

const startConnection = (event, node) => {
  if (event.button !== 0) return;
  event.preventDefault();
  event.stopPropagation();
  const position = positionMap.value.get(node.id);
  pointer.mode = "connect";
  pointer.pointerId = event.pointerId;
  pointer.sourceId = node.id;
  pointer.targetId = "";
  pointer.currentX = position.x + NODE_WIDTH;
  pointer.currentY = position.y + NODE_HEIGHT / 2;
  props.control.clearSelection();
  canvas.value.setPointerCapture(event.pointerId);
};

const startPan = (event) => {
  if (event.button !== 0 || event.target.closest(".workflow-node") || event.target.closest(".workflow-edge-control")) return;
  props.control.clearSelection();
  pointer.mode = "pan";
  pointer.pointerId = event.pointerId;
  pointer.startX = event.clientX;
  pointer.startY = event.clientY;
  pointer.viewportX = viewport.x;
  pointer.viewportY = viewport.y;
  canvas.value.setPointerCapture(event.pointerId);
  canvas.value.classList.add("is-panning");
};

const movePointer = (event) => {
  if (pointer.pointerId !== event.pointerId) return;
  if (pointer.mode === "pan") {
    viewport.x = pointer.viewportX + event.clientX - pointer.startX;
    viewport.y = pointer.viewportY + event.clientY - pointer.startY;
    event.preventDefault();
    return;
  }
  if (pointer.mode === "node") {
    const distance = Math.hypot(event.clientX - pointer.startX, event.clientY - pointer.startY);
    if (!pointer.moved && distance < 5) return;
    const node = workflow.value.nodes.find((item) => item.id === pointer.nodeId);
    if (!node) return;
    pointer.moved = true;
    node.x = Math.round(pointer.originX + (event.clientX - pointer.startX) / viewport.zoom);
    node.y = Math.round(pointer.originY + (event.clientY - pointer.startY) / viewport.zoom);
    event.preventDefault();
    return;
  }
  if (pointer.mode === "connect") {
    const point = pointInScene(event);
    pointer.currentX = point.x;
    pointer.currentY = point.y;
    pointer.targetId = document.elementFromPoint(event.clientX, event.clientY)
      ?.closest(".workflow-handle-target")?.dataset.nodeId || "";
    event.preventDefault();
  }
};

const finishPointer = (event) => {
  if (pointer.pointerId !== event.pointerId) return;
  if (canvas.value.hasPointerCapture(event.pointerId)) canvas.value.releasePointerCapture(event.pointerId);
  if (pointer.mode === "node") {
    if (pointer.moved) props.control.commitNodeMove(pointer.before);
    else if (pointer.wasSelected) props.control.clearSelection();
  } else if (pointer.mode === "connect" && pointer.targetId) {
    props.control.connectNodes(pointer.sourceId, pointer.targetId);
  }
  resetPointer();
};

const wheel = (event) => {
  if (!workflow.value) return;
  event.preventDefault();
  if (!event.ctrlKey && !event.metaKey) {
    viewport.x -= event.deltaX;
    viewport.y -= event.deltaY;
    return;
  }
  const rect = canvas.value.getBoundingClientRect();
  const anchorX = event.clientX - rect.left;
  const anchorY = event.clientY - rect.top;
  const sceneX = (anchorX - viewport.x) / viewport.zoom;
  const sceneY = (anchorY - viewport.y) / viewport.zoom;
  viewport.zoom = Math.max(MIN_ZOOM, Math.min(MAX_ZOOM,
    viewport.zoom * Math.exp(-event.deltaY * 0.0035)));
  viewport.x = anchorX - sceneX * viewport.zoom;
  viewport.y = anchorY - sceneY * viewport.zoom;
};

watch(() => [workflow.value?.routeId, props.control.state.selectedScope], async () => {
  await nextTick();
  fit();
});

onMounted(async () => {
  await nextTick();
  fit();
  if (window.ResizeObserver) {
    resizeObserver = new ResizeObserver(() => fit());
    resizeObserver.observe(canvas.value);
  }
});
onBeforeUnmount(() => resizeObserver?.disconnect());
</script>

<template>
  <div
    ref="canvas"
    class="workflow-canvas"
    role="application"
    aria-label="Editable supply chain route"
    @pointerdown="startPan"
    @pointermove="movePointer"
    @pointerup="finishPointer"
    @pointercancel="finishPointer"
    @wheel="wheel"
  >
    <div class="workflow-scene" :style="sceneStyle">
      <svg class="workflow-edge-layer" :width="bounds.width" :height="bounds.height" :viewBox="`0 0 ${bounds.width} ${bounds.height}`" aria-label="Route connections">
        <defs>
          <marker id="workflow-arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto" markerUnits="strokeWidth">
            <path d="M 0 0 L 8 4 L 0 8 z" class="workflow-arrow-marker" />
          </marker>
        </defs>
        <g v-for="item in edges" :key="`${item.edge.from}-${item.edge.to}`">
          <path
            class="workflow-edge"
            :class="{ selected: control.state.selectedEdgeIndex === item.index }"
            :d="item.path"
            marker-end="url(#workflow-arrow)"
            @pointerdown.stop.prevent="control.selectEdge(item.index)"
          />
          <g
            class="workflow-edge-control"
            :transform="`translate(${item.removeX} ${item.removeY})`"
            role="button"
            tabindex="0"
            :aria-label="`Remove connection from ${item.edge.from} to ${item.edge.to}`"
            @click.stop="control.deleteEdge(item.index)"
            @keydown.enter.prevent="control.deleteEdge(item.index)"
            @keydown.space.prevent="control.deleteEdge(item.index)"
          >
            <circle r="14" />
            <text aria-hidden="true">×</text>
          </g>
        </g>
        <path v-if="previewPath" class="workflow-edge-preview" :d="previewPath" />
      </svg>

      <div class="workflow-node-layer">
        <article
          v-for="position in positions"
          :key="position.node.id"
          class="workflow-node"
          :class="{
            selected: control.state.selectedNodeId === position.node.id,
            'is-dragging': pointer.mode === 'node' && pointer.nodeId === position.node.id && pointer.moved,
          }"
          :style="nodeStyle(position)"
          role="button"
          tabindex="0"
          :aria-pressed="control.state.selectedNodeId === position.node.id"
          :aria-label="`${position.node.label}, ${control.roleLabels[position.node.role] || position.node.role}${control.state.selectedNodeId === position.node.id ? ', selected' : ''}`"
          @pointerdown="startNode($event, position.node)"
          @keydown.enter.prevent="control.toggleNode(position.node.id)"
          @keydown.space.prevent="control.toggleNode(position.node.id)"
        >
          <strong class="workflow-node-title">{{ position.node.label }}</strong>
          <span class="workflow-node-role">{{ control.roleLabels[position.node.role] || position.node.role }}</span>
          <span class="workflow-node-user">{{ position.node.username || 'Unassigned' }}</span>
          <span class="workflow-node-state" aria-hidden="true">
            {{ control.state.selectedNodeId === position.node.id ? '✓ Selected' : '' }}
          </span>
          <button
            v-if="position.node.role !== 'supplier'"
            type="button"
            class="workflow-handle workflow-handle-target"
            :class="{ 'is-connect-target': pointer.targetId === position.node.id }"
            :data-node-id="position.node.id"
            :aria-label="`Connect to ${position.node.label}`"
            @pointerdown.stop.prevent
          />
          <button
            v-if="position.node.role !== 'supermarket'"
            type="button"
            class="workflow-handle workflow-handle-source"
            :aria-label="`Connect from ${position.node.label}`"
            @pointerdown="startConnection($event, position.node)"
          />
        </article>
      </div>
    </div>

    <p class="workflow-canvas-tip">Drag the background to pan · Pinch to zoom · Select × to remove a connection</p>
    <div class="workflow-canvas-controls" aria-label="Canvas controls">
      <button type="button" aria-label="Zoom out" @click="setZoom(viewport.zoom - 0.05)">−</button>
      <span aria-live="polite">{{ Math.round(viewport.zoom * 100) }}%</span>
      <button type="button" @click="fit">Fit route</button>
      <button type="button" aria-label="Zoom in" @click="setZoom(viewport.zoom + 0.05)">+</button>
    </div>
  </div>
</template>
