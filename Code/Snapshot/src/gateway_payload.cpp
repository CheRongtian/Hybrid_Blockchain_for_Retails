#include "gateway_payload.hpp"

#include <array>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace schnucks::snapshot
{
namespace
{
constexpr std::size_t KECCAK_RATE = 136;

constexpr std::array<std::uint64_t, 24> ROUND_CONSTANTS = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

constexpr std::array<unsigned int, 24> ROTATION_OFFSETS = {
    1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
    27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
};

constexpr std::array<unsigned int, 24> PERMUTATION_INDEXES = {
    10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
    15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
};

std::uint64_t rotate_left(std::uint64_t value, unsigned int offset)
{
    return (value << offset) | (value >> (64U - offset));
}

std::uint64_t load_little_endian(const unsigned char* bytes)
{
    std::uint64_t value = 0;
    for(unsigned int index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(bytes[index]) << (index * 8U);
    return value;
}

void keccak_permutation(std::array<std::uint64_t, 25>& state)
{
    for(const std::uint64_t round_constant : ROUND_CONSTANTS)
    {
        std::array<std::uint64_t, 5> column{};
        for(unsigned int index = 0; index < 5; ++index)
        {
            column[index] = state[index] ^ state[index + 5] ^
                            state[index + 10] ^ state[index + 15] ^
                            state[index + 20];
        }

        for(unsigned int index = 0; index < 5; ++index)
        {
            const std::uint64_t delta = column[(index + 4) % 5] ^
                                        rotate_left(column[(index + 1) % 5], 1);
            for(unsigned int row = 0; row < 25; row += 5)
                state[row + index] ^= delta;
        }

        std::uint64_t current = state[1];
        for(unsigned int index = 0; index < 24; ++index)
        {
            const unsigned int destination = PERMUTATION_INDEXES[index];
            const std::uint64_t saved = state[destination];
            state[destination] = rotate_left(current, ROTATION_OFFSETS[index]);
            current = saved;
        }

        for(unsigned int row = 0; row < 25; row += 5)
        {
            std::array<std::uint64_t, 5> values{};
            for(unsigned int index = 0; index < 5; ++index)
                values[index] = state[row + index];
            for(unsigned int index = 0; index < 5; ++index)
            {
                state[row + index] = values[index] ^
                    ((~values[(index + 1) % 5]) & values[(index + 2) % 5]);
            }
        }

        state[0] ^= round_constant;
    }
}

std::string bytes_to_hex(const unsigned char* bytes, std::size_t size)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for(std::size_t index = 0; index < size; ++index)
        result << std::setw(2) << static_cast<unsigned int>(bytes[index]);
    return result.str();
}

std::optional<std::string> normalize_bytes32(const std::string& source)
{
    std::string value = source;
    if(value.size() == 66 && value[0] == '0' &&
       (value[1] == 'x' || value[1] == 'X'))
        value.erase(0, 2);

    if(value.size() != 64) return std::nullopt;
    for(char& character : value)
    {
        const unsigned char byte = static_cast<unsigned char>(character);
        if(!std::isxdigit(byte)) return std::nullopt;
        character = static_cast<char>(std::tolower(byte));
    }
    return "0x" + value;
}

std::string json_escape(const std::string& value)
{
    std::ostringstream escaped;
    for(const unsigned char character : value)
    {
        switch(character)
        {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if(character < 0x20)
                {
                    escaped << "\\u00" << std::hex << std::setw(2)
                            << std::setfill('0')
                            << static_cast<unsigned int>(character)
                            << std::dec << std::setfill(' ');
                }
                else
                {
                    escaped << static_cast<char>(character);
                }
        }
    }
    return escaped.str();
}

std::string json_string(const std::string& value)
{
    return "\"" + json_escape(value) + "\"";
}
}

std::string keccak256_hex(const std::string& value)
{
    std::array<std::uint64_t, 25> state{};
    const auto* bytes = reinterpret_cast<const unsigned char*>(value.data());
    std::size_t remaining = value.size();

    while(remaining >= KECCAK_RATE)
    {
        for(std::size_t lane = 0; lane < KECCAK_RATE / 8; ++lane)
            state[lane] ^= load_little_endian(bytes + lane * 8);
        keccak_permutation(state);
        bytes += KECCAK_RATE;
        remaining -= KECCAK_RATE;
    }

    std::array<unsigned char, KECCAK_RATE> final_block{};
    if(remaining > 0) std::memcpy(final_block.data(), bytes, remaining);
    final_block[remaining] = 0x01;
    final_block[KECCAK_RATE - 1] |= 0x80;
    for(std::size_t lane = 0; lane < KECCAK_RATE / 8; ++lane)
        state[lane] ^= load_little_endian(final_block.data() + lane * 8);
    keccak_permutation(state);

    std::array<unsigned char, 32> digest{};
    for(std::size_t index = 0; index < digest.size(); ++index)
    {
        digest[index] = static_cast<unsigned char>(
            (state[index / 8] >> ((index % 8) * 8U)) & 0xffU);
    }
    return bytes_to_hex(digest.data(), digest.size());
}

std::optional<GatewayPayload> build_gateway_payload(
    const Preview& preview,
    const GatewayContext& context,
    std::string& error)
{
    error.clear();
    if(preview.protocol.empty() || preview.snapshot_id.empty() ||
       preview.batch_id.empty() || preview.manifest_json.empty())
    {
        error = "The snapshot preview is incomplete";
        return std::nullopt;
    }
    if(preview.snapshot_version <= 0)
    {
        error = "The snapshot version must be positive";
        return std::nullopt;
    }
    if(context.source_network.empty())
    {
        error = "The source network is missing";
        return std::nullopt;
    }
    if(context.destination_chain_id == 0 || context.nonce == 0)
    {
        error = "The destination chain ID and nonce must be positive";
        return std::nullopt;
    }

    const auto public_root = normalize_bytes32(preview.public_root);
    const auto source_block_hash = normalize_bytes32(
        preview.final_private_block_hash);
    if(!public_root || !source_block_hash)
    {
        error = "The Public Root and source block hash must be bytes32 hex values";
        return std::nullopt;
    }

    GatewayPayload payload;
    payload.protocol = preview.protocol;
    payload.snapshot_id = preview.snapshot_id;
    payload.batch_id = preview.batch_id;
    payload.protocol_hash = "0x" + keccak256_hex(preview.protocol);
    payload.snapshot_id_hash = "0x" + keccak256_hex(preview.snapshot_id);
    payload.batch_id_hash = "0x" + keccak256_hex(preview.batch_id);
    payload.public_root = *public_root;
    payload.manifest_hash = "0x" + keccak256_hex(preview.manifest_json);
    payload.source_block_hash = *source_block_hash;
    payload.source_network_id = "0x" + keccak256_hex(context.source_network);
    payload.destination_chain_id = context.destination_chain_id;
    payload.nonce = context.nonce;
    payload.snapshot_version = static_cast<std::uint32_t>(
        preview.snapshot_version);
    return payload;
}

std::string gateway_payload_json(const GatewayPayload& payload)
{
    std::ostringstream json;
    json << "{\n"
         << "  \"protocol\": " << json_string(payload.protocol) << ",\n"
         << "  \"snapshotId\": " << json_string(payload.snapshot_id) << ",\n"
         << "  \"batchId\": " << json_string(payload.batch_id) << ",\n"
         << "  \"protocolHash\": " << json_string(payload.protocol_hash) << ",\n"
         << "  \"snapshotIdHash\": "
         << json_string(payload.snapshot_id_hash) << ",\n"
         << "  \"batchIdHash\": " << json_string(payload.batch_id_hash) << ",\n"
         << "  \"publicRoot\": " << json_string(payload.public_root) << ",\n"
         << "  \"manifestHash\": " << json_string(payload.manifest_hash) << ",\n"
         << "  \"sourceBlockHash\": "
         << json_string(payload.source_block_hash) << ",\n"
         << "  \"sourceNetworkId\": "
         << json_string(payload.source_network_id) << ",\n"
         << "  \"destinationChainId\": "
         << payload.destination_chain_id << ",\n"
         << "  \"nonce\": " << payload.nonce << ",\n"
         << "  \"snapshotVersion\": " << payload.snapshot_version << "\n"
         << '}';
    return json.str();
}
}
