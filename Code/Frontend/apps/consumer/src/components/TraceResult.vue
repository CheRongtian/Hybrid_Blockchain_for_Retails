<script setup>
import { computed } from "vue";
import AiAssistant from "./AiAssistant.vue";

const props = defineProps({
  trace: { type: Object, required: true },
  stages: { type: Array, required: true },
  selectedStageIndex: { type: Number, default: 0 },
  verificationOnly: { type: Boolean, default: false },
  assistant: { type: Object, required: true },
});

defineEmits(["select-stage"]);

const manifest = computed(() => props.trace.manifest);
const selectedStage = computed(() => props.stages[props.selectedStageIndex] || props.stages[0]);
const badgeLabel = computed(() => props.trace.verified
  ? "Verified"
  : props.trace.integrityVerified ? props.trace.status : "Verification failed");
const badgeKind = computed(() => props.trace.verified
  ? "verified"
  : props.trace.integrityVerified ? "warning" : "failed");
const technicalLabels = {
  publicRoot: "Public Root", manifestHash: "Manifest Hash",
  sourceBlockHash: "Final Private Block Hash", snapshotIdHash: "Snapshot ID Hash",
  transactionHash: "Transaction Hash", contractAddress: "Contract Address",
  blockNumber: "Block Number", chainId: "Chain ID", nonce: "Nonce",
  publisher: "Publisher", publishedAt: "Published At (UTC)",
};
const checks = computed(() => Object.entries(props.trace.checks || {}).flatMap(([group, values]) =>
  Object.entries(values).map(([name, valid]) => ({ label: `${group}.${name}`, valid }))));
</script>

<template>
  <article class="trace-card" :class="{ 'verification-card': verificationOnly }">
    <header class="trace-summary">
      <div class="product-heading">
        <p class="eyebrow">ACTIVE PUBLIC SNAPSHOT</p>
        <h2>{{ manifest.batch.product_name }}</h2>
        <p>{{ manifest.batch.batch_id }} <span>·</span> Snapshot {{ trace.snapshotId }}</p>
      </div>
      <div class="verification-seal" :class="badgeKind">
        <svg viewBox="0 0 24 24" aria-hidden="true">
          <path v-if="trace.verified" d="m6.5 12.5 3.5 3.5 7.5-8"/>
          <path v-else d="M12 7v6m0 4h.01"/>
        </svg>
        <span><small>CHAIN STATUS</small><strong>{{ badgeLabel }}</strong></span>
      </div>
    </header>

    <div class="verification-meta">
      <div><span>Last verified</span><strong>{{ trace.formattedLastUpdated || 'Unavailable' }}</strong></div>
      <div><span>Snapshot published</span><strong>{{ trace.formattedPublishedAt || 'Unavailable' }}</strong></div>
      <div><span>Availability</span><strong>{{ trace.availability?.state === 'expired' ? 'Off shelf' : 'On shelf' }}</strong></div>
    </div>

    <section class="fact-grid" aria-label="Product facts">
      <article><span>Origin</span><strong>{{ manifest.origin.farm_location || 'Not disclosed' }}</strong></article>
      <article><span>Harvest</span><strong>{{ manifest.origin.harvest_date || 'Not disclosed' }}</strong></article>
      <article><span>Category</span><strong>{{ manifest.batch.category || 'Not disclosed' }}</strong></article>
      <article><span>Route</span><strong>{{ manifest.verification.route_completed ? 'Completed' : 'Incomplete' }}</strong></article>
    </section>

    <section class="route-panel" aria-labelledby="route-title">
      <header class="section-title">
        <div><p class="eyebrow">CHAIN OF CUSTODY</p><h3 id="route-title">Origin to shelf</h3></div>
        <span>{{ stages.length }} verified handoffs</span>
      </header>
      <div class="route-timeline">
        <button
          v-for="(stage, index) in stages"
          :key="`${stage.label}-${index}`"
          class="route-stop"
          :class="{ active: selectedStageIndex === index }"
          type="button"
          :aria-current="selectedStageIndex === index ? 'step' : undefined"
          @click="$emit('select-stage', index)"
        >
          <span class="route-number">{{ String(index + 1).padStart(2, '0') }}</span>
          <span><strong>{{ stage.label }}</strong><small>{{ stage.primary }}</small></span>
        </button>
      </div>
      <div v-if="selectedStage" class="stage-inspector" aria-live="polite">
        <div><span>Selected handoff</span><strong>{{ selectedStage.label }}</strong></div>
        <p>{{ selectedStage.primary }}</p>
        <p>{{ selectedStage.secondary }}</p>
      </div>
    </section>

    <AiAssistant :assistant="assistant" />

    <section v-if="!verificationOnly && trace.evidence?.length" class="evidence-panel">
      <header class="section-title"><div><p class="eyebrow">PUBLIC EVIDENCE</p><h3>Referenced files</h3></div></header>
      <div class="evidence-grid">
        <article v-for="item in trace.evidence" :key="`${item.cid}-${item.type}`">
          <span>{{ item.type }} · {{ item.stage }}</span><code>{{ item.cid }}</code>
        </article>
      </div>
    </section>

    <details v-if="!verificationOnly" class="technical-panel">
      <summary>Technical verification details <span>Manifest, hashes and chain record</span></summary>
      <div class="check-grid">
        <span v-for="check in checks" :key="check.label" :class="{ failed: !check.valid }">
          {{ check.valid ? 'PASS' : 'FAIL' }} · {{ check.label }}
        </span>
      </div>
      <dl>
        <template v-for="(label, key) in technicalLabels" :key="key">
          <dt>{{ label }}</dt><dd>{{ trace.technical?.[key] ?? 'Unavailable' }}</dd>
        </template>
      </dl>
    </details>
  </article>
</template>
