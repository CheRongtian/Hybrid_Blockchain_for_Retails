import App from "./App.vue";
import { mountRuntimeApp } from "../../../shared/runtime/mountRuntimeApp.js";
import "../../../../PrivateChain/Server/control_static/css/style.css";
import "../../../shared/styles/tokens.css";
import "./styles/control.css";

mountRuntimeApp({
  App,
  loadRuntime: () => import("../../../../PrivateChain/Server/control_static/js/main.js"),
  name: "control",
  statusSelector: "#login-status",
});
