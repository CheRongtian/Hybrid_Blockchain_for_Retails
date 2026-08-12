#ifndef SCHNUCKS_PUBLIC_SNAPSHOT_HPP
#define SCHNUCKS_PUBLIC_SNAPSHOT_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace schnucks::snapshot
{
struct EvidenceInput
{
    std::string stage;
    std::string category;
    std::string cid;
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
    std::vector<PublicField> public_fields;
    std::vector<PublicEvidence> public_evidence;
    std::vector<std::string> excluded_fields;
};

Eligibility evaluate_eligibility(const BatchInput& input);

std::optional<Preview> build_preview(
    const BatchInput& input,
    const std::vector<EvidenceInput>& selected_evidence,
    std::string& error);
}

#endif
