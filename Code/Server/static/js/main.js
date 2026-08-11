const form = document.querySelector("#record-form");
const statusLine = document.querySelector("#request-status");
const resultCard = document.querySelector("#result-card");
const verificationTitle = document.querySelector("#verification-title");

form.addEventListener("submit", async (event) => {
    event.preventDefault();
    statusLine.textContent = "正在生成并验证 Merkle Proof…";
    statusLine.className = "request-status pending";
    resultCard.hidden = true;

    const body = new URLSearchParams(new FormData(form));
    body.set("confirmed", document.querySelector("#confirmed").checked ? "true" : "false");

    try {
        const response = await fetch("/api/records", {
            method: "POST",
            headers: {
                "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8"
            },
            body: body.toString()
        });

        const result = await response.json();
        if (!response.ok) {
            throw new Error(result.error || `Request failed: ${response.status}`);
        }

        verificationTitle.textContent = result.verified ? "Verified ✓" : "Verification failed";
        verificationTitle.className = result.verified ? "verified" : "failed";
        document.querySelector("#block-id").textContent = result.blockID;
        document.querySelector("#root-hash").textContent = result.rootHash;
        document.querySelector("#proof").textContent = result.proof;
        resultCard.hidden = false;

        statusLine.textContent = result.verified
            ? "记录已经加入 Merkle Tree，Proof 验证通过。"
            : "记录已加入，但 Proof 验证失败。";
        statusLine.className = result.verified
            ? "request-status success"
            : "request-status error";
    } catch (error) {
        statusLine.textContent = error.message;
        statusLine.className = "request-status error";
    }
});
