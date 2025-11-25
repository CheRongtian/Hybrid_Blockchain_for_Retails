// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract SnapshotContract {

    struct Snapshot { // content for a snapshot
        uint256 timestamp;
        bytes32 rootHash;
        string ipfsCid;
        address signature;
    }

    Snapshot[] public snapshots;

    function anchorSnapshot(bytes32 rootHash, string memory ipfsCid) public { // add one snapshot record
        snapshots.push(Snapshot(block.timestamp, rootHash, ipfsCid, msg.sender));
    }
    
    // read all content for one snapshot depending on index
    function getSnapshot(uint256 index) 
        public
        view
        returns (uint256, bytes32, string memory, address)
    {
        require(index < snapshots.length, "Snapshot not found");
        Snapshot memory snap = snapshots[index];
        return (snap.timestamp, snap.rootHash, snap.ipfsCid, snap.signature);
    }

    // read the latest snapshot
    function getLatestSnapshot()
        public
        view
        returns (uint256, bytes32, string memory, address)
    {
        require(snapshots.length > 0, "No snapshots available");
        Snapshot memory snap = snapshots[snapshots.length - 1];
        return (snap.timestamp, snap.rootHash, snap.ipfsCid, snap.signature);
    }
}
