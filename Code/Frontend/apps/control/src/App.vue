<script setup>
import ControlDashboard from "./components/ControlDashboard.vue";
import LoginPanel from "./components/LoginPanel.vue";
</script>

<template>
  <a class="skip-link" href="#main-content">Skip to main content</a>
  <main id="main-content" class="control-root">
    <LoginPanel />
    <ControlDashboard />
  </main>
</template>
