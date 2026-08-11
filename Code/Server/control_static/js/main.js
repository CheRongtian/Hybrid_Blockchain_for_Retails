const list = document.querySelector("#record-list");
const statusLine = document.querySelector("#load-status");
const refreshButton = document.querySelector("#refresh-button");

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
            local: "无法转换当地时间"
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
        field("批次号", record.batchId),
        field("产品", record.product),
        field("产地", record.origin),
        field("阶段", record.stage),
        field("确认人", record.confirmedBy)
    );

    const timeFields = document.createElement("dl");
    timeFields.className = "time-fields";
    timeFields.append(
        field("UTC 时间", submissionTime.utc),
        field("监控端当地时间", submissionTime.local)
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
    refreshButton.disabled = true;
    statusLine.textContent = "正在读取记录…";
    statusLine.className = "status pending";

    try {
        const response = await fetch("/api/records");
        const records = await response.json();
        if (!response.ok) {
            throw new Error(records.error || `Request failed: ${response.status}`);
        }

        list.replaceChildren(...records.map(renderRecord));
        statusLine.textContent = records.length === 0
            ? "当前还没有供应链记录。"
            : `共读取 ${records.length} 条记录。`;
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
loadRecords();
