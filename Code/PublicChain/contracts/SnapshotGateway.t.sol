// SPDX-License-Identifier: MIT
pragma solidity ^0.8.24;

import "./SnapshotGateway.sol";

contract SnapshotGatewayActor {
    function publish(
        SnapshotGateway gateway,
        SnapshotGateway.PublishRequest calldata request
    ) external returns (bytes32) {
        return gateway.publishSnapshot(request);
    }

    function recall(
        SnapshotGateway gateway,
        string calldata batchId,
        bytes32 reasonHash
    ) external {
        gateway.recallBatch(batchId, reasonHash);
    }
}

contract SnapshotGatewayTest {
    function testPublishAndQuery() external {
        SnapshotGateway gateway = _gateway();
        SnapshotGateway.PublishRequest memory request = _request(
            "SNAP-BATCH-0001-V0001",
            "BATCH-0001",
            1
        );

        bytes32 snapshotIdHash = gateway.publishSnapshot(request);
        SnapshotGateway.SnapshotRecord memory record = gateway.getSnapshot(
            snapshotIdHash
        );

        require(record.publicRoot == request.publicRoot, "public root");
        require(record.manifestHash == request.manifestHash, "manifest hash");
        require(record.publisher == address(this), "publisher");
        require(
            record.status == SnapshotGateway.SnapshotStatus.Active,
            "status"
        );

        bytes32[] memory history = gateway.getBatchHistory("BATCH-0001");
        require(history.length == 1, "history length");
        require(history[0] == snapshotIdHash, "history value");
    }

    function testUnauthorizedPublisher() external {
        SnapshotGateway gateway = _gateway();
        SnapshotGatewayActor actor = new SnapshotGatewayActor();
        SnapshotGateway.PublishRequest memory request = _request(
            "SNAP-BATCH-0001-V0001",
            "BATCH-0001",
            1
        );

        try actor.publish(gateway, request) {
            revert("unauthorized publish succeeded");
        } catch {}
    }

    function testDuplicateSnapshotAndNonceReplay() external {
        SnapshotGateway gateway = _gateway();
        SnapshotGateway.PublishRequest memory first = _request(
            "SNAP-BATCH-0001-V0001",
            "BATCH-0001",
            1
        );
        gateway.publishSnapshot(first);

        try gateway.publishSnapshot(first) {
            revert("duplicate snapshot succeeded");
        } catch {}

        SnapshotGateway.PublishRequest memory replay = _request(
            "SNAP-BATCH-0002-V0001",
            "BATCH-0002",
            1
        );
        try gateway.publishSnapshot(replay) {
            revert("nonce replay succeeded");
        } catch {}
    }

    function testValidationRules() external {
        SnapshotGateway gateway = _gateway();
        SnapshotGateway.PublishRequest memory request = _request(
            "SNAP-BATCH-0001-V0001",
            "BATCH-0001",
            1
        );

        request.publicRoot = bytes32(0);
        try gateway.publishSnapshot(request) {
            revert("empty root succeeded");
        } catch {}

        request = _request("SNAP-BATCH-0001-V0001", "BATCH-0001", 1);
        request.snapshotVersion = 2;
        try gateway.publishSnapshot(request) {
            revert("unsupported version succeeded");
        } catch {}

        request = _request("SNAP-BATCH-0001-V0001", "BATCH-0001", 1);
        request.destinationChainId = block.chainid + 1;
        try gateway.publishSnapshot(request) {
            revert("wrong destination chain succeeded");
        } catch {}

        request = _request("SNAP-BATCH-0001-V0001", "BATCH-0001", 1);
        request.protocol = "Unsupported-Trace-v1";
        try gateway.publishSnapshot(request) {
            revert("unsupported protocol succeeded");
        } catch {}

        request = _request("SNAP-BATCH-0001-V0001", "BATCH-0001", 1);
        request.sourceNetworkId = keccak256("unknown-private-network");
        try gateway.publishSnapshot(request) {
            revert("unknown source network succeeded");
        } catch {}
    }

    function testReplacementAndHistory() external {
        SnapshotGateway gateway = _gateway();
        bytes32 firstId = gateway.publishSnapshot(
            _request("SNAP-BATCH-0001-A", "BATCH-0001", 1)
        );
        bytes32 secondId = gateway.publishSnapshot(
            _request("SNAP-BATCH-0001-B", "BATCH-0001", 2)
        );

        SnapshotGateway.SnapshotRecord memory first = gateway.getSnapshot(
            firstId
        );
        SnapshotGateway.SnapshotRecord memory second = gateway.getSnapshot(
            secondId
        );
        require(
            first.status == SnapshotGateway.SnapshotStatus.Superseded,
            "old status"
        );
        require(
            second.status == SnapshotGateway.SnapshotStatus.Active,
            "new status"
        );

        bytes32[] memory history = gateway.getBatchHistory("BATCH-0001");
        require(history.length == 2, "replacement history");
        require(history[0] == firstId && history[1] == secondId, "order");
    }

    function testRecallAndRevoke() external {
        SnapshotGateway gateway = _gateway();
        bytes32 firstId = gateway.publishSnapshot(
            _request("SNAP-BATCH-0001-A", "BATCH-0001", 1)
        );
        gateway.recallBatch("BATCH-0001", keccak256("unsafe temperature"));

        SnapshotGateway.SnapshotRecord memory recalled = gateway.getSnapshot(
            firstId
        );
        require(
            recalled.status == SnapshotGateway.SnapshotStatus.Recalled,
            "recall status"
        );

        try gateway.publishSnapshot(
            _request("SNAP-BATCH-0001-B", "BATCH-0001", 2)
        ) {
            revert("recalled batch accepted a snapshot");
        } catch {}

        SnapshotGateway secondGateway = _gateway();
        bytes32 secondId = secondGateway.publishSnapshot(
            _request("SNAP-BATCH-0002-A", "BATCH-0002", 3)
        );
        secondGateway.revokeSnapshot(secondId, keccak256("publisher error"));
        SnapshotGateway.SnapshotRecord memory revoked = secondGateway
            .getSnapshot(secondId);
        require(
            revoked.status == SnapshotGateway.SnapshotStatus.Revoked,
            "revoke status"
        );
    }

    function testRolePermissionsAndPause() external {
        SnapshotGateway gateway = _gateway();
        SnapshotGatewayActor actor = new SnapshotGatewayActor();
        gateway.setPublisher(address(actor), true);
        gateway.setRecallManager(address(actor), true);

        actor.publish(
            gateway,
            _request("SNAP-BATCH-0001-A", "BATCH-0001", 1)
        );
        actor.recall(gateway, "BATCH-0001", keccak256("recall"));

        gateway.setPaused(true);
        try gateway.publishSnapshot(
            _request("SNAP-BATCH-0002-A", "BATCH-0002", 2)
        ) {
            revert("paused publish succeeded");
        } catch {}
    }

    function _request(
        string memory snapshotId,
        string memory batchId,
        uint256 nonce
    ) private view returns (SnapshotGateway.PublishRequest memory) {
        return SnapshotGateway.PublishRequest({
            protocol: "Supermarket-Trace-v1",
            snapshotId: snapshotId,
            batchId: batchId,
            publicRoot: keccak256(abi.encodePacked("public-root", snapshotId)),
            manifestHash: keccak256(abi.encodePacked("manifest", snapshotId)),
            sourceBlockHash: keccak256(abi.encodePacked("source", snapshotId)),
            sourceNetworkId: keccak256("supermarket-private-local-v1"),
            destinationChainId: block.chainid,
            nonce: nonce,
            snapshotVersion: 1
        });
    }

    function _gateway() private returns (SnapshotGateway gateway) {
        gateway = new SnapshotGateway();
        gateway.setSourceNetwork(
            keccak256("supermarket-private-local-v1"),
            true
        );
    }
}
