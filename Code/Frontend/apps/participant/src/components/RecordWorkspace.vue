<script setup>
defineProps({ record: { type: Object, required: true } });
</script>

<template>
  <section class="participant-workspace">
    <aside class="participant-rail">
      <a class="participant-brand" href="/" aria-label="FreshLedger participant portal">
        <span class="brand-symbol" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="M4 7.5 12 3l8 4.5v9L12 21l-8-4.5zM4 7.5l8 4.5m8-4.5L12 12m0 9v-9"/></svg></span>
        <span><strong>FreshLedger</strong><small>Participant network</small></span>
      </a>

      <div class="rail-stage">
        <span>ASSIGNED STAGE</span>
        <strong>{{ record.stageLabel.value }}</strong>
        <small>{{ record.identityLabel.value }}</small>
      </div>

      <div class="rail-checklist" aria-label="Submission requirements">
        <p>SUBMISSION REQUIREMENTS</p>
        <span class="complete"><i>1</i> Batch context</span>
        <span :class="{ complete: record.state.confirmed }"><i>2</i> Event information</span>
        <span :class="{ complete: record.state.policy }"><i>3</i> Identity confirmation</span>
        <span :class="{ complete: record.state.uploadedReferences.length || record.state.selectedFiles.length }"><i>4</i> Evidence (optional)</span>
      </div>

      <button class="rail-logout" type="button" @click="record.logout">Log out</button>
    </aside>

    <div class="participant-main">
      <header class="workspace-heading">
        <div>
          <p class="eyebrow">MERKLE EVENT ENTRY</p>
          <h1>Confirm the next handoff.</h1>
          <p>Review inherited batch data, enter this stage's facts and sign the event before it joins the private chain.</p>
        </div>
        <span class="route-status"><i></i> Route synchronized</span>
      </header>

      <form class="record-form" @submit.prevent="record.submit($event.currentTarget)">
        <section class="form-card context-card">
          <header class="card-heading">
            <span>01</span><div><h2>Batch context</h2><p>Identity and inherited product facts.</p></div>
          </header>

          <div v-if="record.role.value === 'supplier'" class="field-grid one-column">
            <label>
              Product name
              <input v-model="record.state.product" placeholder="Pumpkins" maxlength="256" required>
              <small>A new batch ID is allocated by the server.</small>
            </label>
          </div>

          <template v-else>
            <label class="full-field">
              Assigned batch
              <select :value="record.state.selectedBatchId" required @change="record.selectBatch($event.target.value)">
                <option value="">Choose an assigned batch</option>
                <option v-for="batch in record.state.batches" :key="batch.batchId" :value="batch.batchId">
                  {{ batch.product }} · {{ batch.batchId }}
                </option>
              </select>
            </label>
            <div class="batch-facts">
              <article><span>Product</span><strong>{{ record.selectedBatch.value?.product || '—' }}</strong></article>
              <article><span>Harvest date</span><strong>{{ record.selectedBatch.value?.harvestDate || '—' }}</strong></article>
              <article><span>Farm</span><strong>{{ record.selectedBatch.value?.farmLocation || '—' }}</strong></article>
            </div>
            <p class="inline-status" :class="record.state.batchStatusKind" role="status" aria-live="polite">
              {{ record.state.batchStatus }}
            </p>
          </template>
        </section>

        <section class="form-card event-card">
          <header class="card-heading">
            <span>02</span><div><h2>{{ record.roleLabels[record.role.value] }} event</h2><p>Facts recorded in the next private-chain block.</p></div>
          </header>

          <div v-if="record.role.value === 'supplier'" class="field-grid">
            <label>Harvest date<input v-model="record.state.event.harvestDate" type="date" required></label>
            <label>Farm location<input v-model="record.state.event.farmLocation" maxlength="256" placeholder="Farm location or coded ID" required></label>
            <label>Certificate ID<input v-model="record.state.event.certificateId" pattern="CERT-[0-9]{4}" title="Use CERT-0001" required></label>
          </div>

          <div v-else-if="record.role.value === 'logistics'" class="field-grid">
            <label>Shipment ID<input v-model="record.state.event.shipmentId" readonly required></label>
            <label>Vehicle / container<input v-model="record.state.event.vehicleContainerId" readonly required></label>
            <label>Pickup location<input v-model="record.state.event.pickupLocation" maxlength="256" required></label>
            <label>Delivery location<input v-model="record.state.event.deliveryLocation" readonly required></label>
            <label>Departure time<input v-model="record.state.event.departureTime" type="datetime-local" required></label>
            <label>Arrival time<input v-model="record.state.event.arrivalTime" type="datetime-local" required></label>
            <label>Temperature
              <span class="measurement-field"><input v-model="record.state.event.temperature" inputmode="decimal" pattern="[+-]?[0-9]+([.][0-9]+)?([ ]*-[ ]*[+-]?[0-9]+([.][0-9]+)?)?" required><select v-model="record.state.event.temperatureUnit"><option value="C">°C</option><option value="F">°F</option></select></span>
            </label>
            <label>Humidity
              <span class="measurement-field"><input v-model="record.state.event.humidity" inputmode="decimal" pattern="[0-9]+([.][0-9]+)?([ ]*-[ ]*[0-9]+([.][0-9]+)?)?" required><span>% RH</span></span>
            </label>
          </div>

          <div v-else-if="record.role.value === 'warehouse'" class="field-grid">
            <label>Storage lot ID<input v-model="record.state.event.storageLotId" readonly required></label>
            <label>Storage zone / rack<input v-model="record.state.event.storageZoneRackId" readonly required></label>
            <label>Inbound time<input v-model="record.state.event.inboundTime" type="datetime-local" required></label>
            <label>Outbound time<input v-model="record.state.event.outboundTime" type="datetime-local" required></label>
            <label>Temperature
              <span class="measurement-field"><input v-model="record.state.event.temperature" inputmode="decimal" pattern="[+-]?[0-9]+([.][0-9]+)?([ ]*-[ ]*[+-]?[0-9]+([.][0-9]+)?)?" required><select v-model="record.state.event.temperatureUnit"><option value="C">°C</option><option value="F">°F</option></select></span>
            </label>
            <label>Humidity
              <span class="measurement-field"><input v-model="record.state.event.humidity" inputmode="decimal" pattern="[0-9]+([.][0-9]+)?([ ]*-[ ]*[0-9]+([.][0-9]+)?)?" required><span>% RH</span></span>
            </label>
          </div>

          <div v-else-if="record.role.value === 'supermarket'" class="field-grid">
            <label>Shelf placement date<input v-model="record.state.event.shelfPlacementDate" type="date" required></label>
            <label>Expiration / sell-by date<input v-model="record.state.event.expirationSellByDate" type="date" required></label>
            <label>Store location ID<input v-model="record.state.event.storeLocationId" pattern="STORE-[0-9]{4}" title="Use STORE-0001" required></label>
          </div>
        </section>

        <section class="form-card evidence-card">
          <header class="card-heading">
            <span>03</span><div><h2>Public evidence reference</h2><p>Files are uploaded to the configured IPFS node.</p></div>
          </header>
          <div class="evidence-inputs">
            <label>Attachment category
              <select v-model="record.state.attachmentCategory">
                <option v-for="category in record.attachmentCategories.value" :key="category.value" :value="category.value">{{ category.label }}</option>
              </select>
            </label>
            <label>Choose files<input type="file" multiple @change="record.selectFiles($event.target.files)"></label>
          </div>
          <div v-if="record.attachmentItems.value.length" class="attachment-list">
            <article v-for="item in record.attachmentItems.value" :key="item.key"><strong>{{ item.title }}</strong><small>{{ item.meta }}</small></article>
          </div>
          <p v-else class="empty-evidence">No files selected. Evidence is optional for this event.</p>
        </section>

        <section class="form-card confirmation-card">
          <header class="card-heading">
            <span>04</span><div><h2>Identity confirmation</h2><p>Sign this exact event with an allowed confirmation method.</p></div>
          </header>
          <p v-if="record.state.policy" class="policy-copy">
            Policy for <strong>{{ record.state.policy.nodeLabel || record.stageLabel.value }}</strong>
            <span v-if="record.state.policy.username"> · {{ record.state.policy.username }}</span>
          </p>
          <div class="confirmation-methods">
            <label v-for="method in record.availableMethods.value" :key="method.value" :class="{ disabled: !method.supported }">
              <input v-model="record.state.confirmationMethod" type="radio" :value="method.value" :disabled="!method.supported" @change="record.validateTypedName">
              <span><strong>{{ method.label }}</strong><small>{{ method.supported ? 'Available' : 'Unavailable in this prototype' }}</small></span>
            </label>
          </div>
          <label v-if="record.state.confirmationMethod === 'typed_name'" class="full-field">
            Registered display name
            <input v-model="record.state.confirmationName" maxlength="256" autocomplete="off" placeholder="Type your registered name" required @input="record.validateTypedName">
          </label>
          <p class="inline-status error" role="alert" aria-live="polite">{{ record.state.confirmationError }}</p>
          <label class="final-confirmation">
            <input v-model="record.state.confirmed" type="checkbox" required>
            <span><strong>I confirm this event is accurate.</strong><small>The signed data becomes part of the private chain.</small></span>
          </label>
        </section>

        <footer class="submission-bar">
          <div>
            <p class="inline-status" :class="record.state.statusKind" role="status" aria-live="polite">{{ record.state.status }}</p>
            <p class="signature-status">{{ record.state.signatureStatus }}</p>
          </div>
          <button class="secondary-button" type="button" :disabled="record.state.submitting" @click="record.clearForm">Clear form</button>
          <button class="primary-button" type="submit" :disabled="!record.canSubmit.value">
            {{ record.state.submitting ? 'Creating Merkle block…' : record.state.completed ? 'Merkle block created' : 'Sign and submit event' }}
          </button>
        </footer>
      </form>
    </div>
  </section>
</template>
