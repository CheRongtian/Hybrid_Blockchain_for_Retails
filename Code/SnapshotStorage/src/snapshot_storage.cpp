#include "snapshot_storage.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace supermarket::snapshot_storage
{
namespace
{
constexpr int SQLITE_BUSY_TIMEOUT_MS = 5000;
constexpr int DEFAULT_REFRESH_INTERVAL_SECONDS = 60 * 60;

const char* state_name(SnapshotState state)
{
    switch(state)
    {
        case SnapshotState::Preview: return "preview";
        case SnapshotState::Active: return "active";
        case SnapshotState::Superseded: return "superseded";
        case SnapshotState::Invalidated: return "invalidated";
    }
    return "preview";
}

SnapshotState state_from_name(const std::string& value)
{
    if(value == "active") return SnapshotState::Active;
    if(value == "superseded") return SnapshotState::Superseded;
    if(value == "invalidated") return SnapshotState::Invalidated;
    return SnapshotState::Preview;
}

std::string utc_timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream value;
    value << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return value.str();
}

std::string sqlite_error(sqlite3* database, const std::string& operation)
{
    return operation + ": " + (database ? sqlite3_errmsg(database) : "unknown SQLite error");
}

bool execute_sql(sqlite3* database,
                 const char* sql,
                 std::string& error)
{
    char* message = nullptr;
    if(sqlite3_exec(database, sql, nullptr, nullptr, &message) == SQLITE_OK)
        return true;

    error = message ? message : sqlite_error(database, "SQLite operation failed");
    sqlite3_free(message);
    return false;
}

bool open_database(const std::string& path,
                   int flags,
                   sqlite3*& database,
                   std::string& error)
{
    database = nullptr;
    if(sqlite3_open_v2(path.c_str(), &database, flags, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Open snapshot storage database failed");
        if(database) sqlite3_close(database);
        database = nullptr;
        return false;
    }
    sqlite3_busy_timeout(database, SQLITE_BUSY_TIMEOUT_MS);
    return true;
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value)
{
    return sqlite3_bind_text(statement, index, value.c_str(), -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

std::string column_text(sqlite3_stmt* statement, int index)
{
    const unsigned char* value = sqlite3_column_text(statement, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

std::optional<std::string> normalized_utc_minute(
    sqlite3* database,
    const std::string& value)
{
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT strftime('%Y-%m-%dT%H:%M:00Z', ?);";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
       !bind_text(statement, 1, value))
    {
        if(statement) sqlite3_finalize(statement);
        return std::nullopt;
    }
    const int result = sqlite3_step(statement);
    std::optional<std::string> normalized;
    if(result == SQLITE_ROW && sqlite3_column_type(statement, 0) != SQLITE_NULL)
        normalized = column_text(statement, 0);
    sqlite3_finalize(statement);
    return normalized;
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

std::string safe_path_component(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    for(const unsigned char character : value)
    {
        if(std::isalnum(character) || character == '-' || character == '_' ||
           character == '.')
            result += static_cast<char>(character);
        else
            result += '_';
    }
    return result.empty() ? "unknown" : result;
}

std::string archive_envelope(const SnapshotRecord& record)
{
    std::ostringstream json;
    json << "{\n"
         << "  \"storageState\": " << json_string(state_name(record.state)) << ",\n"
         << "  \"revision\": " << record.revision << ",\n"
         << "  \"protocol\": " << json_string(record.protocol) << ",\n"
         << "  \"snapshotId\": " << json_string(record.snapshot_id) << ",\n"
         << "  \"schemaVersion\": " << record.schema_version << ",\n"
         << "  \"generatedAt\": " << json_string(record.generated_at) << ",\n"
         << "  \"batchId\": " << json_string(record.batch_id) << ",\n"
         << "  \"publicRoot\": " << json_string(record.public_root) << ",\n"
         << "  \"sourceBlockHash\": " << json_string(record.source_block_hash) << ",\n"
         << "  \"routeFingerprint\": " << json_string(record.route_fingerprint) << ",\n"
         << "  \"manifestJson\": " << json_string(record.manifest_json) << ",\n"
         << "  \"candidateJson\": " << json_string(record.candidate_json) << ",\n"
         << "  \"publicationJson\": " << json_string(record.publication_json) << ",\n"
         << "  \"transactionHash\": " << json_string(record.transaction_hash) << ",\n"
         << "  \"publishedAt\": " << json_string(record.published_at) << "\n"
         << "}\n";
    return json.str();
}

bool write_archive(const SnapshotRecord& record, std::string& error)
{
    const fs::path path(record.archive_path);
    std::error_code filesystem_error;
    fs::create_directories(path.parent_path(), filesystem_error);
    if(filesystem_error)
    {
        error = "Create Snapshot archive directory failed: " +
                filesystem_error.message();
        return false;
    }

    const fs::path temporary = path.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if(!output)
        {
            error = "Open Snapshot archive file failed: " + temporary.string();
            return false;
        }
        output << archive_envelope(record);
        if(!output)
        {
            error = "Write Snapshot archive file failed: " + temporary.string();
            return false;
        }
    }

    fs::remove(path, filesystem_error);
    filesystem_error.clear();
    fs::rename(temporary, path, filesystem_error);
    if(filesystem_error)
    {
        error = "Commit Snapshot archive file failed: " +
                filesystem_error.message();
        fs::remove(temporary, filesystem_error);
        return false;
    }
    return true;
}

bool create_schema(sqlite3* database, std::string& error)
{
    return execute_sql(
        database,
        "CREATE TABLE IF NOT EXISTS snapshot_storage ("
        "snapshot_id TEXT PRIMARY KEY,"
        "batch_id TEXT NOT NULL,"
        "revision INTEGER NOT NULL,"
        "protocol TEXT NOT NULL,"
        "schema_version INTEGER NOT NULL,"
        "generated_at TEXT NOT NULL,"
        "public_root TEXT NOT NULL,"
        "source_block_hash TEXT NOT NULL,"
        "route_fingerprint TEXT NOT NULL,"
        "state TEXT NOT NULL CHECK (state IN "
        "('preview', 'active', 'superseded', 'invalidated')),"
        "archive_path TEXT NOT NULL,"
        "candidate_json TEXT NOT NULL DEFAULT '',"
        "manifest_json TEXT NOT NULL DEFAULT '',"
        "publication_json TEXT NOT NULL DEFAULT '',"
        "transaction_hash TEXT NOT NULL DEFAULT '',"
        "published_at TEXT NOT NULL DEFAULT '',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE (batch_id, revision)"
        ");"
        "CREATE TABLE IF NOT EXISTS snapshot_verification_status ("
        "batch_id TEXT PRIMARY KEY,"
        "snapshot_id TEXT NOT NULL DEFAULT '',"
        "checked_at TEXT NOT NULL DEFAULT '',"
        "status TEXT NOT NULL DEFAULT '',"
        "message TEXT NOT NULL DEFAULT '',"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_snapshot_storage_batch_state "
        "ON snapshot_storage (batch_id, state, revision);"
        "CREATE TABLE IF NOT EXISTS snapshot_refresh_policies ("
        "product TEXT PRIMARY KEY,"
        "interval_seconds INTEGER NOT NULL CHECK (interval_seconds > 0),"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS snapshot_refresh_schedule ("
        "batch_id TEXT PRIMARY KEY,"
        "next_refresh_at TEXT NOT NULL DEFAULT ''"
        ");"
        "CREATE TABLE IF NOT EXISTS snapshot_availability_windows ("
        "batch_id TEXT PRIMARY KEY,"
        "available_from TEXT NOT NULL,"
        "available_until TEXT NOT NULL,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "CHECK (julianday(available_from) IS NOT NULL),"
        "CHECK (julianday(available_until) IS NOT NULL),"
        "CHECK (julianday(available_until) > julianday(available_from))"
        ");",
       error);
}

bool read_availability_window(sqlite3* database,
                              const std::string& batch_id,
                              AvailabilityWindow& window,
                              bool& found)
{
    found = false;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT batch_id, available_from, available_until, updated_at "
        "FROM snapshot_availability_windows WHERE batch_id = ? LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
       !bind_text(statement, 1, batch_id))
    {
        if(statement) sqlite3_finalize(statement);
        return false;
    }
    const int result = sqlite3_step(statement);
    if(result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return true;
    }
    if(result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return false;
    }
    found = true;
    window.batch_id = column_text(statement, 0);
    window.available_from = column_text(statement, 1);
    window.available_until = column_text(statement, 2);
    window.updated_at = column_text(statement, 3);
    sqlite3_finalize(statement);
    return true;
}

bool read_verification_status(sqlite3* database,
                              const std::string& batch_id,
                              VerificationStatus& status,
                              bool& found)
{
    found = false;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT batch_id, snapshot_id, checked_at, status, message "
        "FROM snapshot_verification_status WHERE batch_id = ? LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
        return false;
    if(!bind_text(statement, 1, batch_id))
    {
        sqlite3_finalize(statement);
        return false;
    }
    const int result = sqlite3_step(statement);
    if(result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return true;
    }
    if(result != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        return false;
    }
    found = true;
    status.batch_id = column_text(statement, 0);
    status.snapshot_id = column_text(statement, 1);
    status.checked_at = column_text(statement, 2);
    status.status = column_text(statement, 3);
    status.message = column_text(statement, 4);
    sqlite3_finalize(statement);
    return true;
}

bool read_record_by_snapshot_id(sqlite3* database,
                                const std::string& snapshot_id,
                                SnapshotRecord& record,
                                bool& found,
                                std::string& error)
{
    found = false;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT protocol, snapshot_id, revision, schema_version, generated_at, "
        "batch_id, manifest_json, public_root, source_block_hash, "
        "route_fingerprint, state, archive_path, candidate_json, publication_json, "
        "transaction_hash, published_at FROM snapshot_storage "
        "WHERE snapshot_id = ? LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare Snapshot lookup failed");
        return false;
    }
    if(!bind_text(statement, 1, snapshot_id))
    {
        error = sqlite_error(database, "Bind Snapshot lookup failed");
        sqlite3_finalize(statement);
        return false;
    }
    const int result = sqlite3_step(statement);
    if(result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return true;
    }
    if(result != SQLITE_ROW)
    {
        error = sqlite_error(database, "Read Snapshot lookup failed");
        sqlite3_finalize(statement);
        return false;
    }

    found = true;
    record.protocol = column_text(statement, 0);
    record.snapshot_id = column_text(statement, 1);
    record.revision = sqlite3_column_int(statement, 2);
    record.schema_version = sqlite3_column_int(statement, 3);
    record.generated_at = column_text(statement, 4);
    record.batch_id = column_text(statement, 5);
    record.manifest_json = column_text(statement, 6);
    record.public_root = column_text(statement, 7);
    record.source_block_hash = column_text(statement, 8);
    record.route_fingerprint = column_text(statement, 9);
    record.state = state_from_name(column_text(statement, 10));
    record.archive_path = column_text(statement, 11);
    record.candidate_json = column_text(statement, 12);
    record.publication_json = column_text(statement, 13);
    record.transaction_hash = column_text(statement, 14);
    record.published_at = column_text(statement, 15);
    sqlite3_finalize(statement);
    return true;
}

bool read_latest_publication_by_batch(sqlite3* database,
                                      const std::string& batch_id,
                                      SnapshotRecord& record,
                                      bool& found,
                                      std::string& error)
{
    found = false;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT protocol, snapshot_id, revision, schema_version, generated_at, "
        "batch_id, manifest_json, public_root, source_block_hash, "
        "route_fingerprint, state, archive_path, candidate_json, publication_json, "
        "transaction_hash, published_at FROM snapshot_storage "
        "WHERE batch_id = ? AND state IN "
        "('active', 'superseded', 'invalidated') "
        "ORDER BY revision DESC LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare latest Snapshot lookup failed");
        return false;
    }
    if(!bind_text(statement, 1, batch_id))
    {
        error = sqlite_error(database, "Bind latest Snapshot lookup failed");
        sqlite3_finalize(statement);
        return false;
    }
    const int result = sqlite3_step(statement);
    if(result == SQLITE_DONE)
    {
        sqlite3_finalize(statement);
        return true;
    }
    if(result != SQLITE_ROW)
    {
        error = sqlite_error(database, "Read latest Snapshot lookup failed");
        sqlite3_finalize(statement);
        return false;
    }

    found = true;
    record.protocol = column_text(statement, 0);
    record.snapshot_id = column_text(statement, 1);
    record.revision = sqlite3_column_int(statement, 2);
    record.schema_version = sqlite3_column_int(statement, 3);
    record.generated_at = column_text(statement, 4);
    record.batch_id = column_text(statement, 5);
    record.manifest_json = column_text(statement, 6);
    record.public_root = column_text(statement, 7);
    record.source_block_hash = column_text(statement, 8);
    record.route_fingerprint = column_text(statement, 9);
    record.state = state_from_name(column_text(statement, 10));
    record.archive_path = column_text(statement, 11);
    record.candidate_json = column_text(statement, 12);
    record.publication_json = column_text(statement, 13);
    record.transaction_hash = column_text(statement, 14);
    record.published_at = column_text(statement, 15);
    sqlite3_finalize(statement);
    return true;
}

bool next_revision(sqlite3* database,
                   const std::string& batch_id,
                   int& revision,
                   std::string& error)
{
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT COALESCE(MAX(revision), 0) + 1 FROM snapshot_storage "
        "WHERE batch_id = ?;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare Snapshot revision lookup failed");
        return false;
    }
    if(!bind_text(statement, 1, batch_id) || sqlite3_step(statement) != SQLITE_ROW)
    {
        error = sqlite_error(database, "Read Snapshot revision failed");
        sqlite3_finalize(statement);
        return false;
    }
    revision = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return true;
}

