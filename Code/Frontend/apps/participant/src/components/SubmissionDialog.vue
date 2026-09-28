<script setup>
defineProps({ record: { type: Object, required: true } });
</script>

<template>
  <section v-if="record.state.resultOpen && record.state.result" class="result-overlay" role="dialog" aria-modal="true" aria-labelledby="result-title">
    <article class="result-dialog">
      <div class="result-icon" :class="{ failed: !record.state.result.success }" aria-hidden="true">
        <svg viewBox="0 0 24 24"><path v-if="record.state.result.success" d="m5 12 4 4L19 6"/><path v-else d="M7 7l10 10M17 7 7 17"/></svg>
      </div>
      <p class="eyebrow">SUBMISSION RESULT</p>
      <h2 id="result-title">{{ record.state.result.title }}</h2>
      <dl>
        <div><dt>Batch ID</dt><dd>{{ record.state.result.batchId }}</dd></div>
        <div><dt>Block ID</dt><dd>{{ record.state.result.blockId }}</dd></div>
        <div><dt>Next stage</dt><dd>{{ record.state.result.nextStage }}</dd></div>
        <div><dt>IPFS references</dt><dd>{{ record.state.result.ipfsCount }}</dd></div>
      </dl>
      <button class="primary-button" type="button" @click="record.closeResult">Confirm</button>
    </article>
  </section>
</template>
