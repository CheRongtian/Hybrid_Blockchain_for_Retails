#include "db_utils.hpp"

#include <sqlite3.h>

#include <ctime>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace
{
constexpr int DATABASE_SCHEMA_VERSION = 7;
constexpr int OLDER_DATABASE_SCHEMA_VERSION = 3;
constexpr int PREVIOUS_DATABASE_SCHEMA_VERSION = 4;
constexpr int ROLE_POLICY_DATABASE_SCHEMA_VERSION = 5;
constexpr int ROUTE_DATABASE_SCHEMA_VERSION = 6;

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

bool has_column(sqlite3* db,
                const std::string& table,
                const std::string& column)
{
    const std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return false;

    bool found = false;
    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        if(column_text(statement, 1) == column)
        {
            found = true;
            break;
        }
    }
    sqlite3_finalize(statement);
    return found;
}

bool add_column_if_missing(sqlite3* db,
                           const char* table,
                           const char* column,
                           const char* definition)
{
    if(has_column(db, table, column)) return true;

    const std::string sql =
        std::string("ALTER TABLE ") + table + " ADD COLUMN " +
        column + " " + definition + ";";
    return execute_sql(db, sql.c_str(), "Extend database schema failed");
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
                       "PRAGMA user_version = 7;",
                       "Set database schema version failed");
}

bool migrate_confirmation_policies(sqlite3* db)
{
    if(has_column(db, "confirmation_policy", "role"))
    {
        return execute_sql(
            db,
            "INSERT OR IGNORE INTO confirmation_policy "
            "(role, typed_name, handwritten, face) VALUES "
            "('supplier', 1, 0, 0),"
            "('logistics', 1, 0, 0),"
            "('warehouse', 1, 0, 0),"
            "('supermarket', 1, 0, 0);",
            "Seed role confirmation policies failed");
    }

    if(!execute_sql(db, "BEGIN IMMEDIATE TRANSACTION;",
                    "Begin confirmation policy migration failed"))
        return false;

    const char* migration_sql =
        "ALTER TABLE confirmation_policy RENAME TO confirmation_policy_legacy;"
        "CREATE TABLE confirmation_policy ("
        "role TEXT PRIMARY KEY CHECK (role IN "
        "('supplier', 'logistics', 'warehouse', 'supermarket')),"
        "typed_name INTEGER NOT NULL CHECK (typed_name IN (0, 1)),"
        "handwritten INTEGER NOT NULL CHECK (handwritten IN (0, 1)),"
        "face INTEGER NOT NULL CHECK (face IN (0, 1)),"
        "updated_by_uid TEXT NOT NULL DEFAULT '',"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "INSERT INTO confirmation_policy "
        "(role, typed_name, handwritten, face, updated_by_uid, updated_at) "
        "SELECT 'supplier', typed_name, handwritten, face, updated_by_uid, updated_at "
        "FROM confirmation_policy_legacy WHERE id = 1 "
        "UNION ALL "
        "SELECT 'logistics', typed_name, handwritten, face, updated_by_uid, updated_at "
        "FROM confirmation_policy_legacy WHERE id = 1 "
        "UNION ALL "
        "SELECT 'warehouse', typed_name, handwritten, face, updated_by_uid, updated_at "
        "FROM confirmation_policy_legacy WHERE id = 1 "
        "UNION ALL "
        "SELECT 'supermarket', typed_name, handwritten, face, updated_by_uid, updated_at "
        "FROM confirmation_policy_legacy WHERE id = 1;"
        "DROP TABLE confirmation_policy_legacy;";

    if(!execute_sql(db, migration_sql, "Migrate confirmation policies failed") ||
       !execute_sql(db, "COMMIT;", "Commit confirmation policy migration failed"))
    {
        execute_sql(db, "ROLLBACK;", "Rollback confirmation policy migration failed");
        return false;
    }
    return true;
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
        "created_by_uid, current_stage, status, route_id"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
        bind_text(statement, 8, record.chain_status) &&
        bind_text(statement, 9, record.route_id);
    const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Insert batch failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    return succeeded;
}

