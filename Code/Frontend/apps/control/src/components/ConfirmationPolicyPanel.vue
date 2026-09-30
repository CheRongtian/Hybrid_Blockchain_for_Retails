<script setup>
defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <details id="policy-panel" class="record-card policy-card" open>
    <summary class="policy-summary">
      <div>
        <p class="eyebrow">CONFIRMATION POLICY</p>
        <h2 id="policy-title">User confirmation methods</h2>
        <p>Configure confirmation for each connected route node and assigned account.</p>
      </div>
      <span class="badge" :class="control.state.policies.length ? 'verified' : 'pending'">
        {{ control.state.policies.length }} route nodes configured
      </span>
    </summary>
    <div class="policy-content">
      <form class="policy-form" @submit.prevent="control.savePolicies">
        <div class="role-policy-list">
          <div class="policy-matrix-header" aria-hidden="true">
            <span>Route node / account</span><span>Typed name</span><span>Handwritten</span><span>Face</span>
          </div>
          <div v-for="policy in control.state.policies" :key="policy.nodeId" class="role-policy">
            <div class="policy-role-name">
              <strong>{{ policy.nodeLabel || policy.nodeId }}</strong>
              <small>{{ policy.role || 'route' }} · {{ policy.username || 'unassigned' }}</small>
            </div>
            <label class="policy-option"><input v-model="policy.typedName" type="checkbox"><span>Typed name</span></label>
            <label class="policy-option"><input v-model="policy.handwritten" type="checkbox"><span>Handwritten</span></label>
            <label class="policy-option"><input v-model="policy.face" type="checkbox"><span>Face</span></label>
          </div>
          <p v-if="control.state.policies.length === 0" class="empty-copy">Connect the route to configure node policies.</p>
        </div>
        <div class="policy-actions">
          <button type="submit" :disabled="control.state.policyBusy || control.state.policies.length === 0">
            {{ control.state.policyBusy ? 'Saving policies…' : 'Save Route Node Policies' }}
          </button>
          <p class="status" :class="control.state.policyStatusKind" role="status" aria-live="polite">
            {{ control.state.policyStatus }}
          </p>
        </div>
      </form>
    </div>
  </details>
</template>
