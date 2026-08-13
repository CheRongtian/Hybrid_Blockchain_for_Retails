#include "snapshot_policy.hpp"

namespace supermarket::snapshot
{
const std::vector<EvidencePolicy>& public_evidence_policy()
{
    static const std::vector<EvidencePolicy> policy = {
        {"harvestPhotos", "harvest_photo", "Harvest photos", true},
        {"inspectionReports", "inspection_report", "Inspection reports", false},
        {"sealVerificationImages", "seal_verification_image",
         "Seal verification images", false},
        {"productPhotosLabels", "product_photo_or_label",
         "Product photos and labels", true},
        {"recallNotices", "recall_notice", "Recall notices", true}
    };
    return policy;
}

const EvidencePolicy* find_evidence_policy(const std::string& category)
{
    for(const EvidencePolicy& item : public_evidence_policy())
    {
        if(item.category == category) return &item;
    }
    return nullptr;
}

const std::vector<std::string>& excluded_private_fields()
{
    static const std::vector<std::string> fields = {
        "Participant UID, username, display name, and organization ID",
        "Digital signatures, public keys, challenges, and signed payloads",
        "Private Merkle leaves, proofs, roots, and canonical records",
        "Shipment ID and vehicle or container ID",
        "Storage lot, zone, and rack identifiers",
        "Raw GPS, sensor, energy, receipt, and transaction records",
        "Attachment filenames, content types, sizes, and unapproved CIDs"
    };
    return fields;
}
}