bool update_batch_state(sqlite3* db, const SupplyChainRecord& record)
{
    const char* sql =
        "UPDATE batches SET current_stage = ?, status = ?, route_id = ? "
        "WHERE batch_id = ?;";
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
        bind_text(statement, 3, record.route_id) &&
        bind_text(statement, 4, record.batch_id);
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
       schema_version != OLDER_DATABASE_SCHEMA_VERSION &&
       schema_version != PREVIOUS_DATABASE_SCHEMA_VERSION &&
       schema_version != ROLE_POLICY_DATABASE_SCHEMA_VERSION &&
       schema_version != ROUTE_DATABASE_SCHEMA_VERSION &&
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
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "route_id TEXT NOT NULL DEFAULT ''"
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
        "confirmation_method TEXT NOT NULL DEFAULT 'none',"
        "confirmation_name TEXT NOT NULL DEFAULT '',"
        "signature_algorithm TEXT NOT NULL DEFAULT '',"
        "signature TEXT NOT NULL DEFAULT '',"
        "signature_public_key_hash TEXT NOT NULL DEFAULT '',"
        "signed_payload_hash TEXT NOT NULL DEFAULT '',"
        "signature_verified INTEGER NOT NULL DEFAULT 0 CHECK (signature_verified IN (0, 1)),"
        "route_id TEXT NOT NULL DEFAULT '',"
        "route_node_id TEXT NOT NULL DEFAULT '',"
        "route_step_index INTEGER NOT NULL DEFAULT -1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS users ("
        "uid TEXT PRIMARY KEY,"
        "username TEXT NOT NULL UNIQUE,"
        "password_salt TEXT NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "organization_id TEXT NOT NULL,"
        "display_name TEXT NOT NULL DEFAULT '',"
        "public_key TEXT NOT NULL DEFAULT '',"
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
        ");"
        "CREATE TABLE IF NOT EXISTS confirmation_policy ("
        "role TEXT PRIMARY KEY CHECK (role IN "
        "('supplier', 'logistics', 'warehouse', 'supermarket')),"
        "typed_name INTEGER NOT NULL DEFAULT 1 CHECK (typed_name IN (0, 1)),"
        "handwritten INTEGER NOT NULL DEFAULT 0 CHECK (handwritten IN (0, 1)),"
        "face INTEGER NOT NULL DEFAULT 0 CHECK (face IN (0, 1)),"
        "updated_by_uid TEXT NOT NULL DEFAULT '',"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS route_definitions ("
        "route_id TEXT PRIMARY KEY,"
        "batch_id TEXT NOT NULL DEFAULT '',"
        "name TEXT NOT NULL,"
        "is_default INTEGER NOT NULL DEFAULT 0 CHECK (is_default IN (0, 1)),"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");"
        "CREATE TABLE IF NOT EXISTS route_nodes ("
        "route_id TEXT NOT NULL,"
        "node_id TEXT NOT NULL,"
        "node_type TEXT NOT NULL,"
        "label TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "username TEXT NOT NULL DEFAULT '',"
        "position_x INTEGER NOT NULL DEFAULT 0,"
        "position_y INTEGER NOT NULL DEFAULT 0,"
        "step_index INTEGER NOT NULL DEFAULT -1,"
        "PRIMARY KEY (route_id, node_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS route_edges ("
        "route_id TEXT NOT NULL,"
        "from_node_id TEXT NOT NULL,"
        "to_node_id TEXT NOT NULL,"
        "PRIMARY KEY (route_id, from_node_id, to_node_id)"
        ");"
        "CREATE TABLE IF NOT EXISTS transport_batch_links ("
        "shipment_id TEXT NOT NULL,"
        "vehicle_container_id TEXT NOT NULL,"
        "batch_id TEXT NOT NULL,"
        "block_id INTEGER NOT NULL,"
        "PRIMARY KEY (shipment_id, batch_id, block_id)"
        ");";

    if(!execute_sql(db, schema_sql, "Create database schema failed") ||
       !add_column_if_missing(db, "users", "display_name",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "users", "public_key",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "confirmation_method",
                              "TEXT NOT NULL DEFAULT 'none'") ||
       !add_column_if_missing(db, "supply_chain_records", "confirmation_name",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "signature_algorithm",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "signature",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "signature_public_key_hash",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "signed_payload_hash",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "signature_verified",
                              "INTEGER NOT NULL DEFAULT 0") ||
       !add_column_if_missing(db, "batches", "route_id",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "route_id",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "route_node_id",
                              "TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "supply_chain_records", "route_step_index",
                              "INTEGER NOT NULL DEFAULT -1") ||
       !migrate_confirmation_policies(db) ||
       !execute_sql(db,
                    "UPDATE users SET display_name = username "
                    "WHERE display_name = '';",
                    "Backfill user display names failed") ||
       !write_schema_version(db))
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return ensure_default_workflow(db_path);
}

bool load_confirmation_policy(const std::string& db_path,
                              const std::string& role,
                              ConfirmationPolicy& policy)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "SELECT role, typed_name, handwritten, face, updated_by_uid, "
        "updated_at FROM confirmation_policy WHERE role = ? LIMIT 1;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare confirmation policy query failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    if(!bind_text(statement, 1, role) || sqlite3_step(statement) != SQLITE_ROW)
    {
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return false;
    }

    policy.role = column_text(statement, 0);
    policy.typed_name = sqlite3_column_int(statement, 1) != 0;
    policy.handwritten = sqlite3_column_int(statement, 2) != 0;
    policy.face = sqlite3_column_int(statement, 3) != 0;
    policy.updated_by_uid = column_text(statement, 4);
    policy.updated_at = column_text(statement, 5);
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return true;
}

