#ifndef SUPPLY_CHAIN_BLOCK_MERKLE_HPP
#define SUPPLY_CHAIN_BLOCK_MERKLE_HPP

#include "block_data.hpp"

#include <optional>
#include <string>
#include <vector>

struct BlockMerkleResult
{
    std::string canonical_record;
    std::string root_hash;
    std::vector<MerkleLeafRecord> leaves;
    bool verified = false;
};

std::optional<BlockMerkleResult> build_block_merkle(
    const std::vector<MerkleField>& fields);

std::string canonical_record_from_fields(
    const std::vector<MerkleField>& fields);

std::string sha256_value(const std::string& value);

#endif
