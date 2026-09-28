import { fileURLToPath, URL } from "node:url";
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

const frontendRoot = fileURLToPath(new URL(".", import.meta.url));
const codeRoot = fileURLToPath(new URL("..", import.meta.url));

export function createVueAppConfig({
  appName,
  entry,
  port,
  backendPort,
  proxyPaths = ["/api"],
}) {
  const appRoot = fileURLToPath(new URL(`./apps/${appName}/`, import.meta.url));
  const proxy = Object.fromEntries(proxyPaths.map((path) => [
    path,
    `http://127.0.0.1:${backendPort}`,
  ]));

  return defineConfig({
    root: appRoot,
    base: "/",
    plugins: [vue()],
    build: {
      outDir: fileURLToPath(new URL(`./dist/${appName}`, import.meta.url)),
      emptyOutDir: true,
      rollupOptions: {
        input: fileURLToPath(new URL(`./apps/${appName}/${entry}`, import.meta.url)),
      },
    },
    server: {
      port,
      strictPort: true,
      fs: {
        allow: [frontendRoot, codeRoot],
      },
      proxy,
    },
  });
}
