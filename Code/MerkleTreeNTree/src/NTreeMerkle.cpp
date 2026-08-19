#include "NTreeMerkle.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace merkle_ntree {

namespace {

using EvpContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

std::string join_hashes(const std::vector<std::size_t>& child_ids,
                        const std::vector<Node>& nodes)
{
    std::string material;
    for(const std::size_t child_id : child_ids)
        material += nodes.at(child_id).hash;
    return material;
}

} // namespace

NTreeMerkle::NTreeMerkle(const std::size_t arity) : arity_(arity)
{
    if(arity_ < 2)
        throw std::invalid_argument("The branching factor N must be at least 2.");
}

std::string NTreeMerkle::sha256_hex(const std::string& value)
{
    EvpContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if(!context)
        throw std::runtime_error("Unable to allocate the SHA-256 context.");

    if(EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1 ||
       EVP_DigestUpdate(context.get(), value.data(), value.size()) != 1)
    {
        throw std::runtime_error("Unable to initialize the SHA-256 digest.");
    }

    unsigned char digest[EVP_MAX_MD_SIZE]{};
    unsigned int digest_size = 0;
    if(EVP_DigestFinal_ex(context.get(), digest, &digest_size) != 1)
        throw std::runtime_error("Unable to finalize the SHA-256 digest.");

    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for(unsigned int index = 0; index < digest_size; ++index)
        result << std::setw(2) << static_cast<unsigned int>(digest[index]);
    return result.str();
}

void NTreeMerkle::emit(const std::string& type,
                       const std::string& message,
                       const std::optional<std::size_t>& node_id,
                       const std::size_t level,
                       const std::string& node_hash,
                       EventCallback& callback) const
{
    if(!callback) return;

    BuildEvent event;
    event.type = type;
    event.message = message;
    event.arity = arity_;
    event.level = level;
    event.node_id = node_id;
    event.node_hash = node_hash;
    event.root_hash = root_hash();
    event.nodes = nodes_;
    callback(event);
}

BuildResult NTreeMerkle::build(const std::vector<std::string>& leaves,
                               EventCallback callback)
{
    nodes_.clear();
    leaf_ids_.clear();
    root_id_.reset();
    parent_links_.clear();

    emit("build_started", "Starting N-ary Merkle Tree construction.",
         std::nullopt, 0, "", callback);

    if(leaves.empty())
    {
        const std::string error = "At least one non-empty leaf is required.";
        emit("build_failed", error, std::nullopt, 0, "", callback);
        return BuildResult{false, error, "", arity_, 0, 0, 0};
    }

    for(std::size_t index = 0; index < leaves.size(); ++index)
    {
        Node leaf;
        leaf.id = nodes_.size();
        leaf.level = 0;
        leaf.index = index;
        leaf.leaf = true;
        leaf.value = leaves[index];
        leaf.hash = sha256_hex(leaf.value);
        nodes_.push_back(leaf);
        leaf_ids_.push_back(leaf.id);

        emit("leaf_created", "Created leaf " + std::to_string(index) + ".",
             leaf.id, 0, leaf.hash, callback);
    }

    std::vector<std::size_t> current = leaf_ids_;
    std::size_t level = 0;

    while(current.size() > 1)
    {
        std::vector<std::size_t> next;
        std::size_t padding_count = 0;

        for(std::size_t offset = 0; offset < current.size(); offset += arity_)
        {
            std::vector<std::size_t> group;
            const std::size_t end = std::min(offset + arity_, current.size());
            for(std::size_t child_index = offset; child_index < end; ++child_index)
                group.push_back(current[child_index]);

            while(group.size() < arity_)
            {
                const std::size_t source_id = group.back();
                Node padding;
                padding.id = nodes_.size();
                padding.level = level;
                padding.index = current.size() + padding_count;
                padding.padding = true;
                padding.duplicate_of = source_id;
                padding.hash = nodes_.at(source_id).hash;
                nodes_.push_back(padding);
                group.push_back(padding.id);
                ++padding_count;

                emit("padding_added",
                     "Duplicated the final child to complete an N-child group.",
                     padding.id, level, padding.hash, callback);
            }

            Node parent;
            parent.id = nodes_.size();
            parent.level = level + 1;
            parent.index = next.size();
            parent.children = group;
            parent.hash = sha256_hex(join_hashes(group, nodes_));
            nodes_.push_back(parent);
            next.push_back(parent.id);

            for(std::size_t child_index = 0; child_index < group.size(); ++child_index)
            {
                parent_links_[group[child_index]] =
                    ParentLink{parent.id, child_index};
            }

            emit("parent_created", "Created a parent node from an N-child group.",
                 parent.id, parent.level, parent.hash, callback);
        }

        current = std::move(next);
        ++level;
        emit("level_completed", "Completed tree level " + std::to_string(level) + ".",
             std::nullopt, level, "", callback);
    }

    root_id_ = current.front();
    emit("root_created", "Merkle Root created successfully.", root_id_,
         nodes_.at(*root_id_).level, nodes_.at(*root_id_).hash, callback);

    const BuildResult result{
        true,
        "",
        nodes_.at(*root_id_).hash,
        arity_,
        leaf_ids_.size(),
        nodes_.size(),
        height(),
    };
    emit("build_succeeded", "N-ary Merkle Tree construction completed.",
         root_id_, nodes_.at(*root_id_).level, result.root_hash, callback);
    return result;
}