bool load_confirmation_policies(
    const std::string& db_path,
    std::vector<ConfirmationPolicy>& policies)
{
    policies.clear();

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "SELECT role, typed_name, handwritten, face, updated_by_uid, updated_at "
        "FROM confirmation_policy ORDER BY CASE role "
        "WHEN 'supplier' THEN 0 WHEN 'logistics' THEN 1 "
        "WHEN 'warehouse' THEN 2 ELSE 3 END;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare confirmation policy list failed: "
                  << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        ConfirmationPolicy policy;
        policy.role = column_text(statement, 0);
        policy.typed_name = sqlite3_column_int(statement, 1) != 0;
        policy.handwritten = sqlite3_column_int(statement, 2) != 0;
        policy.face = sqlite3_column_int(statement, 3) != 0;
        policy.updated_by_uid = column_text(statement, 4);
        policy.updated_at = column_text(statement, 5);
        policies.push_back(std::move(policy));
    }
    const bool succeeded = result == SQLITE_DONE && policies.size() == 4;
    if(!succeeded)
        std::cerr << "Load confirmation policies failed: " << sqlite3_errmsg(db)
                  << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

bool save_confirmation_policies(
    const std::string& db_path,
    const std::vector<ConfirmationPolicy>& policies)
{
    if(policies.size() != 4) return false;
    for(const ConfirmationPolicy& policy : policies)
    {
        if(policy.role.empty() ||
           (!policy.typed_name && !policy.handwritten && !policy.face))
            return false;
    }

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }
    if(!execute_sql(db, "BEGIN IMMEDIATE TRANSACTION;",
                    "Begin confirmation policy update failed"))
    {
        sqlite3_close(db);
        return false;
    }

    const char* sql =
        "UPDATE confirmation_policy SET typed_name = ?, handwritten = ?, "
        "face = ?, updated_by_uid = ?, updated_at = CURRENT_TIMESTAMP "
        "WHERE role = ?;";
    for(const ConfirmationPolicy& policy : policies)
    {
        sqlite3_stmt* statement = nullptr;
        if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare confirmation policy update failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute_sql(db, "ROLLBACK;", "Rollback confirmation policy update failed");
            sqlite3_close(db);
            return false;
        }

        const bool bound =
            sqlite3_bind_int(statement, 1, policy.typed_name ? 1 : 0) == SQLITE_OK &&
            sqlite3_bind_int(statement, 2, policy.handwritten ? 1 : 0) == SQLITE_OK &&
            sqlite3_bind_int(statement, 3, policy.face ? 1 : 0) == SQLITE_OK &&
            bind_text(statement, 4, policy.updated_by_uid) &&
            bind_text(statement, 5, policy.role);
        const bool succeeded = bound && sqlite3_step(statement) == SQLITE_DONE &&
            sqlite3_changes(db) == 1;
        if(!succeeded)
            std::cerr << "Save confirmation policy failed: "
                      << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(statement);
        if(!succeeded)
        {
            execute_sql(db, "ROLLBACK;", "Rollback confirmation policy update failed");
            sqlite3_close(db);
            return false;
        }
    }

    const bool committed = execute_sql(
        db, "COMMIT;", "Commit confirmation policy update failed");
    if(!committed)
        execute_sql(db, "ROLLBACK;", "Rollback confirmation policy update failed");
    sqlite3_close(db);
    return committed;
}

