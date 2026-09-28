import { createVueAppConfig } from "./vite.app.config.js";

export default createVueAppConfig({
  appName: "control",
  entry: "Home.html",
  port: 5173,
  backendPort: 8081,
});