std::string archive_path_for(const std::string& archive_root,
                             const SnapshotRecord& record)
{
    const fs::path path = fs::path(archive_root) /
        safe_path_component(record.batch_id) /
        (safe_path_component(record.snapshot_id) + "-r" +
         std::to_string(record.revision) + ".json");
    return path.string();
}

bool insert_record(sqlite3* database,
                   const SnapshotRecord& record,
                   std::string& error)
{
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "INSERT INTO snapshot_storage (snapshot_id, batch_id, revision, protocol, "
        "schema_version, generated_at, public_root, source_block_hash, "
        "route_fingerprint, state, archive_path, candidate_json, manifest_json, "
        "publication_json, transaction_hash, published_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare Snapshot insert failed");
        return false;
    }
    bool bound = bind_text(statement, 1, record.snapshot_id) &&
                 bind_text(statement, 2, record.batch_id) &&
                 sqlite3_bind_int(statement, 3, record.revision) == SQLITE_OK &&
                 bind_text(statement, 4, record.protocol) &&
                 sqlite3_bind_int(statement, 5, record.schema_version) == SQLITE_OK &&
                 bind_text(statement, 6, record.generated_at) &&
                 bind_text(statement, 7, record.public_root) &&
                 bind_text(statement, 8, record.source_block_hash) &&
                 bind_text(statement, 9, record.route_fingerprint) &&
                 bind_text(statement, 10, state_name(record.state)) &&
                 bind_text(statement, 11, record.archive_path) &&
                 bind_text(statement, 12, record.candidate_json) &&
                 bind_text(statement, 13, record.manifest_json) &&
                 bind_text(statement, 14, record.publication_json) &&
                 bind_text(statement, 15, record.transaction_hash) &&
                 bind_text(statement, 16, record.published_at);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded) error = sqlite_error(database, "Insert Snapshot failed");
    sqlite3_finalize(statement);
    return succeeded;
}

