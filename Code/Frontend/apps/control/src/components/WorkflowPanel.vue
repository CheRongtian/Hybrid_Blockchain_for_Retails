<template>
  <section id="workflow-panel" class="record-card workflow-card" aria-labelledby="workflow-title">
    <header>
      <div>
        <p class="eyebrow">ROUTE WORKFLOW</p>
        <h2 id="workflow-title">Supply Chain Route</h2>
        <p class="section-description">Assign participants, connect stages, and review the active route revision.</p>
      </div>
      <span id="workflow-route-badge" class="badge pending">Loading route</span>
    </header>

    <div class="workflow-toolbar" aria-label="Route editing tools">
      <label>
        Route scope
        <select id="workflow-batch-select">
          <option value="">Default route</option>
        </select>
      </label>
      <label>
        Add route node
        <select id="workflow-node-type">
          <option value="transport">Transport</option>
          <option value="warehouse">Warehouse</option>
        </select>
      </label>
      <label>
        Assigned participant
        <select id="workflow-node-account">
          <option value="">Select account</option>
        </select>
      </label>
      <button id="workflow-add-node" type="button">Add node</button>
      <button id="workflow-delete-node" class="workflow-secondary" type="button" disabled>
        Delete selected
      </button>
      <button id="workflow-delete-edge" class="workflow-secondary" type="button" disabled>
        Remove connection
      </button>
      <button id="workflow-auto-layout" class="workflow-secondary" type="button">Auto arrange</button>
      <button id="workflow-reset-route" class="workflow-secondary" type="button">Reload route</button>
      <button id="workflow-undo" class="workflow-secondary" type="button" disabled>Undo</button>
      <button id="workflow-redo" class="workflow-secondary" type="button" disabled>Redo</button>
    </div>

    <div class="workflow-connect-panel" aria-labelledby="workflow-connect-title">
      <div class="workflow-connect-heading">
        <strong id="workflow-connect-title">Keyboard connection</strong>
        <span>Choose two stages to create the same connection as dragging between handles.</span>
      </div>
      <label>
        From
        <select id="workflow-connect-from" aria-label="Connection source node"></select>
      </label>
      <label>
        To
        <select id="workflow-connect-to" aria-label="Connection target node"></select>
      </label>
      <button id="workflow-connect-nodes" class="workflow-secondary" type="button">Connect nodes</button>
    </div>

    <p id="workflow-help" class="workflow-help">
      Drag nodes or the background to navigate. Pinch to zoom. Use handles or the keyboard connection controls to connect stages.
    </p>
    <div
      id="workflow-canvas"
      class="workflow-canvas"
      role="application"
      aria-label="Editable supply chain route"
      aria-describedby="workflow-help"
    >
      <div id="workflow-scene" class="workflow-scene">
        <svg id="workflow-edge-layer" class="workflow-edge-layer" aria-hidden="true"></svg>
        <div id="workflow-node-layer" class="workflow-node-layer"></div>
      </div>
      <div class="workflow-canvas-controls" aria-label="Canvas controls">
        <button id="workflow-zoom-out" type="button" aria-label="Zoom out">−</button>
        <span id="workflow-zoom-level" class="workflow-zoom-level" aria-live="polite">100%</span>
        <button id="workflow-fit-view" type="button">Fit route</button>
        <button id="workflow-zoom-in" type="button" aria-label="Zoom in">+</button>
      </div>
      <p class="workflow-canvas-tip">Drag or two-finger scroll to pan · Pinch to zoom · Use × to remove connections</p>
    </div>
    <p id="workflow-status" class="status" role="status" aria-live="polite">Loading route...</p>
  </section>
</template>
