<script setup>
defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <header class="control-topbar">
    <div>
      <p class="eyebrow">NETWORK OPERATIONS</p>
      <h1>Supply route control</h1>
      <p class="identity-status">Logged in: {{ control.identity }}</p>
    </div>
    <span class="environment-badge"><i></i> Private chain online</span>
  </header>
</template>
