<script setup>
defineProps({
  batches: { type: Array, required: true },
  selectedBatchId: { type: String, default: "" },
  status: { type: String, default: "" },
  statusKind: { type: String, default: "" },
  loading: { type: Boolean, default: false },
  verificationOnly: { type: Boolean, default: false },
});

defineEmits(["select"]);
</script>

<template>
  <header class="consumer-header">
    <a class="brand" href="/" aria-label="FreshLedger home">
      <span class="brand-mark" aria-hidden="true">
        <svg viewBox="0 0 24 24"><path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9"/></svg>
      </span>
      <span><strong>FreshLedger</strong><small>Public trace network</small></span>
    </a>
    <span class="network-pill"><i></i> Local chain · 31337</span>
  </header>

  <section class="trace-hero" :class="{ 'is-verification': verificationOnly }">
    <div class="hero-copy">
      <p class="eyebrow">VERIFIABLE FOOD ORIGIN</p>
      <h1>{{ verificationOnly ? 'Verification result' : 'Follow every handoff.' }}</h1>
      <p class="lede">
        {{ verificationOnly
          ? 'This public trace is checked against its active on-chain Snapshot.'
          : 'Inspect the route from farm to shelf, with public evidence anchored to an independently verified Snapshot.' }}
      </p>
      <div class="trust-row" aria-label="Verification capabilities">
        <span>Manifest integrity</span>
        <span>Merkle proof</span>
        <span>Chain anchor</span>
      </div>
    </div>

    <form v-if="!verificationOnly" class="batch-search" @submit.prevent>
      <p class="search-kicker">TRACE LOOKUP</p>
      <label for="batch-select">Published product batch</label>
      <select
        id="batch-select"
        :value="selectedBatchId"
        :disabled="loading || batches.length === 0"
        @change="$emit('select', $event.target.value)"
      >
        <option value="">Choose a product batch</option>
        <option v-for="batch in batches" :key="batch.batchId" :value="batch.batchId">
          {{ batch.product }} · {{ batch.batchId }}{{ batch.availabilityState === 'expired' ? ' · Off shelf' : '' }}
        </option>
      </select>
      <p class="search-help">Only currently published public records appear here.</p>
    </form>
  </section>

  <p class="page-status" :class="statusKind" role="status" aria-live="polite">
    <span v-if="loading" class="status-spinner" aria-hidden="true"></span>
    {{ status }}
  </p>
</template>
