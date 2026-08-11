const loginCard = document.querySelector("#login-card");
const loginForm = document.querySelector("#login-form");
const loginStatus = document.querySelector("#login-status");
const recordCard = document.querySelector("#record-card");
const identityStatus = document.querySelector("#identity-status");
const logoutButton = document.querySelector("#logout-button");
const form = document.querySelector("#record-form");
const statusLine = document.querySelector("#request-status");
const resultCard = document.querySelector("#result-card");
const verificationTitle = document.querySelector("#verification-title");
const controlApiBase = "http://127.0.0.1:8081/api";
const sessionKey = "supply-chain-user-session";
let session = null;

function setSession(result) {
    session = result;
    sessionStorage.setItem(sessionKey, JSON.stringify(result));
    loginCard.hidden = true;
    recordCard.hidden = false;
    identityStatus.textContent =
        `Logged in: ${result.user.username} · ${result.user.role} · ${result.user.organizationId}`;
}

function clearSession() {
    session = null;
    sessionStorage.removeItem(sessionKey);
    loginCard.hidden = false;
    recordCard.hidden = true;
}

async function login(event) {
    event.preventDefault();
    loginStatus.textContent = "Authenticating…";
    loginStatus.className = "request-status pending";

    try {
        const response = await fetch(`${controlApiBase}/auth/login`, {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
            },
            body: new URLSearchParams(new FormData(loginForm)).toString()
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);

        loginForm.reset();
        setSession(result);
        loginStatus.textContent = "Identity verified.";
    } catch (error) {
        loginStatus.textContent = error.message;
        loginStatus.className = "request-status error";
    }
}

async function restoreSession() {
    const saved = sessionStorage.getItem(sessionKey);
    if (!saved) return;

    try {
        const stored = JSON.parse(saved);
        const response = await fetch(`${controlApiBase}/auth/me`, {
            headers: { Authorization: `Bearer ${stored.token}` }
        });
        if (!response.ok) throw new Error("Session expired");
        setSession({ token: stored.token, user: await response.json() });
    } catch {
        clearSession();
    }
}

loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", clearSession);

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    if (!session) return;
    statusLine.textContent = "Generating and verifying Merkle proof...";
    statusLine.className = "request-status pending";
    resultCard.hidden = true;

    const body = new URLSearchParams(new FormData(form));
    body.set("confirmed", document.querySelector("#confirmed").checked ? "true" : "false");

    try {
        const response = await fetch(`${controlApiBase}/records`, {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
                Authorization: `Bearer ${session.token}`
            },
            body: body.toString()
        });

        const result = await response.json();
        if (response.status === 401) {
            clearSession();
            throw new Error("Your session has expired. Please log in again.");
        }
        if (!response.ok) {
            throw new Error(result.error || `Request failed: ${response.status}`);
        }

        verificationTitle.textContent = result.verified ? "Verified ✓" : "Verification failed";
        verificationTitle.className = result.verified ? "verified" : "failed";
        document.querySelector("#block-id").textContent = result.blockID;
        resultCard.hidden = false;
        form.reset();

        statusLine.textContent = result.verified
            ? "Record saved. Merkle proof verified."
            : "Record saved, but Merkle proof verification failed.";
        statusLine.className = result.verified
            ? "request-status success"
            : "request-status error";
    } catch (error) {
        statusLine.textContent = error.message;
        statusLine.className = "request-status error";
    }
});

restoreSession();
