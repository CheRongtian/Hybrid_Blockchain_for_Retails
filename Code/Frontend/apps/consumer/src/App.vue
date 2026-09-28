<script setup>
import SearchHero from "./components/SearchHero.vue";
import TraceResult from "./components/TraceResult.vue";
import { useConsumerTrace } from "./composables/useConsumerTrace.js";

const trace = useConsumerTrace();
</script>

<template>
  <a class="skip-link" href="#main-content">Skip to main content</a>
  <main id="main-content" class="consumer-shell">
    <SearchHero
      v-if="!trace.state.verificationOnly || !trace.state.trace"
      :batches="trace.state.batches"
      :selected-batch-id="trace.state.selectedBatchId"
      :status="trace.state.status"
      :status-kind="trace.state.statusKind"
      :loading="trace.state.loadingBatches || trace.state.loadingTrace"
      :verification-only="trace.state.verificationOnly"
      @select="trace.selectBatch"
    />
    <TraceResult
      v-if="trace.state.trace"
      :trace="trace.state.trace"
      :stages="trace.stages.value"
      :selected-stage-index="trace.state.selectedStageIndex"
      :verification-only="trace.state.verificationOnly"
      :assistant="trace.assistant"
      @select-stage="trace.selectStage"
    />
  </main>
</template>
