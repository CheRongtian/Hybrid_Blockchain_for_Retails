#include "db_utils.hpp"

#include <sqlite3.h>

#include <ctime>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace
{
constexpr int DATABASE_SCHEMA_VERSION = 4;
constexpr int PREVIOUS_DATABASE_SCHEMA_VERSION = 3;

std::string column_text(sqlite3_stmt* statement, int column)
{
    const unsigned char* value = sqlite3_column_text(statement, column);
    return value ? reinterpret_cast<const char*>(value) : "";
}

bool bind_text(sqlite3_stmt* statement, int index, const std::string& value)
{
    return sqlite3_bind_text(statement, index, value.c_str(), -1,
                             SQLITE_TRANSIENT) == SQLITE_OK;
}

bool execute_sql(sqlite3* db, const char* sql, const char* operation)
{
    char* error = nullptr;
    if(sqlite3_exec(db, sql, nullptr, nullptr, &error) == SQLITE_OK)
        return true;

    std::cerr << operation << ": " << (error ? error : "unknown SQLite error")
              << '\n';
    sqlite3_free(error);
    return false;
}

bool has_application_tables(sqlite3* db)
{
    const char* sql =
        "SELECT 1 FROM sqlite_master "
        "WHERE type = 'table' AND name NOT LIKE 'sqlite_%' LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        return true;

    const bool found = sqlite3_step(statement) == SQLITE_ROW;
    sqlite3_finalize(statement);
    return found;
}

bool read_schema_version(sqlite3* db, int& version)
{
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &statement, nullptr) !=
       SQLITE_OK)
        return false;

    const bool succeeded = sqlite3_step(statement) == SQLITE_ROW;
    if(succeeded) version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return succeeded;
}

bool write_schema_version(sqlite3* db)
{
    return execute_sql(db,
                       "PRAGMA user_version = 4;",
                       "Set database schema version failed");
}

bool load_record_attachments(sqlite3* db,
                             std::vector<SupplyChainRecord>& records)
{
    std::unordered_map<int, std::size_t> record_indexes;
    for(std::size_t index = 0; index < records.size(); ++index)
        record_indexes[records[index].block_id] = index;

    const char* sql =
        "SELECT block_id, category, cid, filename, content_type, size "
        "FROM record_attachments ORDER BY block_id ASC, cid ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare attachment query failed: " << sqlite3_errmsg(db)
                  << '\n';
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const int block_id = sqlite3_column_int(statement, 0);
        const auto record_index = record_indexes.find(block_id);
        if(record_index == record_indexes.end()) continue;

        records[record_index->second].ipfs_refs.push_back(IpfsReference{
            column_text(statement, 1),
            column_text(statement, 2),
            column_text(statement, 3),
            column_text(statement, 4),
            sqlite3_column_int64(statement, 5)
        });
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read attachments failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    return succeeded;
}

bool load_merkle_leaves(sqlite3* db,
                        std::vector<SupplyChainRecord>& records)
{
    std::unordered_map<int, std::size_t> record_indexes;
    for(std::size_t index = 0; index < records.size(); ++index)
        record_indexes[records[index].block_id] = index;

    const char* sql =
        "SELECT block_id, leaf_index, field_name, leaf_value, leaf_hash, "
        "proof, verified FROM block_merkle_leaves "
        "ORDER BY block_id ASC, leaf_index ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare Merkle leaf query failed: " << sqlite3_errmsg(db)
                  << '\n';
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        const int block_id = sqlite3_column_int(statement, 0);
        const auto record_index = record_indexes.find(block_id);
        if(record_index == record_indexes.end()) continue;

        SupplyChainRecord& record = records[record_index->second];
        record.merkle_fields.push_back(MerkleField{
            column_text(statement, 2),
            column_text(statement, 3)
        });
        record.merkle_leaves.push_back(MerkleLeafRecord{
            sqlite3_column_int(statement, 1),
            column_text(statement, 2),
            column_text(statement, 3),
            column_text(statement, 4),
            column_text(statement, 5),
            sqlite3_column_int(statement, 6) != 0
        });
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read Merkle leaves failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    return succeeded;
}

