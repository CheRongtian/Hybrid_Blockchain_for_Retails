<script setup>
import ConfirmationPolicyPanel from "./ConfirmationPolicyPanel.vue";
import DashboardHeader from "./DashboardHeader.vue";
import RecordsPanel from "./RecordsPanel.vue";
import SnapshotPanel from "./SnapshotPanel.vue";
import WorkflowPanel from "./WorkflowPanel.vue";
</script>

<template>
  <section id="dashboard" class="control-app" hidden>
    <aside class="control-sidebar">
      <a class="control-brand" href="/" aria-label="FreshLedger control center">
        <span class="brand-symbol" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9"/></svg></span>
        <span><strong>FreshLedger</strong><small>Control center</small></span>
      </a>

      <nav class="control-nav" aria-label="Control center sections">
        <a href="#workflow-panel"><span>01</span> Route workflow</a>
        <a href="#policy-panel"><span>02</span> Confirmation</a>
        <a href="#snapshot-panel"><span>03</span> Public Snapshot</a>
        <a href="#records-panel"><span>04</span> Records</a>
      </nav>

      <div class="sidebar-network">
        <p>NETWORK</p>
        <span><i></i> Private chain online</span>
        <span><i></i> Event stream live</span>
        <small>Local development environment</small>
      </div>
    </aside>

    <div class="control-content">
      <DashboardHeader />
      <main class="dashboard-grid">
        <WorkflowPanel />
        <div class="configuration-grid">
          <ConfirmationPolicyPanel />
          <SnapshotPanel />
        </div>
        <RecordsPanel />
      </main>
    </div>
  </section>
</template>