bool update_preview(sqlite3* database,
                    const SnapshotRecord& record,
                    std::string& error)
{
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "UPDATE snapshot_storage SET batch_id = ?, revision = ?, protocol = ?, "
        "schema_version = ?, generated_at = ?, public_root = ?, "
        "source_block_hash = ?, route_fingerprint = ?, state = ?, archive_path = ?, "
        "candidate_json = ?, manifest_json = ?, publication_json = '', "
        "transaction_hash = '', published_at = '', updated_at = CURRENT_TIMESTAMP "
        "WHERE snapshot_id = ?;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare Snapshot preview update failed");
        return false;
    }
    bool bound = bind_text(statement, 1, record.batch_id) &&
                 sqlite3_bind_int(statement, 2, record.revision) == SQLITE_OK &&
                 bind_text(statement, 3, record.protocol) &&
                 sqlite3_bind_int(statement, 4, record.schema_version) == SQLITE_OK &&
                 bind_text(statement, 5, record.generated_at) &&
                 bind_text(statement, 6, record.public_root) &&
                 bind_text(statement, 7, record.source_block_hash) &&
                 bind_text(statement, 8, record.route_fingerprint) &&
                 bind_text(statement, 9, state_name(record.state)) &&
                 bind_text(statement, 10, record.archive_path) &&
                 bind_text(statement, 11, record.candidate_json) &&
                 bind_text(statement, 12, record.manifest_json) &&
                 bind_text(statement, 13, record.snapshot_id);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded) error = sqlite_error(database, "Update Snapshot preview failed");
    sqlite3_finalize(statement);
    return succeeded;
}

