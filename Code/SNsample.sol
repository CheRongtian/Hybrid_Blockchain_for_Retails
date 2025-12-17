// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

import "@openzeppelin/contracts/access/Ownable.col"; // OpenZeppelin package, Ownable contract

/* 
@title Hybrid Supply Chain Snapshot Anchor
@dev Implements a secure anchor for **Hybrid Supply Chain States**.

CORE FUNCTION:
Cryptographically binds the on-chain "Verifiable Proof" (Merkle Root) 
with the off-chain "Data Availability" (IPFS CIDs) and Business Context (Batch Range).
 */

// SnapShotContract inherit ability of Ownable
contract SnapShotContract is Ownable {

    // event: cheap for storing history snapshot
    // emit history records, external audit, web ... can read them
    event SnapShotAnchored(
        uint256 indexed snapshotID,
        bytes32 rootHash,
        string IPFSCID,
        string batchRange,
        uint256 timestamp,
        address operator
    );

    struct Snapshot {
        uint256 timestamp;
        bytes32 rootHash;
        string IPFSCID;
        string batchRange;
        address operato;
    }

    // statue var: permanent date in hard disk of blockchain
    Snapshot public latestSnapshot; // only store the latest one
    uint256 public snapshotCount; // count for # of records

    /*
     * @dev Constructor
     * Initializes the contract owner to the deployer.
     * Establishes the initial root of trust.
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
        bytes32 rootHash,
        string memory IPFSCID,
        string memory batchRange
    ) public onlyOwner {
        // the lock
        snapshotCount++;

        latestSnapshot = Snapshot(
            // overwrite the old one
            block.timestamp,
            rootHash,
            IPFSCID,
            batchRange, 
            msg.sender
        );

        // emit immutable audit trail
        emit SnapShotAnchored(
            snapshotCount,
            rootHash,
            IPFSCID,
            batchRange,
            block.timestamp,
            msg.sender
        );

        // read
        // Retrieves the current verifiable state root
        function getLastestSnapshot()
            public
            view
            return (Snapshot memory)
        {
            return latestSnapshot;
        }
    }
}