#ifndef SUPERMARKET_SNAPSHOT_STORAGE_HPP
#define SUPERMARKET_SNAPSHOT_STORAGE_HPP

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace supermarket::snapshot_storage
{
enum class SnapshotState
{
    Preview,
    Active,
    Superseded,
    Invalidated
};

const char* snapshot_state_name(SnapshotState state);

struct SnapshotPreview
{
    std::string protocol;
    std::string snapshot_id;
    int schema_version = 1;
    std::string generated_at;
    std::string batch_id;
    std::string manifest_json;
    std::string public_root;
    std::string source_block_hash;
    std::string route_fingerprint;
    std::string candidate_json;
};

struct SnapshotRecord
{
    std::string protocol;
    std::string snapshot_id;
    int revision = 0;
    int schema_version = 1;
    std::string generated_at;
    std::string batch_id;
    std::string manifest_json;
    std::string public_root;
    std::string source_block_hash;
    std::string route_fingerprint;
    SnapshotState state = SnapshotState::Preview;
    std::string archive_path;
    std::string candidate_json;
    std::string publication_json;
    std::string transaction_hash;
    std::string published_at;
};

struct VerificationStatus
{
    std::string batch_id;
    std::string snapshot_id;
    std::string checked_at;
    std::string status;
    std::string message;
};

struct RefreshPolicy
{
    std::string product;
    int interval_seconds = 3600;
    std::string updated_at;
};

struct AvailabilityWindow
{
    std::string batch_id;
    std::string available_from;
    std::string available_until;
    std::string updated_at;
};

int default_refresh_interval_seconds();

class SnapshotStore
{
public:
    SnapshotStore(std::string database_path, std::string archive_root);

    bool initialize(std::string& error);

    bool save_preview(const SnapshotPreview& preview, std::string& error);

    bool mark_published(const SnapshotPreview& preview,
                        const std::string& publication_json,
                        const std::string& transaction_hash,
                        std::string& error);

    bool invalidate_batch(const std::string& batch_id, std::string& error);

    std::optional<SnapshotRecord> active_snapshot(
        const std::string& batch_id) const;

    std::optional<SnapshotRecord> latest_publication(
        const std::string& batch_id) const;

    bool touch_verification(const std::string& batch_id,
                            const std::string& snapshot_id,
                            const std::string& checked_at,
                            const std::string& status,
                            const std::string& message,
                            std::string& error);

    std::optional<VerificationStatus> verification_status(
        const std::string& batch_id) const;

    std::vector<RefreshPolicy> refresh_policies() const;

    std::vector<AvailabilityWindow> availability_windows() const;

    std::optional<AvailabilityWindow> availability_window(
        const std::string& batch_id) const;

    bool save_refresh_settings(const std::string& product,
                               int interval_seconds,
                               const std::string& batch_id,
                               const std::string& available_from,
                               const std::string& available_until,
                               std::string& error);

    bool refresh_due(const std::string& batch_id) const;

    std::optional<std::chrono::milliseconds> next_refresh_delay() const;

private:
    std::string database_path_;
    std::string archive_root_;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, SnapshotRecord> active_cache_;
};
}

#endif
