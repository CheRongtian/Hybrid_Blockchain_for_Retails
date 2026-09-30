<script setup>
defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <section class="auth-card" aria-labelledby="login-title">
    <p class="eyebrow">SUPPLY CHAIN · CONTROL IDENTITY</p>
    <h1 id="login-title">Control Panel Login</h1>
    <p>Only administrator accounts can read and manage supply-chain verification records.</p>
    <form @submit.prevent="control.login">
      <label>
        Administrator Username
        <input v-model.trim="control.state.auth.username" name="username" placeholder="admin01" autocomplete="username" required>
      </label>
      <label>
        Password
        <input v-model="control.state.auth.password" name="password" type="password" autocomplete="current-password" required>
      </label>
      <label class="remember-login">
        <input v-model="control.state.auth.remember" name="remember" type="checkbox">
        <span>Remember me on this device</span>
      </label>
      <button type="submit" :disabled="control.state.auth.busy" :aria-busy="control.state.auth.busy">
        {{ control.state.auth.busy ? 'Authenticating…' : 'Log In to Control Panel' }}
      </button>
    </form>
    <p class="status" :class="control.state.auth.kind" role="status" aria-live="polite">
      {{ control.state.auth.status }}
    </p>
  </section>
</template>
