# Hybrid-Chain Design Prototype
```css
Project/
│
└── Code/
│   ├── MerkleTree/
│   │   ├── CMakeLists.txt
│   │   ├── Main.cpp
│   │   ├── MerkleTree_Build.cpp
│   │   ├── MerkleTree_Core.cpp
│   │   ├── MerkleTree_Proof.cpp
│   │   ├── MerkleTree_Utils.cpp
│   │   ├── MerkleTree.hpp
│   │   ├── README.md
│   │   └── build/
│   │       ├── MerkleTree
│   │       ├── ......
│   │       └──inp.txt
│   ├── mempool.cpp
│   ├── SNsample.sol /* snapshot demo in Solidity */
│   ├── To be continued ......
│   
└── Architecture/
│   ├── GeneralDesign.drawio
│   ├── OverallStructure.drawio
│   ├── PrivateChainExample.drawio
│   └── HybridChainExample.drawio
└── ROI/
    ├── Precise Mathematical Modeling.md
    └── Case-based Estimation.md
```
## Code
### Merkle Tree


### Memory Pool
#### Why this one?
- Because this one is very important for miners in Blockchain.
- The mempool serves as the blockchain’s staging area for unconfirmed transactions. All transactions broadcast to the network enter the mempool before they are written into a block, giving miners a real-time pool of candidates to choose from. It allows miners to prioritize transactions by fee, ensures the network maintains a consistent view of pending activity, and acts as the system’s transaction buffer and scheduler.

Working on now~~ 💪
## Content