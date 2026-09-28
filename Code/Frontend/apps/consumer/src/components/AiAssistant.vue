<script setup>
import { nextTick, onBeforeUnmount, ref, watch } from "vue";

const props = defineProps({ assistant: { type: Object, required: true } });
const input = ref(null);
const messageLog = ref(null);

watch(() => props.assistant.open, async (open) => {
  document.body.classList.toggle("assistant-open", open);
  if (!open) return;
  await nextTick();
  input.value?.focus();
});

watch(() => props.assistant.messages.length, async () => {
  await nextTick();
  messageLog.value?.scrollTo({
    top: messageLog.value.scrollHeight,
    behavior: window.matchMedia("(prefers-reduced-motion: reduce)").matches ? "auto" : "smooth",
  });
});

onBeforeUnmount(() => document.body.classList.remove("assistant-open"));
</script>

<template>
  <button
    v-if="assistant.available"
    class="assistant-launcher"
    type="button"
    aria-haspopup="dialog"
    @click="assistant.openAssistant"
  >
    <span class="assistant-launcher-mark" aria-hidden="true">AI</span>
    <span><strong>Ask the verified trace</strong><small>Answers use this Snapshot only</small></span>
    <svg viewBox="0 0 24 24" aria-hidden="true"><path d="m9 18 6-6-6-6"/></svg>
  </button>

  <section
    v-if="assistant.open"
    class="assistant-overlay"
    role="dialog"
    aria-modal="true"
    aria-labelledby="assistant-title"
    @click.self="assistant.closeAssistant"
  >
    <div class="assistant-dialog">
      <header class="assistant-header">
        <div>
          <p class="eyebrow">VERIFIED TRACE ASSISTANT</p>
          <h2 id="assistant-title">Ask about this product</h2>
        </div>
        <button class="icon-button" type="button" aria-label="Close AI assistant" @click="assistant.closeAssistant">
          <svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6 6 18"/></svg>
        </button>
      </header>
      <p class="assistant-intro">Answers are constrained to the currently verified public trace.</p>
      <div ref="messageLog" class="assistant-messages" role="log" aria-live="polite" aria-relevant="additions">
        <article v-for="message in assistant.messages" :key="message.id" class="assistant-message" :class="message.role">
          <span class="assistant-avatar" aria-hidden="true">{{ message.role === 'user' ? 'ME' : 'AI' }}</span>
          <div><time :datetime="message.isoTime">{{ message.time }}</time><p>{{ message.content }}</p></div>
        </article>
      </div>
      <form class="assistant-form" @submit.prevent="assistant.submit">
        <label for="assistant-input">Your question</label>
        <div class="assistant-input-row">
          <input
            id="assistant-input"
            ref="input"
            v-model="assistant.question"
            type="text"
            maxlength="1000"
            autocomplete="off"
            placeholder="Ask about origin, dates, or route..."
            :disabled="assistant.busy"
            required
          >
          <button type="submit" :disabled="assistant.busy || !assistant.question.trim()">
            {{ assistant.busy ? 'Thinking…' : 'Send' }}
          </button>
        </div>
      </form>
      <p class="assistant-status" :class="{ error: assistant.error }" role="status" aria-live="polite">
        {{ assistant.status }}
      </p>
    </div>
  </section>
</template>
