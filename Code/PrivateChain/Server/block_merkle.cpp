#include "block_merkle.hpp"

#include "MerkleTree.hpp"

#include <cstddef>
#include <openssl/sha.h>

namespace
{
constexpr std::size_t HASH_ENTRY_SIZE = 2 + 2 * SHA256_DIGEST_LENGTH;

std::string leaf_input(const MerkleField& field)
{
    return field.name + ":" + std::to_string(field.value.size()) + ":" +
           field.value;
}

bool verify_proof(MerkleTree& tree, const std::string& proof)
{
    const std::string root_hash = tree.GetRootHash();
    if(root_hash.empty() || proof.size() < HASH_ENTRY_SIZE ||
       proof.size() % HASH_ENTRY_SIZE != 0)
        return false;

    const std::string first_direction = proof.substr(0, 2);
    if(first_direction != "L:" && first_direction != "R:") return false;

    std::string current = proof.substr(2, 2 * SHA256_DIGEST_LENGTH);
    std::size_t offset = HASH_ENTRY_SIZE;
    while(offset < proof.size())
    {
        const std::string direction = proof.substr(offset, 2);
        if(direction != "L:" && direction != "R:") return false;

        const std::string sibling = proof.substr(
            offset + 2, 2 * SHA256_DIGEST_LENGTH);
        current = direction == "L:"
            ? tree.SHA256(sibling + current)
            : tree.SHA256(current + sibling);
        offset += HASH_ENTRY_SIZE;
    }

    return current == root_hash;
}
}

std::string canonical_record_from_fields(
    const std::vector<MerkleField>& fields)
{
    std::string canonical;
    for(const MerkleField& field : fields)
    {
        canonical += field.name;
        canonical += ':';
        canonical += std::to_string(field.value.size());
        canonical += ':';
        canonical += field.value;
        canonical += '\n';
    }
    return canonical;
}

std::string sha256_value(const std::string& value)
{
    MerkleTree tree(1);
    return tree.SHA256(value);
}

std::optional<BlockMerkleResult> build_block_merkle(
    const std::vector<MerkleField>& fields)
{
    if(fields.empty()) return std::nullopt;

    MerkleTree tree(1);
    for(const MerkleField& field : fields)
    {
        if(field.name.empty() || !tree.Append(leaf_input(field)))
            return std::nullopt;
    }

    BlockMerkleResult result;
    result.canonical_record = canonical_record_from_fields(fields);
    result.root_hash = tree.GetRootHash();
    result.leaves.reserve(fields.size());

    for(std::size_t index = 0; index < fields.size(); ++index)
    {
        const std::string proof = tree.ProverBlock(static_cast<int>(index));
        result.leaves.push_back(MerkleLeafRecord{
            static_cast<int>(index),
            fields[index].name,
            fields[index].value,
            sha256_value(leaf_input(fields[index])),
            proof,
            verify_proof(tree, proof)
        });
    }

    result.verified = !result.leaves.empty();
    for(const MerkleLeafRecord& leaf : result.leaves)
    {
        if(!leaf.verified)
        {
            result.verified = false;
            break;
        }
    }
    return result;
}
