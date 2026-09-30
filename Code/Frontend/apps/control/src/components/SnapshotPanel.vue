<script setup>
defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <details id="snapshot-panel" class="record-card snapshot-card" open>
    <summary class="snapshot-summary">
      <div>
        <p class="eyebrow">PUBLIC SNAPSHOT</p>
        <h2>Consumer Data Preview</h2>
        <p>Review public fields, availability, and evidence before publication.</p>
      </div>
      <span class="badge" :class="control.state.snapshot.candidates.length ? 'verified' : 'pending'">
        {{ control.state.snapshot.candidates.length }} eligible {{ control.state.snapshot.candidates.length === 1 ? 'batch' : 'batches' }}
      </span>
    </summary>
    <div class="snapshot-content">
      <form class="snapshot-form" @submit.prevent="control.generateSnapshotPreview">
        <label>
          Completed Batch
          <select v-model="control.state.snapshot.selectedBatchId" name="batchId" :disabled="control.state.snapshot.operation || control.state.snapshot.candidates.length === 0">
            <option v-for="batch in control.state.snapshot.candidates" :key="batch.batchId" :value="batch.batchId">
              {{ batch.batchId }} · {{ batch.product }}
            </option>
          </select>
        </label>
        <div class="snapshot-refresh-setting">
          <div class="snapshot-refresh-heading">
            <strong>Public availability and refresh</strong>
            <span>
              {{ control.selectedBatch ? `${control.selectedBatch.product} · public availability` : 'Select a completed batch' }}
            </span>
          </div>
          <label>
            Every
            <input v-model.number="control.state.snapshot.refreshValue" type="number" min="1" step="1" :disabled="!control.selectedBatch || !control.state.snapshot.schedulesAvailable" @change="control.invalidateSnapshot">
          </label>
          <label>
            Unit
            <select v-model="control.state.snapshot.refreshUnit" :disabled="!control.selectedBatch || !control.state.snapshot.schedulesAvailable" @change="control.invalidateSnapshot">
              <option value="minutes">minutes</option>
              <option value="hours">hours</option>
              <option value="days">days</option>
            </select>
          </label>
          <label>
            Available from
            <input v-model="control.state.snapshot.availableFrom" type="datetime-local" step="60" :disabled="!control.selectedBatch || !control.state.snapshot.schedulesAvailable" @change="control.invalidateSnapshot">
          </label>
          <label>
            Available until
            <input v-model="control.state.snapshot.availableUntil" type="datetime-local" step="60" :disabled="!control.selectedBatch || !control.state.snapshot.schedulesAvailable" @change="control.invalidateSnapshot">
          </label>
        </div>
        <p
          class="status snapshot-refresh-policy-status"
          :class="control.state.snapshot.scheduleStatusKind"
          role="status"
          aria-live="polite"
        >{{ control.state.snapshot.scheduleStatus }}</p>
        <fieldset class="snapshot-evidence-fieldset">
          <legend>Public Evidence</legend>
          <p>Only administrator-selected CIDs from the public evidence allowlist enter the preview.</p>
          <div class="snapshot-evidence-list">
            <label v-for="item in control.evidence" :key="`${item.stage}-${item.category}-${item.cid}`" class="snapshot-evidence-option">
              <input
                v-model="control.state.snapshot.selectedEvidence"
                type="checkbox"
                :value="`${item.stage}|${item.category}|${item.cid}`"
                @change="control.invalidateSnapshot"
              >
              <span><strong>{{ item.label }} · {{ item.stage }}</strong><code>{{ item.cid }}</code></span>
            </label>
            <p v-if="control.evidence.length === 0" class="empty-copy">No approved public evidence is attached to this batch.</p>
          </div>
        </fieldset>
        <div class="snapshot-actions">
          <button type="submit" :disabled="!control.selectedBatch || !control.state.snapshot.schedulesAvailable || Boolean(control.state.snapshot.operation)">
            {{ control.state.snapshot.operation === 'preview' ? 'Generating preview…' : 'Generate Snapshot Preview' }}
          </button>
          <p class="status" :class="control.state.snapshot.statusKind" role="status" aria-live="polite">
            {{ control.state.snapshot.status }}
          </p>
        </div>
      </form>

      <section v-if="control.state.snapshot.preview" class="snapshot-preview" aria-label="Generated Snapshot preview">
        <dl class="snapshot-metadata">
          <div><dt>Snapshot ID</dt><dd>{{ control.state.snapshot.preview.snapshotId }}</dd></div>
          <div><dt>Public Root</dt><dd>{{ control.state.snapshot.preview.publicRoot }}</dd></div>
          <div><dt>Final Private Block Hash</dt><dd>{{ control.state.snapshot.preview.finalPrivateBlockHash }}</dd></div>
          <div><dt>Public Fields</dt><dd>{{ control.state.snapshot.preview.publicFieldCount }} fields · {{ control.state.snapshot.preview.selectedEvidenceCount }} evidence CID(s)</dd></div>
        </dl>
        <details class="snapshot-manifest" open>
          <summary>Public Manifest</summary>
          <pre>{{ JSON.stringify(control.state.snapshot.preview.manifest, null, 2) }}</pre>
        </details>
        <details class="snapshot-exclusions">
          <summary>Excluded Private Data</summary>
          <ul><li v-for="field in control.state.snapshot.preview.excludedFields || []" :key="field">{{ field }}</li></ul>
        </details>
        <div class="snapshot-publish-actions">
          <button class="button-accent" type="button" :disabled="!control.state.snapshot.publicationCandidate || Boolean(control.state.snapshot.operation)" @click="control.publishSnapshot">
            {{ control.state.snapshot.operation === 'publish' ? 'Publishing…' : control.state.snapshot.published ? 'Published' : 'Publish to Local Public Chain' }}
          </button>
          <p class="status" :class="control.state.snapshot.publishStatusKind" role="status" aria-live="polite">
            {{ control.state.snapshot.publishStatus }}
          </p>
        </div>
      </section>
    </div>
  </details>
</template>