bool save_user_public_key(const std::string& db_path,
                          const std::string& uid,
                          const std::string& public_key)
{
    if(uid.empty() || public_key.empty()) return false;

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* query = "SELECT public_key FROM users WHERE uid = ? LIMIT 1;";
    sqlite3_stmt* query_statement = nullptr;
    if(sqlite3_prepare_v2(db, query, -1, &query_statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare public-key query failed: " << sqlite3_errmsg(db)
                  << '\n';
        sqlite3_close(db);
        return false;
    }

    if(!bind_text(query_statement, 1, uid) ||
       sqlite3_step(query_statement) != SQLITE_ROW)
    {
        sqlite3_finalize(query_statement);
        sqlite3_close(db);
        return false;
    }

    const std::string stored_key = column_text(query_statement, 0);
    sqlite3_finalize(query_statement);
    if(!stored_key.empty())
    {
        sqlite3_close(db);
        return stored_key == public_key;
    }

    const char* update = "UPDATE users SET public_key = ? WHERE uid = ?;";
    sqlite3_stmt* update_statement = nullptr;
    if(sqlite3_prepare_v2(db, update, -1, &update_statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare public-key update failed: " << sqlite3_errmsg(db)
                  << '\n';
        sqlite3_close(db);
        return false;
    }

    const bool bound = bind_text(update_statement, 1, public_key) &&
        bind_text(update_statement, 2, uid);
    const bool succeeded = bound && sqlite3_step(update_statement) == SQLITE_DONE &&
        sqlite3_changes(db) == 1;
    if(!succeeded)
        std::cerr << "Save public key failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(update_statement);
    sqlite3_close(db);
    return succeeded;
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
        "u.role, u.organization_id, u.display_name, u.public_key, "
        "u.active, s.expires_at "
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
    account.display_name = column_text(statement, 6);
    account.public_key = column_text(statement, 7);
    account.active = sqlite3_column_int(statement, 8) != 0;
    const std::int64_t expires_at = sqlite3_column_int64(statement, 9);
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
        "uid, username, password_salt, password_hash, role, organization_id, "
        "display_name, public_key, active"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
        bind_text(statement, 7, account.display_name) &&
        bind_text(statement, 8, account.public_key) &&
        sqlite3_bind_int(statement, 9, account.active ? 1 : 0) == SQLITE_OK;
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
        "organization_id, display_name, public_key, active "
        "FROM users WHERE username = ? LIMIT 1;";
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
    account.display_name = column_text(statement, 6);
    account.public_key = column_text(statement, 7);
    account.active = sqlite3_column_int(statement, 8) != 0;
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
        "created_by_uid, current_stage, status, created_at, route_id "
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
    batch.route_id = column_text(statement, 9);
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
        "created_by_uid, current_stage, status, created_at, route_id "
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
            column_text(statement, 8),
            column_text(statement, 9),
            "",
            -1
        });
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read batches failed: " << sqlite3_errmsg(db) << '\n';
    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}

