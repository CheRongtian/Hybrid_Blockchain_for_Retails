<script setup>
import { onMounted, ref } from "vue";
import MerkleTreeNode from "./MerkleTreeNode.vue";

const props = defineProps({ record: { type: Object, required: true } });
defineEmits(["close"]);
const closeButton = ref(null);
const selected = ref(props.record.merkleTree?.root || null);
const shortHash = (hash = "") => hash.length > 34 ? `${hash.slice(0, 17)}…${hash.slice(-13)}` : hash || "Unavailable";
onMounted(() => closeButton.value?.focus());
</script>

<template>
  <section class="record-dialog-overlay" role="presentation" @click.self="$emit('close')" @keydown.esc.prevent="$emit('close')">
    <article class="record-dialog merkle-dialog" role="dialog" aria-modal="true" aria-labelledby="merkle-dialog-title">
      <header class="record-dialog-header">
        <div>
          <p class="eyebrow">MERKLE VERIFICATION</p>
          <h2 id="merkle-dialog-title">Block {{ record.blockID }} · Merkle tree</h2>
          <code>{{ record.merkleTree?.leafCount || 0 }} leaves · {{ record.merkleTree?.consistent ? 'Root consistent' : 'Root mismatch' }}</code>
        </div>
        <button ref="closeButton" class="dialog-close" type="button" aria-label="Close Merkle tree" @click="$emit('close')">Close</button>
      </header>
      <div class="merkle-dialog-content">
        <section class="merkle-tree-viewport" aria-label="Merkle tree">
          <p class="merkle-tree-legend">Select a node to inspect its stored hash and proof data.</p>
          <ul v-if="record.merkleTree?.root" class="merkle-tree-list">
            <MerkleTreeNode
              :node="record.merkleTree.root"
              :selected-id="selected?.nodeId"
              @select="selected = $event"
            />
          </ul>
          <p v-else class="empty-copy">This block has no Merkle display tree.</p>
        </section>
        <aside class="merkle-node-details" aria-live="polite">
          <template v-if="selected">
            <h3>{{ selected.kind === 'leaf' ? selected.fieldName : `${selected.kind} node` }}</h3>
            <p><strong>State</strong> {{ selected.verified ? 'Verified' : 'Failed' }}</p>
            <p v-if="selected.kind === 'leaf'"><strong>Value</strong> {{ selected.value || 'Empty value' }}</p>
            <p><strong>Hash</strong><code>{{ selected.hash || 'Unavailable' }}</code></p>
            <p v-if="selected.proof"><strong>Proof</strong><code>{{ selected.proof }}</code></p>
          </template>
          <p v-else class="empty-copy">Select a tree node.</p>
        </aside>
      </div>
      <footer class="merkle-consistency" :class="record.merkleTree?.consistent ? 'success' : 'error'">
        <span>Calculated</span><code>{{ shortHash(record.merkleTree?.calculatedRootHash) }}</code>
        <span>Stored</span><code>{{ shortHash(record.merkleTree?.storedRootHash) }}</code>
      </footer>
    </article>
  </section>
</template>
