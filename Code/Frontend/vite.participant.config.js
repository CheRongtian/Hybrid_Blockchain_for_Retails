import { createVueAppConfig } from "./vite.app.config.js";

export default createVueAppConfig({
  appName: "participant",
  entry: "Home.html",
  port: 5174,
  backendPort: 8080,
});
