#ifndef SUPERMARKET_PUBLIC_SNAPSHOT_HPP
#define SUPERMARKET_PUBLIC_SNAPSHOT_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace supermarket::snapshot
{
struct EvidenceInput
{
    std::string stage;
    std::string category;
    std::string cid;
};

struct RouteNodeInput
{
    std::string node_id;
    std::string label;
    std::string role;
    std::string username;
    int step_index = -1;
    std::string node_type;
};

struct RouteEdgeInput
{
    std::string from_node_id;
    std::string to_node_id;
};

struct StageInput
{
    int block_id = -1;
    int parent_block_id = -1;
    std::string parent_block_hash = "GENESIS";
    std::string stage;
    std::map<std::string, std::string> event_fields;
    std::vector<EvidenceInput> evidence;
    std::string block_hash;
    std::string chain_status;
    bool verified = false;
    bool signature_verified = false;
    std::string route_node_id;
    int route_step_index = -1;
    std::string route_node_label;
    std::string route_node_username;
};

struct HistoricalBlockInput
{
    int block_id = -1;
    int parent_block_id = -1;
    std::string parent_block_hash = "GENESIS";
    std::string block_hash;
    std::string route_node_id;
    int route_step_index = -1;
};

struct BatchInput
{
    std::string batch_id;
    std::string product;
    std::string harvest_date;
    std::string farm_location;
    std::string certificate_id;
    std::string status;
    std::vector<StageInput> stages;
    std::vector<HistoricalBlockInput> historical_blocks;
    std::vector<RouteNodeInput> route_nodes;
    std::vector<RouteEdgeInput> route_edges;
    std::vector<std::string> source_errors;
};

struct Eligibility
{
    bool eligible = false;
    std::vector<std::string> errors;
};

struct PublicField
{
    std::string name;
    std::string value;
};

struct PublicEvidence
{
    std::string stage;
    std::string type;
    std::string cid;
};

struct Preview
{
    std::string protocol;
    std::string snapshot_id;
    int snapshot_version = 1;
    std::string generated_at;
    std::string batch_id;
    std::string manifest_json;
    std::string public_root;
    std::string final_private_block_hash;
    std::string route_fingerprint;
    std::vector<PublicField> public_fields;
    std::vector<PublicEvidence> public_evidence;
    std::vector<std::string> excluded_fields;
};

Eligibility evaluate_eligibility(const BatchInput& input);

std::string route_fingerprint(
    const std::vector<RouteNodeInput>& route_nodes,
    const std::vector<RouteEdgeInput>& route_edges);

std::optional<Preview> build_preview(
    const BatchInput& input,
    const std::vector<EvidenceInput>& selected_evidence,
    std::string& error);
}

#endif
