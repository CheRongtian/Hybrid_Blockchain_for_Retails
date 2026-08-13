#ifndef SUPERMARKET_SNAPSHOT_POLICY_HPP
#define SUPERMARKET_SNAPSHOT_POLICY_HPP

#include <string>
#include <vector>

namespace supermarket::snapshot
{
struct EvidencePolicy
{
    std::string category;
    std::string public_type;
    std::string label;
    bool selected_by_default = false;
};

const std::vector<EvidencePolicy>& public_evidence_policy();
const EvidencePolicy* find_evidence_policy(const std::string& category);
const std::vector<std::string>& excluded_private_fields();
}

#endif
