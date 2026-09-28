import { createApp, nextTick } from "vue";

export async function mountRuntimeApp({
  App,
  loadRuntime,
  name,
  statusSelector,
}) {
  createApp(App).mount("#app");
  await nextTick();

  try {
    await loadRuntime();
    document.documentElement.dataset.uiReady = "true";
  } catch (error) {
    const status = document.querySelector(statusSelector);
    if (status) {
      status.className = "status error request-status";
      status.textContent = `The ${name} interface could not start. Reload the page or review the browser console.`;
    }
    console.error(`${name} interface bootstrap failed`, error);
  }
}
