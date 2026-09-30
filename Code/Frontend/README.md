# Frontend workspace

This workspace owns the Vue 3 administrator, participant, and customer
interfaces plus shared design tokens. The QR display and
standalone Merkle visualizer remain small native HTML applications.

## Build

From this directory, install the declared dependencies and build all Vue apps:

    npm install
    npm run build

Production output is written to `dist/control`, `dist/participant`, and
`dist/consumer`. Build it before configuring the C++ project because CMake
synchronizes the private-page output into its server build directory. The
customer Node service reads `dist/consumer` directly.

The individual build commands are:

    npm run build:control
    npm run build:participant
    npm run build:consumer

For frontend development, keep the matching backend service running and use:

    npm run dev:control
    npm run dev:participant
    npm run dev:consumer

The development pages are `http://127.0.0.1:5173/Home.html`,
`http://127.0.0.1:5174/Home.html`, and `http://127.0.0.1:5175/`. Vite proxies
each app to its existing backend API.

Vue owns all three page shells, presentation, application state, API calls,
SSE updates, route editing, participant submissions, trace results, and the
assistant interaction. The administrator interface no longer loads the former
static CSS or imperative DOM runtime.
