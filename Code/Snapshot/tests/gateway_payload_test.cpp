#include "gateway_payload.hpp"

#include <cassert>
#include <iostream>

using schnucks::snapshot::GatewayContext;
using schnucks::snapshot::Preview;
using schnucks::snapshot::build_gateway_payload;
using schnucks::snapshot::keccak256_hex;

int main()
{
    assert(keccak256_hex("") ==
           "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    assert(keccak256_hex("abc") ==
           "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    Preview preview;
    preview.protocol = "Schnucks-Trace-v1";
    preview.snapshot_id = "SNAP-BATCH-0001-V0001";
    preview.snapshot_version = 1;
    preview.batch_id = "BATCH-0001";
    preview.manifest_json = "{\"batch\":\"BATCH-0001\"}";
    preview.public_root = std::string(64, 'a');
    preview.final_private_block_hash = std::string(64, 'b');

    GatewayContext context;
    context.source_network = "schnucks-private-local-v1";
    context.destination_chain_id = 31337;
    context.nonce = 1;

    std::string error;
    const auto payload = build_gateway_payload(preview, context, error);
    assert(payload.has_value());
    assert(error.empty());
    assert(payload->public_root == "0x" + std::string(64, 'a'));
    assert(payload->source_block_hash == "0x" + std::string(64, 'b'));
    assert(payload->protocol_hash.size() == 66);
    assert(payload->manifest_hash.size() == 66);

    std::cout << "Snapshot gateway payload tests passed.\n";
    return 0;
}
