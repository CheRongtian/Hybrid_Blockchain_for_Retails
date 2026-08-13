// SPDX-License-Identifier: MIT
pragma solidity ^0.8.24;

contract SnapshotGateway {
    bytes32 public constant SUPPORTED_PROTOCOL_HASH =
        keccak256("Schnucks-Trace-v1");
    uint32 public constant SUPPORTED_SNAPSHOT_VERSION = 1;

    enum SnapshotStatus {
        None,
        Active,
        Superseded,
        Recalled,
        Revoked
    }

    struct PublishRequest {
        string protocol;
        string snapshotId;
        string batchId;
        bytes32 publicRoot;
        bytes32 manifestHash;
        bytes32 sourceBlockHash;
        bytes32 sourceNetworkId;
        uint256 destinationChainId;
        uint256 nonce;
        uint32 snapshotVersion;
    }

    struct SnapshotRecord {
        bytes32 snapshotId;
        bytes32 batchIdHash;
        bytes32 protocolHash;
        bytes32 publicRoot;
        bytes32 manifestHash;
        bytes32 sourceBlockHash;
        bytes32 sourceNetworkId;
        uint256 destinationChainId;
        uint256 nonce;
        uint32 snapshotVersion;
        uint64 publishedAt;
        address publisher;
        SnapshotStatus status;
    }

    error Unauthorized();
    error ContractPaused();
    error InvalidAddress();
    error InvalidTextField();
    error InvalidHashField();
    error InvalidProtocol();
    error InvalidSnapshotVersion();
    error InvalidDestinationChain();
    error InvalidNonce();
    error UnsupportedSourceNetwork();
    error SnapshotAlreadyExists();
    error NonceAlreadyUsed();
    error SnapshotNotFound();
    error NoActiveSnapshot();
    error BatchIsRecalled();

    address public admin;
    bool public paused;

    mapping(address => bool) public publishers;
    mapping(address => bool) public recallManagers;
    mapping(bytes32 => bool) public allowedSourceNetworks;
    mapping(bytes32 => SnapshotRecord) private snapshots;
    mapping(bytes32 => bytes32) public currentSnapshotByBatch;
    mapping(bytes32 => bytes32[]) private snapshotHistoryByBatch;
    mapping(bytes32 => mapping(uint256 => bool)) public usedNonces;
    mapping(bytes32 => bool) public recalledBatches;

    event AdminTransferred(
        address indexed previousAdmin,
        address indexed newAdmin
    );
    event PublisherPermissionUpdated(
        address indexed account,
        bool enabled
    );
    event RecallManagerPermissionUpdated(
        address indexed account,
        bool enabled
    );
    event SourceNetworkPermissionUpdated(
        bytes32 indexed sourceNetworkId,
        bool enabled
    );
    event PauseUpdated(bool paused);
    event SnapshotPublished(
        bytes32 indexed snapshotIdHash,
        bytes32 indexed batchIdHash,
        bytes32 indexed sourceNetworkId,
        string snapshotId,
        string batchId,
        bytes32 publicRoot,
        bytes32 manifestHash,
        bytes32 sourceBlockHash,
        uint256 nonce,
        address publisher
    );
    event SnapshotSuperseded(
        bytes32 indexed batchIdHash,
        bytes32 indexed previousSnapshotIdHash,
        bytes32 indexed newSnapshotIdHash
    );
    event BatchRecalled(
        bytes32 indexed batchIdHash,
        bytes32 indexed snapshotIdHash,
        bytes32 reasonHash,
        address indexed recalledBy
    );
    event SnapshotRevoked(
        bytes32 indexed snapshotIdHash,
        bytes32 indexed batchIdHash,
        bytes32 reasonHash,
        address indexed revokedBy
    );

    modifier onlyAdmin() {
        if (msg.sender != admin) revert Unauthorized();
        _;
    }

    modifier onlyPublisher() {
        if (!publishers[msg.sender]) revert Unauthorized();
        _;
    }

    modifier onlyRecallManager() {
        if (!recallManagers[msg.sender] && msg.sender != admin) {
            revert Unauthorized();
        }
        _;
    }

    modifier whenNotPaused() {
        if (paused) revert ContractPaused();
        _;
    }

    constructor() {
        admin = msg.sender;
        publishers[msg.sender] = true;
        recallManagers[msg.sender] = true;

        emit AdminTransferred(address(0), msg.sender);
        emit PublisherPermissionUpdated(msg.sender, true);
        emit RecallManagerPermissionUpdated(msg.sender, true);
    }

    function setPublisher(address account, bool enabled) external onlyAdmin {
        if (account == address(0)) revert InvalidAddress();
        publishers[account] = enabled;
        emit PublisherPermissionUpdated(account, enabled);
    }

    function setRecallManager(
        address account,
        bool enabled
    ) external onlyAdmin {
        if (account == address(0)) revert InvalidAddress();
        recallManagers[account] = enabled;
        emit RecallManagerPermissionUpdated(account, enabled);
    }

    function setPaused(bool value) external onlyAdmin {
        paused = value;
        emit PauseUpdated(value);
    }

    function setSourceNetwork(
        bytes32 sourceNetworkId,
        bool enabled
    ) external onlyAdmin {
        if (sourceNetworkId == bytes32(0)) revert InvalidHashField();
        allowedSourceNetworks[sourceNetworkId] = enabled;
        emit SourceNetworkPermissionUpdated(sourceNetworkId, enabled);
    }

    function transferAdmin(address newAdmin) external onlyAdmin {
        if (newAdmin == address(0)) revert InvalidAddress();
        address previousAdmin = admin;
        admin = newAdmin;
        emit AdminTransferred(previousAdmin, newAdmin);
    }

    function publishSnapshot(
        PublishRequest calldata request
    ) external onlyPublisher whenNotPaused returns (bytes32 snapshotIdHash) {
        _validateRequest(request);

        bytes32 protocolHash = keccak256(bytes(request.protocol));
        snapshotIdHash = keccak256(bytes(request.snapshotId));
        bytes32 batchIdHash = keccak256(bytes(request.batchId));

        if (snapshots[snapshotIdHash].status != SnapshotStatus.None) {
            revert SnapshotAlreadyExists();
        }
        if (usedNonces[request.sourceNetworkId][request.nonce]) {
            revert NonceAlreadyUsed();
        }
        if (recalledBatches[batchIdHash]) revert BatchIsRecalled();

        bytes32 previousSnapshotIdHash = currentSnapshotByBatch[batchIdHash];
        if (previousSnapshotIdHash != bytes32(0)) {
            SnapshotRecord storage previous = snapshots[previousSnapshotIdHash];
            if (previous.status == SnapshotStatus.Active) {
                previous.status = SnapshotStatus.Superseded;
                emit SnapshotSuperseded(
                    batchIdHash,
                    previousSnapshotIdHash,
                    snapshotIdHash
                );
            }
        }

        snapshots[snapshotIdHash] = SnapshotRecord({
            snapshotId: snapshotIdHash,
            batchIdHash: batchIdHash,
            protocolHash: protocolHash,
            publicRoot: request.publicRoot,
            manifestHash: request.manifestHash,
            sourceBlockHash: request.sourceBlockHash,
            sourceNetworkId: request.sourceNetworkId,
            destinationChainId: request.destinationChainId,
            nonce: request.nonce,
            snapshotVersion: request.snapshotVersion,
            publishedAt: uint64(block.timestamp),
            publisher: msg.sender,
            status: SnapshotStatus.Active
        });

        usedNonces[request.sourceNetworkId][request.nonce] = true;
        currentSnapshotByBatch[batchIdHash] = snapshotIdHash;
        snapshotHistoryByBatch[batchIdHash].push(snapshotIdHash);

        emit SnapshotPublished(
            snapshotIdHash,
            batchIdHash,
            request.sourceNetworkId,
            request.snapshotId,
            request.batchId,
            request.publicRoot,
            request.manifestHash,
            request.sourceBlockHash,
            request.nonce,
            msg.sender
        );
    }

    function recallBatch(
        string calldata batchId,
        bytes32 reasonHash
    ) external onlyRecallManager whenNotPaused {
        if (bytes(batchId).length == 0) revert InvalidTextField();
        if (reasonHash == bytes32(0)) revert InvalidHashField();

        bytes32 batchIdHash = keccak256(bytes(batchId));
        bytes32 snapshotIdHash = currentSnapshotByBatch[batchIdHash];
        if (snapshotIdHash == bytes32(0)) revert NoActiveSnapshot();

        SnapshotRecord storage record = snapshots[snapshotIdHash];
        if (record.status != SnapshotStatus.Active) revert NoActiveSnapshot();

        record.status = SnapshotStatus.Recalled;
        recalledBatches[batchIdHash] = true;
        emit BatchRecalled(batchIdHash, snapshotIdHash, reasonHash, msg.sender);
    }

    function revokeSnapshot(
        bytes32 snapshotIdHash,
        bytes32 reasonHash
    ) external onlyAdmin {
        if (reasonHash == bytes32(0)) revert InvalidHashField();

        SnapshotRecord storage record = snapshots[snapshotIdHash];
        if (record.status == SnapshotStatus.None) revert SnapshotNotFound();
        record.status = SnapshotStatus.Revoked;

        if (currentSnapshotByBatch[record.batchIdHash] == snapshotIdHash) {
            currentSnapshotByBatch[record.batchIdHash] = bytes32(0);
        }
        emit SnapshotRevoked(
            snapshotIdHash,
            record.batchIdHash,
            reasonHash,
            msg.sender
        );
    }

    function getSnapshot(
        bytes32 snapshotIdHash
    ) external view returns (SnapshotRecord memory) {
        SnapshotRecord memory record = snapshots[snapshotIdHash];
        if (record.status == SnapshotStatus.None) revert SnapshotNotFound();
        return record;
    }

    function getCurrentSnapshot(
        string calldata batchId
    ) external view returns (SnapshotRecord memory) {
        bytes32 batchIdHash = keccak256(bytes(batchId));
        bytes32 snapshotIdHash = currentSnapshotByBatch[batchIdHash];
        if (snapshotIdHash == bytes32(0)) revert NoActiveSnapshot();
        return snapshots[snapshotIdHash];
    }

    function getBatchHistory(
        string calldata batchId
    ) external view returns (bytes32[] memory) {
        return snapshotHistoryByBatch[keccak256(bytes(batchId))];
    }

    function _validateRequest(PublishRequest calldata request) private view {
        if (
            bytes(request.protocol).length == 0 ||
            bytes(request.snapshotId).length == 0 ||
            bytes(request.batchId).length == 0
        ) revert InvalidTextField();
        if (
            request.publicRoot == bytes32(0) ||
            request.manifestHash == bytes32(0) ||
            request.sourceBlockHash == bytes32(0) ||
            request.sourceNetworkId == bytes32(0)
        ) revert InvalidHashField();
        if (keccak256(bytes(request.protocol)) != SUPPORTED_PROTOCOL_HASH) {
            revert InvalidProtocol();
        }
        if (request.snapshotVersion != SUPPORTED_SNAPSHOT_VERSION) {
            revert InvalidSnapshotVersion();
        }
        if (request.destinationChainId != block.chainid) {
            revert InvalidDestinationChain();
        }
        if (!allowedSourceNetworks[request.sourceNetworkId]) {
            revert UnsupportedSourceNetwork();
        }
        if (request.nonce == 0) revert InvalidNonce();
    }
}
