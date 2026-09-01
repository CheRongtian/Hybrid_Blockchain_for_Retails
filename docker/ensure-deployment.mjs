import fs from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { JsonRpcProvider, isAddress } from "ethers";

const rpcUrl = process.env.PUBLIC_CHAIN_RPC_URL ?? "http://hardhat:8545";
const provider = new JsonRpcProvider(rpcUrl);
const network = await provider.getNetwork();
const chainId = network.chainId.toString();
const deploymentPath = path.join(process.cwd(), "deployments", `${chainId}.json`);

if (fs.existsSync(deploymentPath)) {
  try {
    const deployment = JSON.parse(fs.readFileSync(deploymentPath, "utf8"));
    if (isAddress(deployment.contractAddress)) {
      const code = await provider.getCode(deployment.contractAddress);
      if (code !== "0x") {
        console.log(`Using existing SnapshotGateway: ${deployment.contractAddress}`);
        process.exit(0);
      }
    }
  } catch (error) {
    console.warn(`Existing deployment cannot be reused: ${error.message}`);
  }
}

const deployment = spawnSync("npm", ["run", "deploy:local"], {
  cwd: process.cwd(),
  env: process.env,
  stdio: "inherit",
});

if (deployment.error) throw deployment.error;
process.exit(deployment.status ?? 1);
