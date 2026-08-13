import hardhatEthers from "@nomicfoundation/hardhat-ethers";
import hardhatEthersChaiMatchers from
  "@nomicfoundation/hardhat-ethers-chai-matchers";
import hardhatMocha from "@nomicfoundation/hardhat-mocha";
import { defineConfig } from "hardhat/config";
import "dotenv/config";

const localChainId = Number(process.env.PUBLIC_CHAIN_ID ?? "31337");
const localRpcUrl = process.env.PUBLIC_CHAIN_RPC_URL ??
  "http://127.0.0.1:8545";
const relayerPrivateKey = process.env.RELAYER_PRIVATE_KEY?.trim();

const config = defineConfig({
  plugins: [hardhatEthers, hardhatEthersChaiMatchers, hardhatMocha],
  solidity: {
    profiles: {
      default: {
        version: "0.8.24",
      },
      production: {
        version: "0.8.24",
        settings: {
          optimizer: {
            enabled: true,
            runs: 200,
          },
        },
      },
    },
  },
  networks: {
    hardhatMainnet: {
      type: "edr-simulated",
      chainType: "l1",
      chainId: localChainId,
    },
    localhost: {
      type: "http",
      chainType: "l1",
      chainId: localChainId,
      url: localRpcUrl,
      accounts: relayerPrivateKey ? [relayerPrivateKey] : "remote",
    },
  },
});

export default config;
