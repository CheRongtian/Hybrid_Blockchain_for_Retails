<script setup>
import { onMounted, ref } from "vue";

defineProps({ record: { type: Object, required: true } });
defineEmits(["close"]);

const closeButton = ref(null);
const displayValue = (value) => value === undefined || value === null || value === "" ? "Unavailable" : value;
onMounted(() => closeButton.value?.focus());
</script>

<template>
  <section class="record-dialog-overlay" role="presentation" @click.self="$emit('close')" @keydown.esc.prevent="$emit('close')">
    <article class="record-dialog" role="dialog" aria-modal="true" aria-labelledby="block-dialog-title">
      <header class="record-dialog-header">
        <div>
          <p class="eyebrow">PRIVATE CHAIN BLOCK</p>
          <h2 id="block-dialog-title">Block {{ record.blockID }} · {{ record.routeNodeLabel || record.stage || 'Route stage' }}</h2>
          <code>{{ displayValue(record.rootHash) }}</code>
        </div>
        <button ref="closeButton" class="dialog-close" type="button" aria-label="Close block details" @click="$emit('close')">Close</button>
      </header>
      <div class="record-dialog-body">
        <dl class="block-facts">
          <div><dt>Batch</dt><dd>{{ displayValue(record.batchId) }}</dd></div>
          <div><dt>Product</dt><dd>{{ displayValue(record.product) }}</dd></div>
          <div><dt>Role</dt><dd>{{ displayValue(record.role) }}</dd></div>
          <div><dt>Organization</dt><dd>{{ displayValue(record.organizationId) }}</dd></div>
          <div><dt>Confirmed by</dt><dd>{{ displayValue(record.confirmedBy) }}</dd></div>
          <div><dt>Confirmation</dt><dd>{{ displayValue(record.confirmationMethod) }}</dd></div>
          <div><dt>Parent block</dt><dd>{{ record.parentBlockId >= 0 ? `Block ${record.parentBlockId}` : 'Genesis' }}</dd></div>
          <div><dt>Chain status</dt><dd>{{ displayValue(record.chainStatus) }}</dd></div>
          <div><dt>Harvest date</dt><dd>{{ displayValue(record.batchHarvestDate) }}</dd></div>
          <div><dt>Location</dt><dd>{{ displayValue(record.locationSummary || record.batchFarmLocation) }}</dd></div>
          <div><dt>Block integrity</dt><dd>{{ record.verified ? 'Verified' : 'Failed' }}</dd></div>
          <div><dt>Signature</dt><dd>{{ record.signatureVerified ? 'Verified' : 'Failed' }}</dd></div>
        </dl>
        <section class="dialog-data-section">
          <h3>Event data</h3>
          <pre>{{ JSON.stringify(record.eventData || {}, null, 2) }}</pre>
        </section>
        <section v-if="record.ipfsRefs?.length" class="dialog-data-section">
          <h3>IPFS evidence</h3>
          <ul class="evidence-reference-list">
            <li v-for="item in record.ipfsRefs" :key="`${item.category}-${item.cid}`">
              <strong>{{ item.category }}</strong><code>{{ item.cid }}</code><small>{{ item.filename || item.contentType }}</small>
            </li>
          </ul>
        </section>
      </div>
    </article>
  </section>
</template>
