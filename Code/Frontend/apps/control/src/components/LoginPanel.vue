<template>
  <section id="login-card" class="control-auth" aria-labelledby="login-title">
    <div class="control-auth-story">
      <a class="control-brand" href="/" aria-label="FreshLedger control center">
        <span class="brand-symbol" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9"/></svg></span>
        <span><strong>FreshLedger</strong><small>Control center</small></span>
      </a>
      <div class="control-auth-copy">
        <p class="eyebrow">PRIVATE CHAIN OPERATIONS</p>
        <h1>Route integrity, under control.</h1>
        <p>Operate the private supply route, inspect Merkle-backed records and publish verified public Snapshots from one console.</p>
      </div>
      <div class="control-auth-stats">
        <span><strong>04</strong> Route roles</span>
        <span><strong>P-256</strong> Signatures</span>
        <span><strong>SHA-256</strong> Merkle anchors</span>
      </div>
    </div>

    <div class="control-login-column">
      <form id="login-form" class="control-login-form">
        <div>
          <p class="auth-label">ADMINISTRATOR ACCESS</p>
          <h2 id="login-title">Open the control center</h2>
          <p>Authenticate with a private-chain administrator account.</p>
        </div>
        <label>
          Administrator username
          <input name="username" placeholder="admin01" autocomplete="username" required>
        </label>
        <label>
          Password
          <input name="password" type="password" autocomplete="current-password" required>
        </label>
        <label class="remember-login">
          <input id="remember-login" name="remember" value="true" type="checkbox">
          <span>Remember this session on this device</span>
        </label>
        <button type="submit">Enter control center</button>
        <p id="login-status" class="status" role="status" aria-live="polite"></p>
      </form>
    </div>
  </section>
</template>
