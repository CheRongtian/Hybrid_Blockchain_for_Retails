const qrImage = document.querySelector("#qr-image");

function renderCode(code) {
  if (!code) {
    qrImage.hidden = true;
    qrImage.removeAttribute("src");
    return;
  }

  if (code.qrError || !code.qrImageUrl) {
    qrImage.hidden = true;
    qrImage.removeAttribute("src");
    return;
  }

  qrImage.src = code.qrImageUrl;
  qrImage.alt = `QR Code for ${code.batchId}`;
  qrImage.hidden = false;
}

function renderCodes(nextCodes) {
  const currentCode = nextCodes[0];
  renderCode(currentCode);
}

async function loadCodes() {
  try {
    const response = await fetch("/api/qr-codes", { cache: "no-store" });
    const payload = await response.json();
    if (!response.ok) throw new Error(payload.error || `Request failed: ${response.status}`);
    if (!Array.isArray(payload.qrCodes)) throw new Error("QR Code response is malformed.");
    renderCodes(payload.qrCodes);
  } catch (error) {
    renderCode(null);
  }
}

loadCodes();
window.setInterval(loadCodes, 5000);
