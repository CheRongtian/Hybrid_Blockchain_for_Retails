#ifndef SUPERMARKET_SNAPSHOT_GATEWAY_PAYLOAD_HPP
#define SUPERMARKET_SNAPSHOT_GATEWAY_PAYLOAD_HPP

#include "snapshot.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace supermarket::snapshot
{
struct GatewayContext
{
    std::string source_network;
    std::uint64_t destination_chain_id = 0;
    std::uint64_t nonce = 0;
};

struct GatewayPayload
{
    std::string protocol;
    std::string snapshot_id;
    std::string batch_id;
    std::string protocol_hash;
    std::string snapshot_id_hash;
    std::string batch_id_hash;
    std::string public_root;
    std::string manifest_hash;
    std::string source_block_hash;
    std::string source_network_id;
    std::uint64_t destination_chain_id = 0;
    std::uint64_t nonce = 0;
    std::uint32_t snapshot_version = 0;
};

std::string keccak256_hex(const std::string& value);

std::optional<GatewayPayload> build_gateway_payload(
    const Preview& preview,
    const GatewayContext& context,
    std::string& error);

std::string gateway_payload_json(const GatewayPayload& payload);
}

#endif
