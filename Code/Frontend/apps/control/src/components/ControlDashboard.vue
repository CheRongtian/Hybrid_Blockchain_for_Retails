<script setup>
import ConfirmationPolicyPanel from "./ConfirmationPolicyPanel.vue";
import DashboardHeader from "./DashboardHeader.vue";
import RecordsPanel from "./RecordsPanel.vue";
import SnapshotPanel from "./SnapshotPanel.vue";
import WorkflowPanel from "./WorkflowPanel.vue";

defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <section class="control-app">
    <aside class="control-sidebar" aria-label="Control center navigation">
      <a class="control-brand" href="#control-content" aria-label="FreshLedger control center">
        <span class="brand-symbol" aria-hidden="true">
          <svg viewBox="0 0 24 24">
            <path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9" />
          </svg>
        </span>
        <span class="brand-copy">
          <strong>FreshLedger</strong>
          <small>Control center</small>
        </span>
      </a>

      <nav class="control-nav" aria-label="Control center sections">
        <a href="#workflow-panel"><span>01</span>Route workflow</a>
        <a href="#policy-panel"><span>02</span>Confirmation</a>
        <a href="#snapshot-panel"><span>03</span>Public Snapshot</a>
        <a href="#records-panel"><span>04</span>Records</a>
      </nav>

      <section class="sidebar-network" aria-label="Network status">
        <p>Network</p>
        <span><i aria-hidden="true"></i>Private chain online</span>
        <span><i aria-hidden="true"></i>Event stream live</span>
        <small>Local development environment</small>
      </section>

      <button class="button-secondary control-logout" type="button" @click="control.logout">Log out</button>
    </aside>

    <div id="control-content" class="control-content">
      <DashboardHeader :control="control" />
      <main class="dashboard-grid">
        <WorkflowPanel :control="control" />
        <ConfirmationPolicyPanel :control="control" />
        <SnapshotPanel :control="control" />
        <RecordsPanel :control="control" />
      </main>
    </div>
  </section>
</template>