bool insert_batch_if_missing(sqlite3* db, const SupplyChainRecord& record)
{
    const char* sql =
        "INSERT OR IGNORE INTO batches ("
        "batch_id, product, harvest_date, farm_location, certificate_id, "
        "created_by_uid, current_stage, status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare batch insert failed: " << sqlite3_errmsg(db)
                  << '\n';
        return false;
    }

    const bool bound =
        bind_text(statement, 1, record.batch_id) &&
        bind_text(statement, 2, record.product) &&
        bind_text(statement, 3, record.batch_harvest_date) &&
        bind_text(statement, 4, record.batch_farm_location) &&
        bind_text(statement, 5, record.certificate_id) &&
        bind_text(statement, 6, record.uid) &&
        bind_text(statement, 7, record.stage) &&
        bind_text(statement, 8, record.chain_status);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Insert batch failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    return succeeded;
}

bool update_batch_state(sqlite3* db, const SupplyChainRecord& record)
{
    const char* sql =
        "UPDATE batches SET current_stage = ?, status = ? WHERE batch_id = ?;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare batch update failed: " << sqlite3_errmsg(db)
                  << '\n';
        return false;
    }

    const bool bound =
        bind_text(statement, 1, record.stage) &&
        bind_text(statement, 2, record.chain_status) &&
        bind_text(statement, 3, record.batch_id);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Update batch failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    return succeeded;
}
}

bool init_database(const std::string& db_path)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    int schema_version = 0;
    if(!read_schema_version(db, schema_version))
    {
        std::cerr << "Cannot read database schema version\n";
        sqlite3_close(db);
        return false;
    }

    if(schema_version != 0 &&
       schema_version != PREVIOUS_DATABASE_SCHEMA_VERSION &&
       schema_version != DATABASE_SCHEMA_VERSION)
    {
        std::cerr << "Unsupported database schema version: " << schema_version
                  << ". Expected " << DATABASE_SCHEMA_VERSION << '\n';
        sqlite3_close(db);
        return false;
    }

    if(schema_version == 0 && has_application_tables(db))
    {
        std::cerr << "Existing database uses a legacy schema. Remove the local "
                  << "database before starting the redesigned server.\n";
        sqlite3_close(db);
        return false;
    }

    const char* schema_sql =
        "CREATE TABLE IF NOT EXISTS batches ("
        "batch_id TEXT PRIMARY KEY,"
        "product TEXT NOT NULL,"
        "harvest_date TEXT NOT NULL,"
        "farm_location TEXT NOT NULL,"
        "certificate_id TEXT NOT NULL DEFAULT '',"
        "created_by_uid TEXT NOT NULL,"
        "current_stage TEXT NOT NULL,"
        "status TEXT NOT NULL DEFAULT 'in_progress',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS supply_chain_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "block_id INTEGER NOT NULL UNIQUE,"
        "parent_block_id INTEGER NOT NULL,"
        "parent_block_hash TEXT NOT NULL,"
        "batch_id TEXT NOT NULL,"
        "product TEXT NOT NULL,"
        "location_summary TEXT NOT NULL DEFAULT '',"
        "batch_harvest_date TEXT NOT NULL DEFAULT '',"
        "batch_farm_location TEXT NOT NULL DEFAULT '',"
        "certificate_id TEXT NOT NULL DEFAULT '',"
        "stage TEXT NOT NULL,"
        "confirmed_by TEXT NOT NULL,"
        "uid TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "organization_id TEXT NOT NULL,"
        "event_data TEXT NOT NULL DEFAULT '',"
        "canonical_record TEXT NOT NULL,"
        "root_hash TEXT NOT NULL,"
        "verified INTEGER NOT NULL CHECK (verified IN (0, 1)),"
        "block_hash TEXT NOT NULL,"
        "chain_status TEXT NOT NULL DEFAULT 'in_progress',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS users ("
        "uid TEXT PRIMARY KEY,"
        "username TEXT NOT NULL UNIQUE,"
        "password_salt TEXT NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "organization_id TEXT NOT NULL,"
        "active INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS block_edges ("
        "from_block_id INTEGER NOT NULL,"
        "to_block_id INTEGER NOT NULL,"
        "batch_id TEXT NOT NULL,"
        "relation TEXT NOT NULL DEFAULT 'continues',"
        "PRIMARY KEY (from_block_id, to_block_id, relation)"
        ");"
        "CREATE TABLE IF NOT EXISTS record_attachments ("
        "block_id INTEGER NOT NULL,"
        "category TEXT NOT NULL,"
        "cid TEXT NOT NULL,"
        "filename TEXT NOT NULL DEFAULT '',"
        "content_type TEXT NOT NULL DEFAULT 'application/octet-stream',"
        "size INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY (block_id, cid)"
        ");"
        "CREATE TABLE IF NOT EXISTS block_merkle_leaves ("
        "block_id INTEGER NOT NULL,"
        "leaf_index INTEGER NOT NULL,"
        "field_name TEXT NOT NULL,"
        "leaf_value TEXT NOT NULL,"
        "leaf_hash TEXT NOT NULL,"
        "proof TEXT NOT NULL,"
        "verified INTEGER NOT NULL CHECK (verified IN (0, 1)),"
        "PRIMARY KEY (block_id, leaf_index)"
        ");"
        "CREATE TABLE IF NOT EXISTS auth_sessions ("
        "token_hash TEXT PRIMARY KEY,"
        "uid TEXT NOT NULL,"
        "expires_at INTEGER NOT NULL,"
        "created_at INTEGER NOT NULL"
        ");";

    if(!execute_sql(db, schema_sql, "Create database schema failed") ||
       !write_schema_version(db))
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

