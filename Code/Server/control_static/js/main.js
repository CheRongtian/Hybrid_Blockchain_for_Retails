const loginCard = document.querySelector("#login-card");
const loginForm = document.querySelector("#login-form");
const loginStatus = document.querySelector("#login-status");
const dashboard = document.querySelector("#dashboard");
const identityStatus = document.querySelector("#identity-status");
const logoutButton = document.querySelector("#logout-button");
const list = document.querySelector("#record-list");
const statusLine = document.querySelector("#load-status");
const refreshButton = document.querySelector("#refresh-button");
const sessionKey = "supply-chain-control-session";
let session = null;

function setSession(result) {
    session = result;
    sessionStorage.setItem(sessionKey, JSON.stringify(result));
    loginCard.hidden = true;
    dashboard.hidden = false;
    identityStatus.textContent =
        `Logged in: ${result.user.username} · ${result.user.role} · ${result.user.organizationId}`;
}

function clearSession() {
    session = null;
    sessionStorage.removeItem(sessionKey);
    loginCard.hidden = false;
    dashboard.hidden = true;
    list.replaceChildren();
}

async function login(event) {
    event.preventDefault();
    loginStatus.textContent = "Authenticating administrator...";
    loginStatus.className = "status pending";

    try {
        const response = await fetch("/api/auth/login", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
            },
            body: new URLSearchParams(new FormData(loginForm)).toString()
        });
        const result = await response.json();
        if (!response.ok) throw new Error(result.error || `Login failed: ${response.status}`);
        if (result.user.role !== "admin") throw new Error("This account does not have control-panel access.");

        loginForm.reset();
        setSession(result);
        loadRecords();
    } catch (error) {
        loginStatus.textContent = error.message;
        loginStatus.className = "status error";
    }
}

async function restoreSession() {
    const saved = sessionStorage.getItem(sessionKey);
    if (!saved) return;

    try {
        const stored = JSON.parse(saved);
        const response = await fetch("/api/auth/me", {
            headers: { Authorization: `Bearer ${stored.token}` }
        });
        const user = await response.json();
        if (!response.ok || user.role !== "admin") throw new Error("Session expired");
        setSession({ token: stored.token, user });
        loadRecords();
    } catch {
        clearSession();
    }
}

function field(label, value) {
    const item = document.createElement("div");
    const term = document.createElement("dt");
    const description = document.createElement("dd");
    term.textContent = label;
    description.textContent = value;
    item.append(term, description);
    return item;
}

function detail(label, value) {
    const section = document.createElement("details");
    const summary = document.createElement("summary");
    const content = document.createElement("pre");
    summary.textContent = label;
    content.textContent = value;
    section.append(summary, content);
    return section;
}

function formatSubmissionTime(value) {
    const utcText = value.includes("T") ? value : value.replace(" ", "T");
    const utcIso = utcText.endsWith("Z") ? utcText : `${utcText}Z`;
    const timestamp = new Date(utcIso);

    const pad = (number) => String(number).padStart(2, "0");
    const formatDateTime = (date, timeZone) => {
        const parts = new Intl.DateTimeFormat("en-CA", {
            year: "numeric",
            month: "2-digit",
            day: "2-digit",
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit",
            hourCycle: "h23",
            timeZone,
            timeZoneName: "short"
        }).formatToParts(date);
        const values = Object.fromEntries(parts.map(({ type, value: part }) => [type, part]));
        return `${values.year}-${values.month}-${values.day} ` +
            `${values.hour}:${values.minute}:${values.second} ${values.timeZoneName}`;
    };

    if (Number.isNaN(timestamp.getTime())) {
        return {
            utc: `${value} UTC`,
            local: "Unable to convert to local time"
        };
    }

    return {
        utc: `${formatDateTime(timestamp, "UTC").replace(" UTC", "")} UTC`,
        local: formatDateTime(timestamp)
    };
}

function renderRecord(record) {
    const card = document.createElement("article");
    card.className = "record-card";

    const header = document.createElement("header");
    const title = document.createElement("h2");
    const badge = document.createElement("span");
    title.textContent = `Block ${record.blockID}`;
    badge.textContent = record.verified ? "Verified" : "Failed";
    badge.className = record.verified ? "badge verified" : "badge failed";
    header.append(title, badge);

    const submissionTime = formatSubmissionTime(record.createdAt);
    const fields = document.createElement("dl");
    fields.append(
        field("Batch ID", record.batchId),
        field("Product", record.product),
        field("Origin", record.origin),
        field("Stage", record.stage),
        field("Confirmed By", record.confirmedBy),
        field("UID", record.uid || "Legacy record has no UID"),
        field("Role", record.role || "Legacy record has no role"),
        field("Organization", record.organizationId || "Legacy record has no organization")
    );

    const timeFields = document.createElement("dl");
    timeFields.className = "time-fields";
    timeFields.append(
        field("UTC Time", submissionTime.utc),
        field("Control Browser Local Time", submissionTime.local)
    );

    card.append(
        header,
        fields,
        timeFields,
        detail("Merkle Root", record.rootHash),
        detail("Merkle Proof", record.proof)
    );
    return card;
}

async function loadRecords() {
    if (!session) return;
    refreshButton.disabled = true;
    statusLine.textContent = "Loading records...";
    statusLine.className = "status pending";

    try {
        const response = await fetch("/api/records", {
            headers: { Authorization: `Bearer ${session.token}` }
        });
        const records = await response.json();
        if (response.status === 401 || response.status === 403) {
            clearSession();
            throw new Error("Control-panel session expired or insufficient permissions.");
        }
        if (!response.ok) {
            throw new Error(records.error || `Request failed: ${response.status}`);
        }

        list.replaceChildren(...records.map(renderRecord));
        statusLine.textContent = records.length === 0
            ? "No supply-chain records yet."
            : `Loaded ${records.length} record(s).`;
        statusLine.className = "status success";
    } catch (error) {
        list.replaceChildren();
        statusLine.textContent = error.message;
        statusLine.className = "status error";
    } finally {
        refreshButton.disabled = false;
    }
}

refreshButton.addEventListener("click", loadRecords);
loginForm.addEventListener("submit", login);
logoutButton.addEventListener("click", clearSession);
restoreSession();
