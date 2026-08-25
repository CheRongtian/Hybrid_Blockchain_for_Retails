import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
export const projectDirectory = path.resolve(scriptDirectory, "..");
export const repositoryDirectory = path.resolve(projectDirectory, "..", "..");

const configuredStorageDirectory = process.env.SUPPLY_CHAIN_STORAGE_ROOT?.trim();
export const storageDirectory = configuredStorageDirectory
  ? path.resolve(configuredStorageDirectory)
  : path.join(repositoryDirectory, "Storage");
export const publicManifestDirectory = path.join(storageDirectory, "PublicManifests");
export const publicQrDirectory = path.join(storageDirectory, "QRCodes");

export function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, "utf8"));
}

export function deploymentPath(chainId) {
  return path.join(projectDirectory, "deployments", `${chainId}.json`);
}

export function loadDeployment(chainId) {
  const filePath = deploymentPath(chainId);
  if (!fs.existsSync(filePath)) {
    throw new Error(
      `Deployment file not found: ${filePath}. Run the local deploy script first.`,
    );
  }
  return readJson(filePath);
}

export function writeDeployment(chainId, deployment) {
  const directory = path.join(projectDirectory, "deployments");
  fs.mkdirSync(directory, { recursive: true });
  fs.writeFileSync(
    deploymentPath(chainId),
    `${JSON.stringify(deployment, null, 2)}\n`,
    "utf8",
  );
}

export function resolvePayloadPath() {
  const configured = process.env.SNAPSHOT_PAYLOAD_PATH ??
    "../Snapshot/examples/gateway_payload.example.json";
  return path.isAbsolute(configured)
    ? configured
    : path.resolve(projectDirectory, configured);
}

export function statusName(status) {
  const names = ["None", "Active", "Superseded", "Recalled", "Revoked"];
  return names[Number(status)] ?? `Unknown(${status})`;
}
