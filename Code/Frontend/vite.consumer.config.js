import { createVueAppConfig } from "./vite.app.config.js";

export default createVueAppConfig({
  appName: "consumer",
  entry: "index.html",
  port: 5175,
  backendPort: 8082,
  proxyPaths: ["/api", "/qrcodes"],
});
