<script setup>
import WorkflowCanvas from "./WorkflowCanvas.vue";

defineProps({ control: { type: Object, required: true } });
</script>

<template>
  <section id="workflow-panel" class="record-card workflow-card" aria-labelledby="workflow-title">
    <header>
      <div>
        <p class="eyebrow">ROUTE WORKFLOW</p>
        <h2 id="workflow-title">Supply Chain Route</h2>
        <p class="section-description">Assign participants, connect stages, and review the active route revision.</p>
      </div>
      <span class="badge" :class="control.workflowValidation.valid ? 'verified' : 'pending'">
        {{ control.state.workflow?.routeId || 'Loading route' }}
      </span>
    </header>

    <div class="workflow-toolbar" aria-label="Route editing tools">
      <label>
        Route scope
        <select :value="control.state.selectedScope" @change="control.selectScope($event.target.value)">
          <option value="">Default route</option>
          <option v-for="scope in control.state.scopes" :key="scope.batchId" :value="scope.batchId">
            {{ scope.batchId }} · {{ scope.product }}
          </option>
        </select>
      </label>
      <label>
        Add route node
        <select :value="control.state.nodeType" @change="control.setNodeType($event.target.value)">
          <option value="transport">Transport</option>
          <option value="warehouse">Warehouse</option>
        </select>
      </label>
      <label>
        Assigned participant
        <select v-model="control.state.nodeAccount">
          <option value="">Select account</option>
          <option v-for="account in control.nodeAccounts" :key="account.username" :value="account.username">
            {{ account.username }} · {{ account.organizationId || account.role }}
          </option>
        </select>
      </label>
      <button type="button" :disabled="!control.state.workflow || !control.state.nodeAccount" @click="control.addNode">Add node</button>
      <button class="workflow-secondary danger-action" type="button" :disabled="!control.state.selectedNodeId" @click="control.deleteNode">
        Delete selected
      </button>
      <button class="workflow-secondary danger-action" type="button" :disabled="control.state.selectedEdgeIndex < 0" @click="control.deleteEdge()">
        Remove connection
      </button>
      <button class="workflow-secondary" type="button" @click="control.autoArrange">Auto arrange</button>
      <button class="workflow-secondary" type="button" @click="control.reloadRoute">Reload route</button>
      <button class="workflow-secondary" type="button" :disabled="control.state.history.past.length === 0" @click="control.undo">Undo</button>
      <button class="workflow-secondary" type="button" :disabled="control.state.history.future.length === 0" @click="control.redo">Redo</button>
    </div>

    <div class="workflow-connect-panel" aria-labelledby="workflow-connect-title">
      <div class="workflow-connect-heading">
        <strong id="workflow-connect-title">Keyboard connection</strong>
        <span>Choose two stages to create the same connection as dragging between handles.</span>
      </div>
      <label>
        From
        <select v-model="control.state.connectFrom" aria-label="Connection source node">
          <option v-for="node in control.connectionSources" :key="node.id" :value="node.id">{{ node.label }}</option>
        </select>
      </label>
      <label>
        To
        <select v-model="control.state.connectTo" aria-label="Connection target node">
          <option v-for="node in control.connectionTargets" :key="node.id" :value="node.id">{{ node.label }}</option>
        </select>
      </label>
      <button class="workflow-secondary" type="button" :disabled="!control.state.connectFrom || !control.state.connectTo" @click="control.connectNodes()">Connect nodes</button>
    </div>

    <p id="workflow-help" class="workflow-help">
      Select or drag nodes to edit the route. Pinch to zoom. Use the handles or keyboard connection controls to connect stages.
    </p>
    <WorkflowCanvas v-if="control.state.workflow" :control="control" />
    <div v-else class="workflow-canvas workflow-empty">Loading route canvas…</div>
    <p class="status" :class="control.state.workflowStatusKind" role="status" aria-live="polite">
      {{ control.state.workflowStatus }}
    </p>
  </section>
</template>
