<script setup>
import { ref } from "vue";
import BlockDetailsDialog from "./BlockDetailsDialog.vue";
import MerkleTreeDialog from "./MerkleTreeDialog.vue";

defineProps({ control: { type: Object, required: true } });

const detailsRecord = ref(null);
const merkleRecord = ref(null);
const shortHash = (hash = "") => hash.length > 22 ? `${hash.slice(0, 12)}…${hash.slice(-8)}` : hash || "Unknown";
</script>

<template>
  <section id="records-panel" class="records-region" aria-labelledby="records-title">
    <header class="records-header">
      <div>
        <p class="eyebrow">VERIFICATION LEDGER</p>
        <h2 id="records-title">Supply-chain records</h2>
      </div>
      <p class="status" :class="control.state.recordsStatusKind" role="status" aria-live="polite">
        {{ control.state.recordsStatus }}
      </p>
    </header>
    <section class="record-list" aria-label="Supply-chain records">
      <article v-for="batch in control.recordBatches" :key="batch.batchId" class="chain-card">
        <header>
          <div><p class="eyebrow">BATCH ROUTE</p><h3>{{ batch.product || 'Batch' }} · {{ batch.batchId }}</h3></div>
          <span class="badge" :class="batch.routeError ? 'failed' : batch.complete ? 'verified' : 'pending'">
            {{ batch.routeError ? 'Route error' : batch.complete ? 'Completed' : 'In progress' }}
          </span>
        </header>
        <p v-if="batch.routeError" class="status error">{{ batch.routeError }}</p>
        <p class="chain-structure-label">Linked private-chain blocks · each completed block contains an independent Merkle tree</p>
        <div v-if="batch.records.length || batch.pendingNodes.length" class="chain-flow">
          <template v-for="(record, index) in batch.records" :key="`record-${record.blockID}`">
            <span v-if="index" class="chain-arrow" aria-hidden="true">→</span>
            <section class="chain-node">
              <div class="chain-node-header">
                <strong>Block {{ record.blockID }} · {{ record.routeNodeLabel || control.roleLabels[record.role] || record.stage }}</strong>
                <span class="badge" :class="record.verified && record.signatureVerified ? 'verified' : 'failed'">
                  {{ record.verified && record.signatureVerified ? 'Verified' : 'Failed' }}
                </span>
              </div>
              <dl class="chain-node-summary">
                <div><dt>Role</dt><dd>{{ record.role || 'Unknown' }}</dd></div>
                <div><dt>Organization</dt><dd>{{ record.organizationId || 'Unknown' }}</dd></div>
                <div><dt>Product</dt><dd>{{ record.product || 'Unknown' }}</dd></div>
                <div><dt>Merkle root</dt><dd><code>{{ shortHash(record.rootHash) }}</code></dd></div>
              </dl>
              <div class="chain-node-actions">
                <button class="chain-detail-button" type="button" @click="detailsRecord = record">Block details</button>
                <button class="chain-detail-button" type="button" :disabled="!record.merkleTree?.root" @click="merkleRecord = record">Open Merkle tree</button>
              </div>
            </section>
          </template>
          <template v-for="(node, index) in batch.pendingNodes" :key="`pending-${node.id}`">
            <span v-if="batch.records.length || index" class="chain-arrow pending" aria-hidden="true">→</span>
            <section class="chain-node pending-chain-node">
              <div class="chain-node-header">
                <strong>{{ node.label || node.id }}</strong><span class="badge pending">Pending</span>
              </div>
              <dl class="chain-node-summary">
                <div><dt>Role</dt><dd>{{ node.role || 'Unknown' }}</dd></div>
                <div><dt>Assigned to</dt><dd>{{ node.username || 'Unassigned' }}</dd></div>
              </dl>
              <p class="pending-copy">Waiting for this participant to confirm the next handoff.</p>
            </section>
          </template>
        </div>
        <p v-else class="empty-copy">No blocks have been submitted for this route yet.</p>
      </article>
      <p v-if="control.recordBatches.length === 0" class="empty-copy">No supply-chain records are available.</p>
    </section>
  </section>

  <BlockDetailsDialog v-if="detailsRecord" :record="detailsRecord" @close="detailsRecord = null" />
  <MerkleTreeDialog v-if="merkleRecord" :record="merkleRecord" @close="merkleRecord = null" />
</template>