namespace
{
std::string workflow_route_id(const std::string& batch_id)
{
    return batch_id.empty() ? "route-default" : "route-" + batch_id;
}

std::vector<SupplyRouteNode> default_route_nodes()
{
    return {
        {"route-default", "supplier-1", "supplier", "Supplier", "supplier",
         "supplier01", 80, 150, 0},
        {"route-default", "transport-1", "transport", "Transport 1", "logistics",
         "logistics01", 300, 150, 1},
        {"route-default", "warehouse-1", "warehouse", "Warehouse", "warehouse",
         "warehouse01", 520, 150, 2},
        {"route-default", "supermarket-1", "supermarket", "Supermarket",
         "supermarket", "supermarket01", 740, 150, 3}
    };
}

std::vector<SupplyRouteEdge> default_route_edges()
{
    return {
        {"route-default", "supplier-1", "transport-1"},
        {"route-default", "transport-1", "warehouse-1"},
        {"route-default", "warehouse-1", "supermarket-1"}
    };
}

bool route_node_ids_are_unique(const std::vector<SupplyRouteNode>& nodes)
{
    std::unordered_set<std::string> ids;
    for(const SupplyRouteNode& node : nodes)
    {
        if(node.node_id.empty() || node.label.empty() || node.role.empty() ||
           !ids.insert(node.node_id).second)
            return false;
    }
    return !nodes.empty();
}
}

