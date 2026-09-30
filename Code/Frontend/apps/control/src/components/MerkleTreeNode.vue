<script setup>
import { computed, ref } from "vue";

defineOptions({ name: "MerkleTreeNode" });
const props = defineProps({
  node: { type: Object, required: true },
  selectedId: { type: String, default: "" },
  depth: { type: Number, default: 0 },
});
const emit = defineEmits(["select"]);
const expanded = ref(true);
const hasChildren = computed(() => Array.isArray(props.node.children) && props.node.children.length > 0);
const stateClass = computed(() => props.node.kind === "duplicate"
  ? "duplicate-state"
  : props.node.verified ? "verified-state" : "failed-state");
const label = computed(() => {
  if (props.node.kind === "root") return "Merkle root";
  if (props.node.kind === "leaf") return props.node.fieldName || `Leaf ${props.node.leafIndex}`;
  if (props.node.kind === "duplicate") return "Duplicated leaf";
  return "Internal hash";
});
</script>

<template>
  <li class="merkle-tree-item" :class="{ 'merkle-root': node.kind === 'root' }">
    <div class="merkle-tree-row" :class="stateClass" :style="{ '--tree-depth': depth }">
      <button
        class="merkle-tree-toggle"
        type="button"
        :disabled="!hasChildren"
        :aria-expanded="hasChildren ? expanded : undefined"
        :aria-label="expanded ? 'Collapse branch' : 'Expand branch'"
        @click="expanded = !expanded"
      >{{ hasChildren ? (expanded ? '−' : '+') : '·' }}</button>
      <button
        class="merkle-tree-node-button"
        :class="[stateClass, { selected: selectedId === node.nodeId }]"
        type="button"
        :aria-pressed="selectedId === node.nodeId"
        @click="emit('select', node)"
      >
        <span>{{ label }}</span><i class="merkle-state" aria-hidden="true"></i>
      </button>
    </div>
    <ul v-if="hasChildren && expanded" class="merkle-tree-children">
      <MerkleTreeNode
        v-for="child in node.children"
        :key="child.nodeId"
        :node="child"
        :selected-id="selectedId"
        :depth="depth + 1"
        @select="emit('select', $event)"
      />
    </ul>
  </li>
</template>