bool create_persistent_auth_session(const std::string& db_path,
                                    const std::string& token_hash,
                                    const std::string& uid,
                                    std::int64_t expires_at)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "INSERT INTO auth_sessions (token_hash, uid, expires_at, created_at) "
        "VALUES (?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare auth session insert failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    const std::int64_t created_at =
        static_cast<std::int64_t>(std::time(nullptr));
    const bool bound =
        bind_text(statement, 1, token_hash) &&
        bind_text(statement, 2, uid) &&
        sqlite3_bind_int64(statement, 3, expires_at) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 4, created_at) == SQLITE_OK;
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Insert auth session failed: " << sqlite3_errmsg(db)
                  << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

std::optional<PersistentAuthSession> find_persistent_auth_session(
    const std::string& db_path,
    const std::string& token_hash,
    std::int64_t now)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return std::nullopt;
    }

    const char* sql =
        "SELECT u.uid, u.username, u.password_salt, u.password_hash, "
        "u.role, u.organization_id, u.active, s.expires_at "
        "FROM auth_sessions s JOIN users u ON u.uid = s.uid "
        "WHERE s.token_hash = ? AND s.expires_at > ? AND u.active = 1 "
        "LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare auth session query failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return std::nullopt;
    }

    const bool bound =
        bind_text(statement, 1, token_hash) &&
        sqlite3_bind_int64(statement, 2, now) == SQLITE_OK;
    if(!bound || sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return std::nullopt;
    }

    UserAccount account;
    account.uid = column_text(statement, 0);
    account.username = column_text(statement, 1);
    account.password_salt = column_text(statement, 2);
    account.password_hash = column_text(statement, 3);
    account.role = column_text(statement, 4);
    account.organization_id = column_text(statement, 5);
    account.active = sqlite3_column_int(statement, 6) != 0;
    const std::int64_t expires_at = sqlite3_column_int64(statement, 7);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return PersistentAuthSession{std::move(account), expires_at};
}

