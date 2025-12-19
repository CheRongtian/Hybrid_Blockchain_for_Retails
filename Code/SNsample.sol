// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/access/Ownable.sol"; // OpenZeppelin package, Ownable contract

/* 
@title Hybrid Supply Chain Snapshot Anchor
@dev Implements a secure anchor for Hybrid Supply Chain States.

CORE FUNCTION:
Cryptographically binds the on-chain "Verifiable Proof" (Merkle Root) 
with the small files, data, off-chain "Data Availability" (IPFS CIDs) and Business Context (Batch Range).
 */

// SnapShotContract inherit ability of Ownable
contract SnapShotContract is Ownable {
    
    // define the struct of snapshot
    struct Snapshot {
        bytes32 rootHash; // Merkle Root
        string batchID;       
        string smallFilesDataInfo; // save gas fees for storing small files & data 
        string[] SelectedCIDs; // selected CIDs (big files in IPFS)
        uint256 timestamp;
    }

    mapping(string => Snapshot) public snapshots; // batch ID maps snapshot
    uint256 public totalSnapshots;

    // event: cheap for storing history snapshot
    event SnapShotAnchored(
        string indexed batchID,
        address indexed operator,
        bytes32 rootHash,
        string smallFilesDataInfo,
        string[] SelectedCIDs,
        uint256 timestamp
    );

    /*
    @dev Constructor
    Initializes the contract owner to the deployer.
    Establishes the initial root of trust.
    */
    constructor() Ownable(msg.sender) {}

    /*
    @dev anchorSnapshot
    anchors a new state root to the blockchain.
    
    Security Mechanism:
    - Protected by 'onlyOwner' modifier.
    - Acts as a "Source Authentication" gatekeeper. 
    - Ensures only data verified by the off-chain C++ engine (high Trust Coefficient) 
    can enter the ledger, preventing trust dilution from unauthorized noise.
    
    Risk Management:
    - 'batchRange' parameter allows regulators to instantly verify the scope 
    of a recall event, minimizing the Reputational Multiplier (Phi).
    */
    function anchorSnapshot(
        bytes32 _rootHash,
        string memory _batchID,
        string memory _smallFilesDataInfo,
        string[] memory _SelectedCIDs
    ) public onlyOwner {
        // store state: for inquiring the latest version
        snapshots[_batchID] = Snapshot(
            _rootHash,
            _batchID,
            _smallFilesDataInfo,
            _SelectedCIDs,
            block.timestamp
        );

        totalSnapshots++;

        // emit immutable audit trail
        emit SnapShotAnchored(
            _batchID,
            msg.sender,
            _rootHash,
            _smallFilesDataInfo,
            _SelectedCIDs,
            block.timestamp
        );
    }

    // read
    // Retrieves the current verifiable state root
    function getLastestSnapshot(string memory _batchID)
        public
        view
        return (Snapshot memory)
    {
        return snapshots[_batchID];
    }
}