bool update_publication(sqlite3* database,
                        const SnapshotRecord& record,
                        std::string& error)
{
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "UPDATE snapshot_storage SET state = ?, publication_json = ?, "
        "transaction_hash = ?, published_at = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE snapshot_id = ?;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare Snapshot publication update failed");
        return false;
    }
    const bool bound = bind_text(statement, 1, state_name(record.state)) &&
                       bind_text(statement, 2, record.publication_json) &&
                       bind_text(statement, 3, record.transaction_hash) &&
                       bind_text(statement, 4, record.published_at) &&
                       bind_text(statement, 5, record.snapshot_id);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE &&
                           sqlite3_changes(database) == 1;
    if(!succeeded) error = sqlite_error(database, "Update Snapshot publication failed");
    sqlite3_finalize(statement);
    return succeeded;
}

bool load_active_from_database(const std::string& database_path,
                               const std::string& batch_id,
                               SnapshotRecord& record)
{
    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path, SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT protocol, snapshot_id, revision, schema_version, generated_at, "
        "batch_id, manifest_json, public_root, source_block_hash, "
        "route_fingerprint, state, archive_path, candidate_json, publication_json, "
        "transaction_hash, published_at FROM snapshot_storage "
        "WHERE batch_id = ? AND state = 'active' "
        "ORDER BY revision DESC LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
       !bind_text(statement, 1, batch_id) || sqlite3_step(statement) != SQLITE_ROW)
    {
        if(statement) sqlite3_finalize(statement);
        sqlite3_close(database);
        return false;
    }

    record.protocol = column_text(statement, 0);
    record.snapshot_id = column_text(statement, 1);
    record.revision = sqlite3_column_int(statement, 2);
    record.schema_version = sqlite3_column_int(statement, 3);
    record.generated_at = column_text(statement, 4);
    record.batch_id = column_text(statement, 5);
    record.manifest_json = column_text(statement, 6);
    record.public_root = column_text(statement, 7);
    record.source_block_hash = column_text(statement, 8);
    record.route_fingerprint = column_text(statement, 9);
    record.state = state_from_name(column_text(statement, 10));
    record.archive_path = column_text(statement, 11);
    record.candidate_json = column_text(statement, 12);
    record.publication_json = column_text(statement, 13);
    record.transaction_hash = column_text(statement, 14);
    record.published_at = column_text(statement, 15);
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return true;
}
}

const char* snapshot_state_name(SnapshotState state)
{
    return state_name(state);
}

int default_refresh_interval_seconds()
{
    return DEFAULT_REFRESH_INTERVAL_SECONDS;
}

SnapshotStore::SnapshotStore(std::string database_path, std::string archive_root)
    : database_path_(std::move(database_path)), archive_root_(std::move(archive_root))
{
}

bool SnapshotStore::initialize(std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();

    std::error_code filesystem_error;
    fs::create_directories(fs::path(archive_root_), filesystem_error);
    if(filesystem_error)
    {
        error = "Create Snapshot archive root failed: " + filesystem_error.message();
        return false;
    }

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;
    const bool succeeded = create_schema(database, error);
    sqlite3_close(database);
    if(!succeeded) return false;

    active_cache_.clear();
    return true;
}

