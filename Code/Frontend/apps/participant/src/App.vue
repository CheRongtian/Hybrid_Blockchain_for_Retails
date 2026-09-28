<script setup>
import LoginPanel from "./components/LoginPanel.vue";
import RecordWorkspace from "./components/RecordWorkspace.vue";
import SubmissionDialog from "./components/SubmissionDialog.vue";
import { useParticipantRecord } from "./composables/useParticipantRecord.js";

const record = useParticipantRecord();
</script>

<template>
  <a class="skip-link" href="#main-content">Skip to main content</a>
  <main id="main-content" class="participant-shell">
    <LoginPanel v-if="!record.state.session" :record="record" />
    <RecordWorkspace v-else :record="record" />
    <SubmissionDialog :record="record" />
  </main>
</template>
