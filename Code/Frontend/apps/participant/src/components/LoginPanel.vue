<script setup>
defineProps({ record: { type: Object, required: true } });
</script>

<template>
  <section class="participant-auth">
    <div class="auth-story">
      <a class="participant-brand" href="/" aria-label="FreshLedger participant portal">
        <span class="brand-symbol" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9"/></svg></span>
        <span><strong>FreshLedger</strong><small>Participant network</small></span>
      </a>
      <div>
        <p class="eyebrow">SIGNED SUPPLY CHAIN EVENTS</p>
        <h1>One verified handoff at a time.</h1>
        <p>Confirm your assigned route event, attach evidence and create the next Merkle-backed block.</p>
      </div>
      <ul class="auth-features">
        <li><span>01</span> Role-scoped event fields</li>
        <li><span>02</span> ECDSA identity confirmation</li>
        <li><span>03</span> IPFS evidence references</li>
      </ul>
    </div>

    <div class="auth-form-column">
      <form class="auth-form" @submit.prevent="record.login">
        <div>
          <p class="auth-label">PARTICIPANT ACCESS</p>
          <h2>Continue your route</h2>
          <p>Use the account assigned to the next stage.</p>
        </div>
        <label>
          Username
          <input v-model="record.state.auth.username" autocomplete="username" placeholder="supplier01" required>
        </label>
        <label>
          Password
          <input v-model="record.state.auth.password" type="password" autocomplete="current-password" required>
        </label>
        <label class="checkbox-row">
          <input v-model="record.state.auth.remember" type="checkbox">
          <span>Remember this session on this device</span>
        </label>
        <button class="primary-button" type="submit" :disabled="record.state.auth.busy">
          {{ record.state.auth.busy ? 'Authenticating…' : 'Enter participant portal' }}
        </button>
        <p class="inline-status" :class="record.state.auth.kind" role="status" aria-live="polite">
          {{ record.state.auth.status }}
        </p>
      </form>
    </div>
  </section>
</template>
