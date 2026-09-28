<template>
  <details id="policy-panel" class="record-card policy-card">
    <summary class="policy-summary">
      <div>
        <p class="eyebrow">CONFIRMATION POLICY</p>
        <h2 id="policy-title">User confirmation methods</h2>
        <p>Configure confirmation for each connected route node and assigned account.</p>
      </div>
      <span id="policy-role-count" class="badge pending">Loading policies</span>
    </summary>
    <div class="policy-content">
      <form id="confirmation-policy-form" class="policy-form">
        <div id="role-policy-list" class="role-policy-list"></div>
        <div class="policy-actions">
          <button id="save-policy-button" type="submit">Save Route Node Policies</button>
          <p id="policy-status" class="status" role="status" aria-live="polite"></p>
        </div>
      </form>
    </div>
  </details>
</template>
