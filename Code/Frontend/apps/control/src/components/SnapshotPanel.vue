<template>
  <details id="snapshot-panel" class="record-card snapshot-card">
    <summary class="snapshot-summary">
      <div>
        <p class="eyebrow">PUBLIC SNAPSHOT</p>
        <h2>Consumer Data Preview</h2>
        <p>Review public fields, availability, and evidence before publication.</p>
      </div>
      <span id="snapshot-batch-count" class="badge pending">Loading batches</span>
    </summary>
    <div class="snapshot-content">
      <form id="snapshot-preview-form" class="snapshot-form">
        <label>
          Completed Batch
          <select id="snapshot-batch-select" name="batchId"></select>
        </label>
        <div class="snapshot-refresh-setting">
          <div class="snapshot-refresh-heading">
            <strong>Public availability and refresh</strong>
            <span id="snapshot-refresh-product">Select a completed batch</span>
          </div>
          <label>
            Every
            <input id="snapshot-refresh-value" type="number" min="1" step="1" value="1" disabled>
          </label>
          <label>
            Unit
            <select id="snapshot-refresh-unit" disabled>
              <option value="minutes">minutes</option>
              <option value="hours" selected>hours</option>
              <option value="days">days</option>
            </select>
          </label>
          <label>
            Available from
            <input id="snapshot-available-from" type="datetime-local" step="60" disabled>
          </label>
          <label>
            Available until
            <input id="snapshot-available-until" type="datetime-local" step="60" disabled>
          </label>
        </div>
        <p
          id="snapshot-refresh-policy-status"
          class="status snapshot-refresh-policy-status"
          role="status"
          aria-live="polite"
        ></p>
        <fieldset class="snapshot-evidence-fieldset">
          <legend>Public Evidence</legend>
          <p>Only administrator-selected CIDs from the public evidence allowlist enter the preview.</p>
          <div id="snapshot-evidence-list" class="snapshot-evidence-list"></div>
        </fieldset>
        <div class="snapshot-actions">
          <button id="generate-snapshot-button" type="submit">Generate Snapshot Preview</button>
          <p id="snapshot-status" class="status" role="status" aria-live="polite"></p>
        </div>
      </form>

      <section id="snapshot-preview" class="snapshot-preview" hidden aria-label="Generated Snapshot preview">
        <dl class="snapshot-metadata">
          <div><dt>Snapshot ID</dt><dd id="snapshot-id"></dd></div>
          <div><dt>Public Root</dt><dd id="snapshot-public-root"></dd></div>
          <div><dt>Final Private Block Hash</dt><dd id="snapshot-private-hash"></dd></div>
          <div><dt>Public Fields</dt><dd id="snapshot-field-count"></dd></div>
        </dl>
        <details class="snapshot-manifest" open>
          <summary>Public Manifest</summary>
          <pre id="snapshot-manifest-json"></pre>
        </details>
        <details class="snapshot-exclusions">
          <summary>Excluded Private Data</summary>
          <ul id="snapshot-excluded-fields"></ul>
        </details>
        <div class="snapshot-publish-actions">
          <button id="publish-snapshot-button" class="button-accent" type="button">Publish to Local Public Chain</button>
          <p id="snapshot-publish-status" class="status" role="status" aria-live="polite"></p>
        </div>
      </section>
    </div>
  </details>
</template>