bool SnapshotStore::save_preview(const SnapshotPreview& preview,
                                 std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if(preview.batch_id.empty() || preview.snapshot_id.empty() ||
       preview.protocol.empty() || preview.manifest_json.empty() ||
       preview.public_root.empty())
    {
        error = "Snapshot preview storage received incomplete metadata";
        return false;
    }

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;
    if(!execute_sql(database, "BEGIN IMMEDIATE;", error))
    {
        sqlite3_close(database);
        return false;
    }

    SnapshotRecord record;
    bool found = false;
    if(!read_record_by_snapshot_id(database, preview.snapshot_id, record, found, error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    if(found && record.state == SnapshotState::Active)
    {
        error = "The Snapshot ID is already active";
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }

    record.protocol = preview.protocol;
    record.snapshot_id = preview.snapshot_id;
    record.schema_version = preview.schema_version;
    record.generated_at = preview.generated_at.empty()
        ? utc_timestamp() : preview.generated_at;
    record.batch_id = preview.batch_id;
    record.manifest_json = preview.manifest_json;
    record.public_root = preview.public_root;
    record.source_block_hash = preview.source_block_hash;
    record.route_fingerprint = preview.route_fingerprint;
    record.state = SnapshotState::Preview;
    record.candidate_json = preview.candidate_json;
    record.publication_json.clear();
    record.transaction_hash.clear();
    record.published_at.clear();

    if(!found)
    {
        if(!next_revision(database, preview.batch_id, record.revision, error))
        {
            execute_sql(database, "ROLLBACK;", error);
            sqlite3_close(database);
            return false;
        }
        record.archive_path = archive_path_for(archive_root_, record);
    }

    sqlite3_stmt* supersede = nullptr;
    const char* supersede_sql =
        "UPDATE snapshot_storage SET state = 'superseded', "
        "updated_at = CURRENT_TIMESTAMP WHERE batch_id = ? AND state = 'preview' "
        "AND snapshot_id <> ?;";
    if(sqlite3_prepare_v2(database, supersede_sql, -1, &supersede, nullptr) != SQLITE_OK ||
       !bind_text(supersede, 1, preview.batch_id) ||
       !bind_text(supersede, 2, preview.snapshot_id) ||
       sqlite3_step(supersede) != SQLITE_DONE)
    {
        if(supersede) sqlite3_finalize(supersede);
        error = sqlite_error(database, "Supersede old Snapshot previews failed");
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(supersede);

    const bool stored = found
        ? update_preview(database, record, error)
        : insert_record(database, record, error);
    if(!stored || !execute_sql(database, "COMMIT;", error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_close(database);

    return write_archive(record, error);
}

bool SnapshotStore::mark_published(const SnapshotPreview& preview,
                                   const std::string& publication_json,
                                   const std::string& transaction_hash,
                                   std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if(preview.batch_id.empty() || preview.snapshot_id.empty() ||
       preview.protocol.empty() || publication_json.empty())
    {
        error = "Published Snapshot metadata is incomplete";
        return false;
    }

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;
    if(!execute_sql(database, "BEGIN IMMEDIATE;", error))
    {
        sqlite3_close(database);
        return false;
    }

    SnapshotRecord record;
    bool found = false;
    if(!read_record_by_snapshot_id(database, preview.snapshot_id, record, found, error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    if(!found)
    {
        record.protocol = preview.protocol;
        record.snapshot_id = preview.snapshot_id;
        record.schema_version = preview.schema_version;
        record.generated_at = preview.generated_at.empty()
            ? utc_timestamp() : preview.generated_at;
        record.batch_id = preview.batch_id;
        record.manifest_json = preview.manifest_json;
        record.public_root = preview.public_root;
        record.source_block_hash = preview.source_block_hash;
        record.route_fingerprint = preview.route_fingerprint;
        record.candidate_json = preview.candidate_json;
        if(!next_revision(database, preview.batch_id, record.revision, error))
        {
            execute_sql(database, "ROLLBACK;", error);
            sqlite3_close(database);
            return false;
        }
        record.archive_path = archive_path_for(archive_root_, record);
    }

    sqlite3_stmt* supersede = nullptr;
    const char* supersede_sql =
        "UPDATE snapshot_storage SET state = 'superseded', "
        "updated_at = CURRENT_TIMESTAMP WHERE batch_id = ? AND state = 'active' "
        "AND snapshot_id <> ?;";
    if(sqlite3_prepare_v2(database, supersede_sql, -1, &supersede, nullptr) != SQLITE_OK ||
       !bind_text(supersede, 1, preview.batch_id) ||
       !bind_text(supersede, 2, preview.snapshot_id) ||
       sqlite3_step(supersede) != SQLITE_DONE)
    {
        if(supersede) sqlite3_finalize(supersede);
        error = sqlite_error(database, "Supersede old active Snapshot failed");
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(supersede);

    record.state = SnapshotState::Active;
    record.publication_json = publication_json;
    record.transaction_hash = transaction_hash;
    record.published_at = utc_timestamp();
    const bool stored = found
        ? update_publication(database, record, error)
        : insert_record(database, record, error);
    if(!stored || !execute_sql(database, "COMMIT;", error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_close(database);

    if(!write_archive(record, error)) return false;
    active_cache_[record.batch_id] = record;
    return true;
}

bool SnapshotStore::invalidate_batch(const std::string& batch_id,
                                     std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if(batch_id.empty()) return true;

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;
    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "UPDATE snapshot_storage SET state = 'invalidated', "
        "updated_at = CURRENT_TIMESTAMP WHERE batch_id = ? "
        "AND state IN ('preview', 'active');";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
       !bind_text(statement, 1, batch_id) || sqlite3_step(statement) != SQLITE_DONE)
    {
        if(statement) sqlite3_finalize(statement);
        error = sqlite_error(database, "Invalidate batch Snapshots failed");
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(statement);

    sqlite3_stmt* schedule_statement = nullptr;
    const char* schedule_sql =
        "UPDATE snapshot_refresh_schedule SET next_refresh_at = '' "
        "WHERE batch_id = ?;";
    const bool schedule_reset =
        sqlite3_prepare_v2(database, schedule_sql, -1, &schedule_statement, nullptr) ==
            SQLITE_OK &&
        bind_text(schedule_statement, 1, batch_id) &&
        sqlite3_step(schedule_statement) == SQLITE_DONE;
    if(!schedule_reset)
    {
        error = sqlite_error(database, "Reset invalidated Snapshot schedule failed");
        if(schedule_statement) sqlite3_finalize(schedule_statement);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(schedule_statement);
    sqlite3_close(database);
    active_cache_.erase(batch_id);
    return true;
}

std::optional<SnapshotRecord> SnapshotStore::active_snapshot(
    const std::string& batch_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cached = active_cache_.find(batch_id);
    if(cached != active_cache_.end()) return cached->second;

    SnapshotRecord record;
    if(!load_active_from_database(database_path_, batch_id, record))
        return std::nullopt;
    active_cache_[batch_id] = record;
    return record;
}

std::optional<SnapshotRecord> SnapshotStore::latest_publication(
    const std::string& batch_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(batch_id.empty()) return std::nullopt;

    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return std::nullopt;

    SnapshotRecord record;
    bool found = false;
    const bool read = read_latest_publication_by_batch(
        database, batch_id, record, found, error);
    sqlite3_close(database);
    if(!read || !found) return std::nullopt;
    return record;
}

bool SnapshotStore::touch_verification(const std::string& batch_id,
                                        const std::string& snapshot_id,
                                        const std::string& checked_at,
                                        const std::string& status,
                                        const std::string& message,
                                        std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if(batch_id.empty())
    {
        error = "Verification status requires a batch ID";
        return false;
    }

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;
    if(!execute_sql(database, "BEGIN IMMEDIATE;", error))
    {
        sqlite3_close(database);
        return false;
    }

    const char* sql =
        "INSERT INTO snapshot_verification_status "
        "(batch_id, snapshot_id, checked_at, status, message) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(batch_id) DO UPDATE SET "
        "snapshot_id = excluded.snapshot_id, "
        "checked_at = excluded.checked_at, "
        "status = excluded.status, "
        "message = excluded.message, "
        "updated_at = CURRENT_TIMESTAMP;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        error = sqlite_error(database, "Prepare verification status update failed");
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    const std::string effective_checked_at = checked_at.empty()
        ? utc_timestamp() : checked_at;
    const bool bound = bind_text(statement, 1, batch_id) &&
        bind_text(statement, 2, snapshot_id) &&
        bind_text(statement, 3, effective_checked_at) &&
        bind_text(statement, 4, status) &&
        bind_text(statement, 5, message);
    if(!bound || sqlite3_step(statement) != SQLITE_DONE)
    {
        error = sqlite_error(database, "Write verification status failed");
        sqlite3_finalize(statement);
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(statement);

    sqlite3_stmt* schedule_statement = nullptr;
    const char* schedule_sql =
        "INSERT INTO snapshot_refresh_schedule (batch_id, next_refresh_at) "
        "SELECT b.batch_id, CASE "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') < julianday(w.available_from) "
        "THEN w.available_from "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') >= julianday(w.available_until) THEN '' "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now', '+' || CAST(COALESCE(p.interval_seconds, 3600) "
        "AS TEXT) || ' seconds') >= julianday(w.available_until) "
        "THEN w.available_until "
        "ELSE strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '+' || "
        "CAST(COALESCE(p.interval_seconds, 3600) AS TEXT) || ' seconds') END "
        "FROM batches b "
        "LEFT JOIN snapshot_refresh_policies p ON p.product = b.product "
        "LEFT JOIN snapshot_availability_windows w ON w.batch_id = b.batch_id "
        "WHERE b.batch_id = ? "
        "ON CONFLICT(batch_id) DO UPDATE SET "
        "next_refresh_at = excluded.next_refresh_at;";
    const bool scheduled =
        sqlite3_prepare_v2(database, schedule_sql, -1, &schedule_statement, nullptr) ==
            SQLITE_OK &&
        bind_text(schedule_statement, 1, batch_id) &&
        sqlite3_step(schedule_statement) == SQLITE_DONE;
    if(!scheduled)
    {
        error = sqlite_error(database, "Schedule next Snapshot refresh failed");
        if(schedule_statement) sqlite3_finalize(schedule_statement);
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(schedule_statement);
    if(!execute_sql(database, "COMMIT;", error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_close(database);
    return true;
}

std::optional<VerificationStatus> SnapshotStore::verification_status(
    const std::string& batch_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(batch_id.empty()) return std::nullopt;

    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return std::nullopt;

    VerificationStatus status;
    bool found = false;
    const bool read = read_verification_status(
        database, batch_id, status, found);
    sqlite3_close(database);
    if(!read || !found) return std::nullopt;
    return status;
}

std::vector<RefreshPolicy> SnapshotStore::refresh_policies() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RefreshPolicy> policies;
    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return policies;

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT product, interval_seconds, updated_at "
        "FROM snapshot_refresh_policies ORDER BY product;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_close(database);
        return policies;
    }

    while(sqlite3_step(statement) == SQLITE_ROW)
    {
        RefreshPolicy policy;
        policy.product = column_text(statement, 0);
        policy.interval_seconds = sqlite3_column_int(statement, 1);
        policy.updated_at = column_text(statement, 2);
        policies.push_back(std::move(policy));
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return policies;
}

std::vector<AvailabilityWindow> SnapshotStore::availability_windows() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AvailabilityWindow> windows;
    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return windows;

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT batch_id, available_from, available_until, updated_at "
        "FROM snapshot_availability_windows ORDER BY batch_id;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_close(database);
        return windows;
    }
    while(sqlite3_step(statement) == SQLITE_ROW)
    {
        AvailabilityWindow window;
        window.batch_id = column_text(statement, 0);
        window.available_from = column_text(statement, 1);
        window.available_until = column_text(statement, 2);
        window.updated_at = column_text(statement, 3);
        windows.push_back(std::move(window));
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return windows;
}

std::optional<AvailabilityWindow> SnapshotStore::availability_window(
    const std::string& batch_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(batch_id.empty()) return std::nullopt;

    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return std::nullopt;

    AvailabilityWindow window;
    bool found = false;
    const bool read = read_availability_window(database, batch_id, window, found);
    sqlite3_close(database);
    if(!read || !found) return std::nullopt;
    return window;
}

bool SnapshotStore::save_refresh_settings(const std::string& product,
                                          int interval_seconds,
                                          const std::string& batch_id,
                                          const std::string& available_from,
                                          const std::string& available_until,
                                          std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    error.clear();
    if(product.empty() || batch_id.empty() || interval_seconds < 60)
    {
        error = "A product, batch, and refresh interval of at least 1 minute are required";
        return false;
    }

    sqlite3* database = nullptr;
    if(!open_database(database_path_,
                      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                          SQLITE_OPEN_FULLMUTEX,
                      database, error))
        return false;

    const auto normalized_from = normalized_utc_minute(database, available_from);
    const auto normalized_until = normalized_utc_minute(database, available_until);
    if(!normalized_from || !normalized_until ||
       *normalized_from >= *normalized_until)
    {
        error = "Availability end time must be later than its start time";
        sqlite3_close(database);
        return false;
    }

    if(!execute_sql(database, "BEGIN IMMEDIATE;", error))
    {
        sqlite3_close(database);
        return false;
    }

    sqlite3_stmt* policy_statement = nullptr;
    const char* policy_sql =
        "INSERT INTO snapshot_refresh_policies (product, interval_seconds) "
        "VALUES (?, ?) ON CONFLICT(product) DO UPDATE SET "
        "interval_seconds = excluded.interval_seconds, "
        "updated_at = CURRENT_TIMESTAMP;";
    const bool policy_saved =
        sqlite3_prepare_v2(database, policy_sql, -1, &policy_statement, nullptr) ==
            SQLITE_OK &&
        bind_text(policy_statement, 1, product) &&
        sqlite3_bind_int(policy_statement, 2, interval_seconds) == SQLITE_OK &&
        sqlite3_step(policy_statement) == SQLITE_DONE;
    if(!policy_saved)
    {
        error = sqlite_error(database, "Save Snapshot refresh policy failed");
        if(policy_statement) sqlite3_finalize(policy_statement);
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(policy_statement);

    sqlite3_stmt* window_statement = nullptr;
    const char* window_sql =
        "INSERT INTO snapshot_availability_windows "
        "(batch_id, available_from, available_until) VALUES (?, ?, ?) "
        "ON CONFLICT(batch_id) DO UPDATE SET "
        "available_from = excluded.available_from, "
        "available_until = excluded.available_until, "
        "updated_at = CURRENT_TIMESTAMP;";
    const bool window_saved =
        sqlite3_prepare_v2(database, window_sql, -1, &window_statement, nullptr) ==
            SQLITE_OK &&
        bind_text(window_statement, 1, batch_id) &&
        bind_text(window_statement, 2, *normalized_from) &&
        bind_text(window_statement, 3, *normalized_until) &&
        sqlite3_step(window_statement) == SQLITE_DONE;
    if(!window_saved)
    {
        error = sqlite_error(database, "Save Snapshot availability window failed");
        if(window_statement) sqlite3_finalize(window_statement);
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(window_statement);

    sqlite3_stmt* schedule_statement = nullptr;
    const char* schedule_sql =
        "INSERT INTO snapshot_refresh_schedule (batch_id, next_refresh_at) "
        "SELECT b.batch_id, CASE "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') < julianday(w.available_from) "
        "THEN w.available_from "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') >= julianday(w.available_until) THEN '' "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now', '+' || CAST(? AS TEXT) || ' seconds') >= "
        "julianday(w.available_until) THEN w.available_until "
        "ELSE strftime('%Y-%m-%dT%H:%M:%SZ', 'now', '+' || "
        "CAST(? AS TEXT) || ' seconds') END "
        "FROM batches b "
        "LEFT JOIN snapshot_availability_windows w ON w.batch_id = b.batch_id "
        "WHERE b.product = ? AND (b.batch_id = ? OR EXISTS ("
        "SELECT 1 FROM snapshot_storage r WHERE r.batch_id = b.batch_id "
        "AND r.state IN ('active', 'invalidated'))) "
        "ON CONFLICT(batch_id) DO UPDATE SET "
        "next_refresh_at = excluded.next_refresh_at;";
    const bool scheduled =
        sqlite3_prepare_v2(database, schedule_sql, -1, &schedule_statement, nullptr) ==
            SQLITE_OK &&
        sqlite3_bind_int(schedule_statement, 1, interval_seconds) == SQLITE_OK &&
        sqlite3_bind_int(schedule_statement, 2, interval_seconds) == SQLITE_OK &&
        bind_text(schedule_statement, 3, product) &&
        bind_text(schedule_statement, 4, batch_id) &&
        sqlite3_step(schedule_statement) == SQLITE_DONE;
    if(!scheduled)
    {
        error = sqlite_error(database, "Schedule product Snapshot refresh failed");
        if(schedule_statement) sqlite3_finalize(schedule_statement);
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_finalize(schedule_statement);

    if(!execute_sql(database, "COMMIT;", error))
    {
        execute_sql(database, "ROLLBACK;", error);
        sqlite3_close(database);
        return false;
    }
    sqlite3_close(database);
    return true;
}

bool SnapshotStore::refresh_due(const std::string& batch_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(batch_id.empty()) return false;

    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return true;

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT CASE WHEN w.batch_id IS NOT NULL AND ("
        "julianday('now') < julianday(w.available_from) OR "
        "julianday('now') >= julianday(w.available_until)) THEN 0 "
        "WHEN COALESCE(s.next_refresh_at, '') = '' "
        "OR julianday(s.next_refresh_at) IS NULL "
        "OR julianday(s.next_refresh_at) <= julianday('now') "
        "THEN 1 ELSE 0 END FROM batches b "
        "LEFT JOIN snapshot_refresh_schedule s ON s.batch_id = b.batch_id "
        "LEFT JOIN snapshot_availability_windows w ON w.batch_id = b.batch_id "
        "WHERE b.batch_id = ? LIMIT 1;";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK ||
       !bind_text(statement, 1, batch_id))
    {
        if(statement) sqlite3_finalize(statement);
        sqlite3_close(database);
        return true;
    }
    const int result = sqlite3_step(statement);
    const bool due = result == SQLITE_ROW && sqlite3_column_int(statement, 0) != 0;
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return result == SQLITE_ROW ? due : true;
}

std::optional<std::chrono::milliseconds> SnapshotStore::next_refresh_delay() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3* database = nullptr;
    std::string error;
    if(!open_database(database_path_, SQLITE_OPEN_READONLY |
                          SQLITE_OPEN_FULLMUTEX, database, error))
        return std::chrono::milliseconds(0);

    sqlite3_stmt* statement = nullptr;
    const char* sql =
        "SELECT MIN(CASE "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') >= julianday(w.available_until) THEN NULL "
        "WHEN w.batch_id IS NOT NULL AND "
        "julianday('now') < julianday(w.available_from) "
        "THEN MAX(0.0, (julianday(w.available_from) - "
        "julianday('now')) * 86400000.0) "
        "WHEN COALESCE(s.next_refresh_at, '') = '' "
        "OR julianday(s.next_refresh_at) IS NULL THEN 0.0 "
        "ELSE MAX(0.0, (julianday(s.next_refresh_at) - "
        "julianday('now')) * 86400000.0) END) "
        "FROM snapshot_storage r "
        "LEFT JOIN snapshot_refresh_schedule s ON s.batch_id = r.batch_id "
        "LEFT JOIN snapshot_availability_windows w ON w.batch_id = r.batch_id "
        "WHERE r.revision = (SELECT MAX(r2.revision) "
        "FROM snapshot_storage r2 WHERE r2.batch_id = r.batch_id) "
        "AND r.state IN ('active', 'invalidated');";
    if(sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        sqlite3_close(database);
        return std::chrono::milliseconds(0);
    }

    const int result = sqlite3_step(statement);
    if(result != SQLITE_ROW || sqlite3_column_type(statement, 0) == SQLITE_NULL)
    {
        sqlite3_finalize(statement);
        sqlite3_close(database);
        return std::nullopt;
    }

    const double delay_milliseconds = std::max(
        0.0, sqlite3_column_double(statement, 0));
    const auto rounded_delay = static_cast<long long>(
        std::ceil(delay_milliseconds));
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return std::chrono::milliseconds(rounded_delay);
}
}