std::size_t NTreeMerkle::height() const noexcept
{
    if(!root_id_) return 0;
    return nodes_.at(*root_id_).level + 1;
}

std::string NTreeMerkle::root_hash() const
{
    return root_id_ ? nodes_.at(*root_id_).hash : "";
}

Proof NTreeMerkle::generate_proof(const std::size_t leaf_index) const
{
    if(!root_id_ || leaf_index >= leaf_ids_.size())
        throw std::out_of_range("The requested leaf index is unavailable.");

    Proof proof;
    proof.leaf_index = leaf_index;
    const Node& leaf = nodes_.at(leaf_ids_[leaf_index]);
    proof.leaf_value = leaf.value;
    proof.leaf_hash = leaf.hash;
    proof.root_hash = root_hash();

    std::size_t current_id = leaf.id;
    while(current_id != *root_id_)
    {
        const auto link = parent_links_.find(current_id);
        if(link == parent_links_.end())
            throw std::logic_error("The tree is missing a parent link.");

        const Node& parent = nodes_.at(link->second.parent_id);
        ProofStep step;
        step.level = parent.level;
        step.child_index = link->second.child_index;
        for(std::size_t index = 0; index < parent.children.size(); ++index)
        {
            if(index != step.child_index)
                step.sibling_hashes.push_back(nodes_.at(parent.children[index]).hash);
        }
        proof.steps.push_back(std::move(step));
        current_id = parent.id;
    }

    return proof;
}

bool NTreeMerkle::verify_proof(const Proof& proof, std::string* error) const
{
    auto fail = [&](const std::string& message) {
        if(error) *error = message;
        return false;
    };

    if(proof.leaf_hash != sha256_hex(proof.leaf_value))
        return fail("The leaf hash does not match the leaf value.");
    if(proof.root_hash.empty())
        return fail("The proof does not contain a Root Hash.");

    std::string current_hash = proof.leaf_hash;
    for(const ProofStep& step : proof.steps)
    {
        if(step.child_index >= arity_)
            return fail("A proof child index is outside the configured arity.");
        if(step.sibling_hashes.size() + 1 != arity_)
            return fail("A proof level does not contain N child hashes.");

        std::vector<std::string> children;
        children.reserve(arity_);
        std::size_t sibling_index = 0;
        for(std::size_t child_index = 0; child_index < arity_; ++child_index)
        {
            if(child_index == step.child_index)
                children.push_back(current_hash);
            else
                children.push_back(step.sibling_hashes[sibling_index++]);
        }

        std::string material;
        for(const std::string& child_hash : children) material += child_hash;
        current_hash = sha256_hex(material);
    }

    if(current_hash != proof.root_hash)
        return fail("The reconstructed Root Hash does not match the proof.");

    if(root_id_ && current_hash != root_hash())
        return fail("The proof does not match this tree's Root Hash.");

    if(error) error->clear();
    return true;
}

} // namespace merkle_ntree
