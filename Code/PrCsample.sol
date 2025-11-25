// SPDX-License-Identifier: MIT
pragma solidity ^0.8.0;

contract PrivateChain {
    struct EventRecord {
        uint256 timestamp; //timestamp
        string batchID; // batch ID
        bytes32 fileHash; // small file Hash
        bytes32 dataHash; // data hash
        string ipfsCid; // CID for IPFS
        address uploader; // uploader in which node
    }
    
    EventRecord[] public records;

    function addRecord(string memory batchID, bytes32 fileHash, bytes32 dataHash, string memory ipfsCid) public{
        records.push(EventRecord(block.timestamp, batchID, fileHash, dataHash, ipfsCid, msg.sender));
    }

    function getRecord(uint256 index)
        public
        view
        returns (uint256, string memory, bytes32, bytes32, string memory, address)
    {
        require(index < records.length, "Record not found");
        EventRecord memory rec = records[index];
        return (rec.timestamp, rec.batchID, rec.fileHash, rec.dataHash, rec.ipfsCid, rec.uploader);
    }

    function getLatestRecord()
        public
        view
        returns (uint256, string memory, bytes32, bytes32, string memory, address)
    {
        require(records.length > 0, "No records available");
        EventRecord memory rec = records[records.length - 1];
        return (rec.timestamp, rec.batchID, rec.fileHash, rec.dataHash, rec.ipfsCid, rec.uploader);
    }
}