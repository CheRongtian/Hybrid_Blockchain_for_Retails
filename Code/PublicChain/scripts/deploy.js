import { network } from "hardhat";
import { writeDeployment } from "./runtime.js";

const { ethers, networkName } = await network.connect();
const [deployer] = await ethers.getSigners();
const networkInfo = await ethers.provider.getNetwork();
const chainId = networkInfo.chainId.toString();

const sourceNetworkName = process.env.SOURCE_NETWORK_NAME ??
  "supermarket-private-local-v1";
const sourceNetworkId = ethers.keccak256(
  ethers.toUtf8Bytes(sourceNetworkName),
);

console.log(`Deploying SnapshotGateway to ${networkName} (${chainId})...`);
console.log(`Deployer: ${deployer.address}`);

const gateway = await ethers.deployContract("SnapshotGateway");
await gateway.waitForDeployment();
const deploymentTransaction = gateway.deploymentTransaction();
const deploymentReceipt = await deploymentTransaction.wait();
const contractAddress = await gateway.getAddress();

const sourceTransaction = await gateway.setSourceNetwork(sourceNetworkId, true);
await sourceTransaction.wait();

const deployment = {
  contract: "SnapshotGateway",
  contractAddress,
  chainId,
  network: networkName,
  deployer: deployer.address,
  relayer: deployer.address,
  sourceNetworkName,
  sourceNetworkId,
  deploymentTransactionHash: deploymentTransaction.hash,
  deploymentBlockNumber: deploymentReceipt.blockNumber,
};

writeDeployment(chainId, deployment);

console.log(`Contract address: ${contractAddress}`);
console.log(`Source network: ${sourceNetworkName} (${sourceNetworkId})`);
console.log(`Deployment transaction: ${deploymentTransaction.hash}`);
console.log(`Deployment block: ${deploymentReceipt.blockNumber}`);
console.log(`Saved deployment: deployments/${chainId}.json`);
