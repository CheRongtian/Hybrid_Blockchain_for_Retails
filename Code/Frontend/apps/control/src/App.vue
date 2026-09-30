<script setup>
import ControlDashboard from "./components/ControlDashboard.vue";
import LoginPanel from "./components/LoginPanel.vue";
import { useControlCenter } from "./composables/useControlCenter.js";

const control = useControlCenter();
</script>

<template>
  <a class="skip-link" href="#main-content">Skip to main content</a>
  <main id="main-content" class="control-root">
    <LoginPanel v-if="!control.state.session" :control="control" />
    <ControlDashboard v-else :control="control" />
  </main>
</template>