bool delete_persistent_auth_session(const std::string& db_path,
                                    const std::string& token_hash,
                                    bool& deleted)
{
    deleted = false;
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql = "DELETE FROM auth_sessions WHERE token_hash = ?;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare auth session deletion failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    const bool bound = bind_text(statement, 1, token_hash);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(succeeded) deleted = sqlite3_changes(db) > 0;
    if(!succeeded)
        std::cerr << "Delete auth session failed: " << sqlite3_errmsg(db)
                  << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

bool delete_expired_auth_sessions(const std::string& db_path,
                                  std::int64_t now)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql = "DELETE FROM auth_sessions WHERE expires_at <= ?;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare expired auth session cleanup failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    const bool bound = sqlite3_bind_int64(statement, 1, now) == SQLITE_OK;
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Cleanup expired auth sessions failed: "
                  << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

bool insert_user_account(const std::string& db_path,
                         const UserAccount& account)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "INSERT OR IGNORE INTO users ("
        "uid, username, password_salt, password_hash, role, organization_id, active"
        ") VALUES (?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare user insert failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    const bool bound =
        bind_text(statement, 1, account.uid) &&
        bind_text(statement, 2, account.username) &&
        bind_text(statement, 3, account.password_salt) &&
        bind_text(statement, 4, account.password_hash) &&
        bind_text(statement, 5, account.role) &&
        bind_text(statement, 6, account.organization_id) &&
        sqlite3_bind_int(statement, 7, account.active ? 1 : 0) == SQLITE_OK;
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Insert user failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

std::optional<UserAccount> find_user_account(const std::string& db_path,
                                             const std::string& username)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return std::nullopt;
    }

    const char* sql =
        "SELECT uid, username, password_salt, password_hash, role, "
        "organization_id, active FROM users WHERE username = ? LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare user query failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return std::nullopt;
    }

    if(!bind_text(statement, 1, username) || sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return std::nullopt;
    }

    UserAccount account;
    account.uid = column_text(statement, 0);
    account.username = column_text(statement, 1);
    account.password_salt = column_text(statement, 2);
    account.password_hash = column_text(statement, 3);
    account.role = column_text(statement, 4);
    account.organization_id = column_text(statement, 5);
    account.active = sqlite3_column_int(statement, 6) != 0;
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return account;
}

std::optional<SupplyChainBatch> find_supply_chain_batch(
    const std::string& db_path,
    const std::string& batch_id)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return std::nullopt;
    }

    const char* sql =
        "SELECT batch_id, product, harvest_date, farm_location, certificate_id, "
        "created_by_uid, current_stage, status, created_at "
        "FROM batches WHERE batch_id = ? LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare batch query failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return std::nullopt;
    }

    if(!bind_text(statement, 1, batch_id) || sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return std::nullopt;
    }

    SupplyChainBatch batch;
    batch.batch_id = column_text(statement, 0);
    batch.product = column_text(statement, 1);
    batch.harvest_date = column_text(statement, 2);
    batch.farm_location = column_text(statement, 3);
    batch.certificate_id = column_text(statement, 4);
    batch.created_by_uid = column_text(statement, 5);
    batch.current_stage = column_text(statement, 6);
    batch.status = column_text(statement, 7);
    batch.created_at = column_text(statement, 8);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return batch;
}

bool load_supply_chain_batches(const std::string& db_path,
                               std::vector<SupplyChainBatch>& batches)
{
    batches.clear();
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "SELECT batch_id, product, harvest_date, farm_location, certificate_id, "
        "created_by_uid, current_stage, status, created_at "
        "FROM batches ORDER BY created_at ASC, batch_id ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare batch list failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        batches.push_back(SupplyChainBatch{
            column_text(statement, 0),
            column_text(statement, 1),
            column_text(statement, 2),
            column_text(statement, 3),
            column_text(statement, 4),
            column_text(statement, 5),
            column_text(statement, 6),
            column_text(statement, 7),
            column_text(statement, 8)
        });
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read batches failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

bool insert_supply_chain_record(const std::string& db_path,
                                const SupplyChainRecord& record)
{
    return insert_supply_chain_block(db_path, record, {});
}

