#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace merkle_ntree {

struct Node
{
    std::size_t id = 0;
    std::size_t level = 0;
    std::size_t index = 0;
    bool leaf = false;
    bool padding = false;
    std::string value;
    std::string hash;
    std::vector<std::size_t> children;
    std::optional<std::size_t> duplicate_of;
};

struct ProofStep
{
    std::size_t level = 0;
    std::size_t child_index = 0;
    std::vector<std::string> sibling_hashes;
};

struct Proof
{
    std::size_t leaf_index = 0;
    std::string leaf_value;
    std::string leaf_hash;
    std::string root_hash;
    std::vector<ProofStep> steps;
};

struct BuildEvent
{
    std::string type;
    std::string message;
    std::size_t arity = 0;
    std::size_t level = 0;
    std::optional<std::size_t> node_id;
    std::string node_hash;
    std::string root_hash;
    std::vector<Node> nodes;
};

using EventCallback = std::function<void(const BuildEvent&)>;

struct BuildResult
{
    bool success = false;
    std::string error;
    std::string root_hash;
    std::size_t arity = 0;
    std::size_t leaf_count = 0;
    std::size_t node_count = 0;
    std::size_t height = 0;
};

class NTreeMerkle
{
public:
    explicit NTreeMerkle(std::size_t arity);

    BuildResult build(const std::vector<std::string>& leaves,
                      EventCallback callback = {});

    Proof generate_proof(std::size_t leaf_index) const;
    bool verify_proof(const Proof& proof, std::string* error = nullptr) const;

    std::size_t arity() const noexcept { return arity_; }
    std::size_t leaf_count() const noexcept { return leaf_ids_.size(); }
    std::size_t height() const noexcept;
    std::string root_hash() const;
    const std::vector<Node>& nodes() const noexcept { return nodes_; }

    static std::string sha256_hex(const std::string& value);

private:
    struct ParentLink
    {
        std::size_t parent_id = 0;
        std::size_t child_index = 0;
    };

    void emit(const std::string& type,
              const std::string& message,
              const std::optional<std::size_t>& node_id,
              std::size_t level,
              const std::string& node_hash,
              EventCallback& callback) const;

    std::size_t arity_;
    std::vector<Node> nodes_;
    std::vector<std::size_t> leaf_ids_;
    std::optional<std::size_t> root_id_;
    std::unordered_map<std::size_t, ParentLink> parent_links_;
};

} // namespace merkle_ntree