bool ensure_default_workflow(const std::string& db_path)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    if(!execute_sql(db, "BEGIN IMMEDIATE TRANSACTION;",
                    "Begin default workflow setup failed"))
    {
        sqlite3_close(db);
        return false;
    }

    const auto nodes = default_route_nodes();
    const auto edges = default_route_edges();
    const char* definition_sql =
        "INSERT OR IGNORE INTO route_definitions "
        "(route_id, batch_id, name, is_default) VALUES "
        "('route-default', '', 'Default supply route', 1);";
    bool succeeded = execute_sql(db, definition_sql,
                                 "Create default workflow failed");

    const char* node_sql =
        "INSERT OR IGNORE INTO route_nodes "
        "(route_id, node_id, node_type, label, role, username, position_x, "
        "position_y, step_index) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    for(const SupplyRouteNode& node : nodes)
    {
        sqlite3_stmt* statement = nullptr;
        if(!succeeded || sqlite3_prepare_v2(db, node_sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            succeeded = false;
            if(statement) sqlite3_finalize(statement);
            break;
        }
        const bool bound =
            bind_text(statement, 1, "route-default") &&
            bind_text(statement, 2, node.node_id) &&
            bind_text(statement, 3, node.node_type) &&
            bind_text(statement, 4, node.label) &&
            bind_text(statement, 5, node.role) &&
            bind_text(statement, 6, node.username) &&
            sqlite3_bind_int(statement, 7, node.position_x) == SQLITE_OK &&
            sqlite3_bind_int(statement, 8, node.position_y) == SQLITE_OK &&
            sqlite3_bind_int(statement, 9, node.step_index) == SQLITE_OK;
        succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if(!succeeded) break;
    }

    const char* edge_sql =
        "INSERT OR IGNORE INTO route_edges "
        "(route_id, from_node_id, to_node_id) VALUES (?, ?, ?);";
    for(const SupplyRouteEdge& edge : edges)
    {
        sqlite3_stmt* statement = nullptr;
        if(!succeeded || sqlite3_prepare_v2(db, edge_sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            succeeded = false;
            if(statement) sqlite3_finalize(statement);
            break;
        }
        const bool bound =
            bind_text(statement, 1, "route-default") &&
            bind_text(statement, 2, edge.from_node_id) &&
            bind_text(statement, 3, edge.to_node_id);
        succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if(!succeeded) break;
    }

    if(succeeded)
    {
        succeeded = execute_sql(
            db,
            "UPDATE batches SET route_id = 'route-default' WHERE route_id = '';",
            "Bind existing batches to default workflow failed");
    }

    if(!succeeded || !execute_sql(db, "COMMIT;",
                                  "Commit default workflow setup failed"))
    {
        execute_sql(db, "ROLLBACK;", "Rollback default workflow setup failed");
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

bool ensure_batch_workflow(const std::string& db_path,
                           const std::string& batch_id,
                           std::string& route_id)
{
    route_id.clear();
    if(batch_id.empty()) return false;

    const auto batch = find_supply_chain_batch(db_path, batch_id);
    if(!batch) return false;
    if(!batch->route_id.empty())
    {
        route_id = batch->route_id;
        return true;
    }

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        if(db) sqlite3_close(db);
        return false;
    }
    const char* sql = "UPDATE batches SET route_id = 'route-default' "
                      "WHERE batch_id = ? AND route_id = '';";
    sqlite3_stmt* statement = nullptr;
    const bool prepared = sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK;
    const bool succeeded = prepared && bind_text(statement, 1, batch_id) &&
        sqlite3_step(statement) == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Bind batch workflow failed: " << sqlite3_errmsg(db) << '\n';
    if(statement) sqlite3_finalize(statement);
    sqlite3_close(db);
    if(!succeeded) return false;
    route_id = "route-default";
    return true;
}

bool load_workflow_route(const std::string& db_path,
                         const std::string& batch_id,
                         std::string& route_id,
                         std::vector<SupplyRouteNode>& nodes,
                         std::vector<SupplyRouteEdge>& edges)
{
    route_id.clear();
    nodes.clear();
    edges.clear();

    if(!batch_id.empty() && !ensure_batch_workflow(db_path, batch_id, route_id))
        return false;
    if(batch_id.empty()) route_id = "route-default";

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        if(db) sqlite3_close(db);
        return false;
    }

    const char* node_sql =
        "SELECT node_id, node_type, label, role, username, position_x, "
        "position_y, step_index FROM route_nodes WHERE route_id = ? "
        "ORDER BY step_index ASC, position_x ASC, node_id ASC;";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, node_sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare workflow node query failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }
    bool succeeded = bind_text(statement, 1, route_id);
    int result = SQLITE_ROW;
    while(succeeded && (result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        nodes.push_back(SupplyRouteNode{
            route_id,
            column_text(statement, 0),
            column_text(statement, 1),
            column_text(statement, 2),
            column_text(statement, 3),
            column_text(statement, 4),
            sqlite3_column_int(statement, 5),
            sqlite3_column_int(statement, 6),
            sqlite3_column_int(statement, 7)
        });
    }
    succeeded = succeeded && result == SQLITE_DONE;
    sqlite3_finalize(statement);

    const char* edge_sql =
        "SELECT from_node_id, to_node_id FROM route_edges WHERE route_id = ? "
        "ORDER BY from_node_id ASC, to_node_id ASC;";
    if(succeeded && sqlite3_prepare_v2(db, edge_sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare workflow edge query failed: " << sqlite3_errmsg(db) << '\n';
        succeeded = false;
    }
    if(succeeded)
    {
        succeeded = bind_text(statement, 1, route_id);
        result = SQLITE_ROW;
        while(succeeded && (result = sqlite3_step(statement)) == SQLITE_ROW)
        {
            edges.push_back(SupplyRouteEdge{
                route_id,
                column_text(statement, 0),
                column_text(statement, 1)
            });
        }
        succeeded = succeeded && result == SQLITE_DONE;
        sqlite3_finalize(statement);
    }
    sqlite3_close(db);
    return succeeded && route_node_ids_are_unique(nodes);
}

bool save_workflow_route(const std::string& db_path,
                         const std::string& batch_id,
                         const std::vector<SupplyRouteNode>& nodes,
                         const std::vector<SupplyRouteEdge>& edges,
                         std::string& route_id,
                         std::string& error)
{
    error.clear();
    route_id = workflow_route_id(batch_id);
    if(!route_node_ids_are_unique(nodes))
    {
        error = "Workflow contains an invalid or duplicate node ID";
        return false;
    }

    std::unordered_set<std::string> node_ids;
    for(const SupplyRouteNode& node : nodes) node_ids.insert(node.node_id);
    for(const SupplyRouteEdge& edge : edges)
    {
        if(node_ids.count(edge.from_node_id) == 0 ||
           node_ids.count(edge.to_node_id) == 0 ||
           edge.from_node_id == edge.to_node_id)
        {
            error = "Workflow contains an edge for an unknown node";
            return false;
        }
    }

    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        error = "Cannot open the workflow database";
        if(db) sqlite3_close(db);
        return false;
    }
    bool succeeded = execute_sql(db, "BEGIN IMMEDIATE TRANSACTION;",
                                 "Begin workflow save failed");
    const char* definition_sql =
        "INSERT INTO route_definitions "
        "(route_id, batch_id, name, is_default) VALUES (?, ?, ?, ?) "
        "ON CONFLICT(route_id) DO UPDATE SET batch_id = excluded.batch_id, "
        "name = excluded.name, is_default = excluded.is_default, "
        "updated_at = CURRENT_TIMESTAMP;";
    sqlite3_stmt* statement = nullptr;
    if(succeeded && sqlite3_prepare_v2(db, definition_sql, -1, &statement, nullptr) == SQLITE_OK)
    {
        const bool bound =
            bind_text(statement, 1, route_id) &&
            bind_text(statement, 2, batch_id) &&
            bind_text(statement, 3, batch_id.empty() ? "Default supply route" :
                      "Batch " + batch_id + " route") &&
            sqlite3_bind_int(statement, 4, batch_id.empty() ? 1 : 0) == SQLITE_OK;
        succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
    }
    else if(succeeded)
    {
        succeeded = false;
    }

    if(succeeded)
    {
        sqlite3_stmt* delete_statement = nullptr;
        succeeded = sqlite3_prepare_v2(
            db, "DELETE FROM route_nodes WHERE route_id = ?;", -1,
            &delete_statement, nullptr) == SQLITE_OK &&
            bind_text(delete_statement, 1, route_id) &&
            sqlite3_step(delete_statement) == SQLITE_DONE;
        if(delete_statement) sqlite3_finalize(delete_statement);
    }
    if(succeeded)
    {
        sqlite3_stmt* delete_statement = nullptr;
        succeeded = sqlite3_prepare_v2(
            db, "DELETE FROM route_edges WHERE route_id = ?;", -1,
            &delete_statement, nullptr) == SQLITE_OK &&
            bind_text(delete_statement, 1, route_id) &&
            sqlite3_step(delete_statement) == SQLITE_DONE;
        if(delete_statement) sqlite3_finalize(delete_statement);
    }

    const char* node_sql =
        "INSERT INTO route_nodes (route_id, node_id, node_type, label, role, "
        "username, position_x, position_y, step_index) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
    for(const SupplyRouteNode& node : nodes)
    {
        if(!succeeded || sqlite3_prepare_v2(db, node_sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            succeeded = false;
            if(statement) sqlite3_finalize(statement);
            break;
        }
        const bool bound =
            bind_text(statement, 1, route_id) &&
            bind_text(statement, 2, node.node_id) &&
            bind_text(statement, 3, node.node_type) &&
            bind_text(statement, 4, node.label) &&
            bind_text(statement, 5, node.role) &&
            bind_text(statement, 6, node.username) &&
            sqlite3_bind_int(statement, 7, node.position_x) == SQLITE_OK &&
            sqlite3_bind_int(statement, 8, node.position_y) == SQLITE_OK &&
            sqlite3_bind_int(statement, 9, node.step_index) == SQLITE_OK;
        succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if(!succeeded) break;
    }

    const char* edge_sql =
        "INSERT INTO route_edges (route_id, from_node_id, to_node_id) VALUES (?, ?, ?);";
    for(const SupplyRouteEdge& edge : edges)
    {
        if(!succeeded || sqlite3_prepare_v2(db, edge_sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            succeeded = false;
            if(statement) sqlite3_finalize(statement);
            break;
        }
        const bool bound =
            bind_text(statement, 1, route_id) &&
            bind_text(statement, 2, edge.from_node_id) &&
            bind_text(statement, 3, edge.to_node_id);
        succeeded = bound && sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        if(!succeeded) break;
    }

    if(succeeded && !batch_id.empty())
    {
        sqlite3_stmt* batch_statement = nullptr;
        succeeded = sqlite3_prepare_v2(
            db, "UPDATE batches SET route_id = ? WHERE batch_id = ?;", -1,
            &batch_statement, nullptr) == SQLITE_OK &&
            bind_text(batch_statement, 1, route_id) &&
            bind_text(batch_statement, 2, batch_id) &&
            sqlite3_step(batch_statement) == SQLITE_DONE;
        if(batch_statement) sqlite3_finalize(batch_statement);
    }

    if(!succeeded || !execute_sql(db, "COMMIT;", "Commit workflow save failed"))
    {
        execute_sql(db, "ROLLBACK;", "Rollback workflow save failed");
        sqlite3_close(db);
        error = "Failed to save workflow to the database";
        return false;
    }
    sqlite3_close(db);
    return true;
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
        "chain_status, confirmation_method, confirmation_name, "
        "signature_algorithm, signature, signature_public_key_hash, "
        "signed_payload_hash, signature_verified, route_id, route_node_id, "
        "route_step_index"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
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
        bind_text(statement, 20, record.chain_status) &&
        bind_text(statement, 21, record.confirmation_method) &&
        bind_text(statement, 22, record.confirmation_name) &&
        bind_text(statement, 23, record.signature_algorithm) &&
        bind_text(statement, 24, record.signature) &&
        bind_text(statement, 25, record.signature_public_key_hash) &&
        bind_text(statement, 26, record.signed_payload_hash) &&
        sqlite3_bind_int(statement, 27, record.signature_verified ? 1 : 0) == SQLITE_OK &&
        bind_text(statement, 28, record.route_id) &&
        bind_text(statement, 29, record.route_node_id) &&
        sqlite3_bind_int(statement, 30, record.route_step_index) == SQLITE_OK;
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

    if(record.role == "logistics" &&
       !record.transport_shipment_id.empty() &&
       !record.transport_vehicle_container_id.empty())
    {
        const char* transport_sql =
            "INSERT OR IGNORE INTO transport_batch_links "
            "(shipment_id, vehicle_container_id, batch_id, block_id) "
            "VALUES (?, ?, ?, ?);";
        sqlite3_stmt* transport_statement = nullptr;
        if(sqlite3_prepare_v2(db, transport_sql, -1, &transport_statement, nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare transport batch link insert failed: "
                      << sqlite3_errmsg(db) << '\n';
            execute_sql(db, "ROLLBACK;", "Rollback record transaction failed");
            sqlite3_close(db);
            return false;
        }
        const bool transport_bound =
            bind_text(transport_statement, 1, record.transport_shipment_id) &&
            bind_text(transport_statement, 2, record.transport_vehicle_container_id) &&
            bind_text(transport_statement, 3, record.batch_id) &&
            sqlite3_bind_int(transport_statement, 4, record.block_id) == SQLITE_OK;
        const bool linked = transport_bound &&
            sqlite3_step(transport_statement) == SQLITE_DONE;
        if(!linked)
            std::cerr << "Insert transport batch link failed: "
                      << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(transport_statement);
        if(!linked)
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
        "chain_status, confirmation_method, confirmation_name, "
        "signature_algorithm, signature, signature_public_key_hash, "
        "signed_payload_hash, signature_verified, route_id, route_node_id, "
        "route_step_index, created_at "
        "FROM supply_chain_records "
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
        record.confirmation_method = column_text(statement, 20);
        record.confirmation_name = column_text(statement, 21);
        record.signature_algorithm = column_text(statement, 22);
        record.signature = column_text(statement, 23);
        record.signature_public_key_hash = column_text(statement, 24);
        record.signed_payload_hash = column_text(statement, 25);
        record.signature_verified = sqlite3_column_int(statement, 26) != 0;
        record.route_id = column_text(statement, 27);
        record.route_node_id = column_text(statement, 28);
        record.route_step_index = sqlite3_column_int(statement, 29);
        record.created_at = column_text(statement, 30);
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