bool insert_supply_chain_block(const std::string& db_path,
                               const SupplyChainRecord& record,
                               const std::vector<BlockEdge>& edges)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    if(!execute_sql(db, "BEGIN IMMEDIATE TRANSACTION;",
                    "Begin record transaction failed") ||
       !insert_batch_if_missing(db, record))
    {
        execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
        sqlite3_close(db);
        return false;
    }

    const char* record_sql =
        "INSERT INTO supply_chain_records ("
        "block_id, parent_block_id, parent_block_hash, batch_id, product, "
        "location_summary, batch_harvest_date, batch_farm_location, "
        "certificate_id, stage, confirmed_by, uid, role, organization_id, "
        "event_data, canonical_record, root_hash, verified, block_hash, "
        "chain_status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, record_sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare record insert failed: " << sqlite3_errmsg(db)
                  << '\n';
        execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
        sqlite3_close(db);
        return false;
    }

    const bool bound =
        sqlite3_bind_int(statement, 1, record.block_id) == SQLITE_OK &&
        sqlite3_bind_int(statement, 2, record.parent_block_id) == SQLITE_OK &&
        bind_text(statement, 3, record.parent_block_hash) &&
        bind_text(statement, 4, record.batch_id) &&
        bind_text(statement, 5, record.product) &&
        bind_text(statement, 6, record.location_summary) &&
        bind_text(statement, 7, record.batch_harvest_date) &&
        bind_text(statement, 8, record.batch_farm_location) &&
        bind_text(statement, 9, record.certificate_id) &&
        bind_text(statement, 10, record.stage) &&
        bind_text(statement, 11, record.confirmed_by) &&
        bind_text(statement, 12, record.uid) &&
        bind_text(statement, 13, record.role) &&
        bind_text(statement, 14, record.organization_id) &&
        bind_text(statement, 15, record.event_data) &&
        bind_text(statement, 16, record.canonical_record) &&
        bind_text(statement, 17, record.root_hash) &&
        sqlite3_bind_int(statement, 18, record.verified ? 1 : 0) == SQLITE_OK &&
        bind_text(statement, 19, record.block_hash) &&
        bind_text(statement, 20, record.chain_status);
    const bool inserted = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!inserted)
        std::cerr << "Insert record failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    if(!inserted)
    {
        execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
        sqlite3_close(db);
        return false;
    }

    const char* leaf_sql =
        "INSERT INTO block_merkle_leaves ("
        "block_id, leaf_index, field_name, leaf_value, leaf_hash, proof, verified"
        ") VALUES (?, ?, ?, ?, ?, ?, ?);";
    for(const MerkleLeafRecord& leaf : record.merkle_leaves)
    {
        sqlite3_stmt* leaf_statement = nullptr;
        if(sqlite3_prepare_v2(db, leaf_sql, -1, &leaf_statement, nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare Merkle leaf insert failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }

        const bool leaf_bound =
            sqlite3_bind_int(leaf_statement, 1, record.block_id) == SQLITE_OK &&
            sqlite3_bind_int(leaf_statement, 2, leaf.leaf_index) == SQLITE_OK &&
            bind_text(leaf_statement, 3, leaf.field_name) &&
            bind_text(leaf_statement, 4, leaf.leaf_value) &&
            bind_text(leaf_statement, 5, leaf.leaf_hash) &&
            bind_text(leaf_statement, 6, leaf.proof) &&
            sqlite3_bind_int(leaf_statement, 7, leaf.verified ? 1 : 0) == SQLITE_OK;
        const bool inserted_leaf = leaf_bound &&
            sqlite3_step(leaf_statement) == SQLITE_DONE;
        if(!inserted_leaf)
            std::cerr << "Insert Merkle leaf failed: " << sqlite3_errmsg(db)
                      << '\n';
        sqlite3_finalize(leaf_statement);
        if(!inserted_leaf)
        {
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }
    }

    const char* attachment_sql =
        "INSERT INTO record_attachments ("
        "block_id, category, cid, filename, content_type, size"
        ") VALUES (?, ?, ?, ?, ?, ?);";
    for(const IpfsReference& reference : record.ipfs_refs)
    {
        sqlite3_stmt* attachment_statement = nullptr;
        if(sqlite3_prepare_v2(db, attachment_sql, -1, &attachment_statement,
                              nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare attachment insert failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }

        const bool attachment_bound =
            sqlite3_bind_int(attachment_statement, 1, record.block_id) == SQLITE_OK &&
            bind_text(attachment_statement, 2, reference.category) &&
            bind_text(attachment_statement, 3, reference.cid) &&
            bind_text(attachment_statement, 4, reference.filename) &&
            bind_text(attachment_statement, 5, reference.content_type) &&
            sqlite3_bind_int64(attachment_statement, 6, reference.size) == SQLITE_OK;
        const bool inserted_attachment = attachment_bound &&
            sqlite3_step(attachment_statement) == SQLITE_DONE;
        if(!inserted_attachment)
            std::cerr << "Insert attachment failed: " << sqlite3_errmsg(db)
                      << '\n';
        sqlite3_finalize(attachment_statement);
        if(!inserted_attachment)
        {
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }
    }

    const char* edge_sql =
        "INSERT INTO block_edges (from_block_id, to_block_id, batch_id, relation) "
        "VALUES (?, ?, ?, ?);";
    for(const BlockEdge& edge : edges)
    {
        sqlite3_stmt* edge_statement = nullptr;
        if(sqlite3_prepare_v2(db, edge_sql, -1, &edge_statement, nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare block edge insert failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }

        const bool edge_bound =
            sqlite3_bind_int(edge_statement, 1, edge.from_block_id) == SQLITE_OK &&
            sqlite3_bind_int(edge_statement, 2, edge.to_block_id) == SQLITE_OK &&
            bind_text(edge_statement, 3, edge.batch_id) &&
            bind_text(edge_statement, 4, edge.relation);
        const bool inserted_edge = edge_bound &&
            sqlite3_step(edge_statement) == SQLITE_DONE;
        if(!inserted_edge)
            std::cerr << "Insert block edge failed: " << sqlite3_errmsg(db)
                      << '\n';
        sqlite3_finalize(edge_statement);
        if(!inserted_edge)
        {
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }
    }

    if(!update_batch_state(db, record) ||
       !execute_sql(db, "COMMIT;", "Commit record transaction failed"))
    {
        execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
}

bool load_supply_chain_records(const std::string& db_path,
                               std::vector<SupplyChainRecord>& records)
{
    records.clear();
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "SELECT block_id, parent_block_id, parent_block_hash, batch_id, product, "
        "location_summary, batch_harvest_date, batch_farm_location, "
        "certificate_id, stage, confirmed_by, uid, role, organization_id, "
        "event_data, canonical_record, root_hash, verified, block_hash, "
        "chain_status, created_at FROM supply_chain_records "
        "ORDER BY block_id ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare record query failed: " << sqlite3_errmsg(db)
                  << '\n';
        sqlite3_close(db);
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        SupplyChainRecord record;
        record.block_id = sqlite3_column_int(statement, 0);
        record.parent_block_id = sqlite3_column_int(statement, 1);
        record.parent_block_hash = column_text(statement, 2);
        record.batch_id = column_text(statement, 3);
        record.product = column_text(statement, 4);
        record.location_summary = column_text(statement, 5);
        record.batch_harvest_date = column_text(statement, 6);
        record.batch_farm_location = column_text(statement, 7);
        record.certificate_id = column_text(statement, 8);
        record.stage = column_text(statement, 9);
        record.confirmed_by = column_text(statement, 10);
        record.uid = column_text(statement, 11);
        record.role = column_text(statement, 12);
        record.organization_id = column_text(statement, 13);
        record.event_data = column_text(statement, 14);
        record.canonical_record = column_text(statement, 15);
        record.root_hash = column_text(statement, 16);
        record.verified = sqlite3_column_int(statement, 17) != 0;
        record.block_hash = column_text(statement, 18);
        record.chain_status = column_text(statement, 19);
        record.created_at = column_text(statement, 20);
        records.push_back(std::move(record));
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read records failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);

    if(succeeded && !load_record_attachments(db, records))
    {
        sqlite3_close(db);
        return false;
    }
    if(succeeded && !load_merkle_leaves(db, records))
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return succeeded;
}

bool load_block_edges(const std::string& db_path,
                      std::vector<BlockEdge>& edges)
{
    edges.clear();
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "SELECT from_block_id, to_block_id, batch_id, relation FROM block_edges "
        "ORDER BY to_block_id ASC, from_block_id ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare edge query failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        edges.push_back(BlockEdge{
            sqlite3_column_int(statement, 0),
            sqlite3_column_int(statement, 1),
            column_text(statement, 2),
            column_text(statement, 3)
        });
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read block edges failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}
