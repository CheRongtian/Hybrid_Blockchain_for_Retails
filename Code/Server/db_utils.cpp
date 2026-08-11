#include "db_utils.hpp"
#include <sqlite3.h>
#include <iostream>
#include <utility>

namespace
{
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

bool add_column_if_missing(sqlite3* db, const char* column_definition)
{
    const std::string sql =
        "ALTER TABLE supply_chain_records ADD COLUMN " +
        std::string(column_definition) + ";";
    char* error = nullptr;
    const int result = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error);
    if(result == SQLITE_OK)
        return true;

    const std::string message = error ? error : "";
    sqlite3_free(error);
    if(message.find("duplicate column name") != std::string::npos)
        return true;

    std::cerr << "Add database column failed: " << message << '\n';
    return false;
}

bool column_exists(sqlite3* db, const char* table_name, const char* column_name)
{
    const std::string sql = "PRAGMA table_info(" + std::string(table_name) + ");";
    sqlite3_stmt* statement = nullptr;
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK)
        return false;

    bool found = false;
    while(sqlite3_step(statement) == SQLITE_ROW)
    {
        if(column_text(statement, 1) == column_name)
        {
            found = true;
            break;
        }
    }

    sqlite3_finalize(statement);
    return found;
}

bool drop_legacy_destination_column(sqlite3* db)
{
    if(!column_exists(db, "supply_chain_records", "destination"))
        return true;

    char* error = nullptr;
    const int result = sqlite3_exec(
        db,
        "ALTER TABLE supply_chain_records DROP COLUMN destination;",
        nullptr,
        nullptr,
        &error);
    if(result == SQLITE_OK)
        return true;

    std::cerr << "Remove legacy destination column failed: "
              << (error ? error : "") << '\n';
    sqlite3_free(error);
    return false;
}
}

bool init_database(const std::string& db_path)
{
    sqlite3* db = nullptr;
    char* err_msg = nullptr;

    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS supply_chain_records ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "block_id INTEGER NOT NULL UNIQUE,"
        "batch_id TEXT NOT NULL,"
        "product TEXT NOT NULL,"
        "origin TEXT NOT NULL,"
        "stage TEXT NOT NULL,"
        "confirmed_by TEXT NOT NULL,"
        "uid TEXT NOT NULL DEFAULT '',"
        "role TEXT NOT NULL DEFAULT '',"
        "organization_id TEXT NOT NULL DEFAULT '',"
        "canonical_record TEXT NOT NULL,"
        "root_hash TEXT NOT NULL,"
        "proof TEXT NOT NULL,"
        "verified INTEGER NOT NULL CHECK (verified IN (0, 1)),"
        "block_hash TEXT NOT NULL DEFAULT '',"
        "chain_status TEXT NOT NULL DEFAULT 'in_progress',"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";

    if(sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        std::cerr << "Create table failed: " << err_msg << '\n';
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }

    const char* users_sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "uid TEXT PRIMARY KEY,"
        "username TEXT NOT NULL UNIQUE,"
        "password_salt TEXT NOT NULL,"
        "password_hash TEXT NOT NULL,"
        "role TEXT NOT NULL,"
        "organization_id TEXT NOT NULL,"
        "active INTEGER NOT NULL DEFAULT 1,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";

    if(sqlite3_exec(db, users_sql, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        std::cerr << "Create users table failed: " << err_msg << '\n';
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }

    const char* edges_sql =
        "CREATE TABLE IF NOT EXISTS block_edges ("
        "from_block_id INTEGER NOT NULL,"
        "to_block_id INTEGER NOT NULL,"
        "batch_id TEXT NOT NULL,"
        "relation TEXT NOT NULL DEFAULT 'continues',"
        "PRIMARY KEY (from_block_id, to_block_id, relation)"
        ");";

    if(sqlite3_exec(db, edges_sql, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        std::cerr << "Create block_edges table failed: " << err_msg << '\n';
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }

    if(!add_column_if_missing(db, "uid TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "role TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "organization_id TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "block_hash TEXT NOT NULL DEFAULT ''") ||
       !add_column_if_missing(db, "chain_status TEXT NOT NULL DEFAULT 'in_progress'"))
    {
        sqlite3_close(db);
        return false;
    }

    if(!drop_legacy_destination_column(db))
    {
        sqlite3_close(db);
        return false;
    }

    sqlite3_close(db);
    return true;
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

    char* error = nullptr;
    if(sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &error) != SQLITE_OK)
    {
        std::cerr << "Begin record transaction failed: " << (error ? error : "") << '\n';
        sqlite3_free(error);
        sqlite3_close(db);
        return false;
    }

    const char* sql =
        "INSERT INTO supply_chain_records ("
        "block_id, batch_id, product, origin, stage, confirmed_by, "
        "uid, role, organization_id, canonical_record, root_hash, proof, "
        "verified, block_hash, chain_status"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;

    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare insert failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return false;
    }

    const bool bound =
        sqlite3_bind_int(statement, 1, record.block_id) == SQLITE_OK &&
        bind_text(statement, 2, record.batch_id) &&
        bind_text(statement, 3, record.product) &&
        bind_text(statement, 4, record.origin) &&
        bind_text(statement, 5, record.stage) &&
        bind_text(statement, 6, record.confirmed_by) &&
        bind_text(statement, 7, record.uid) &&
        bind_text(statement, 8, record.role) &&
        bind_text(statement, 9, record.organization_id) &&
        bind_text(statement, 10, record.canonical_record) &&
        bind_text(statement, 11, record.root_hash) &&
        bind_text(statement, 12, record.proof) &&
        sqlite3_bind_int(statement, 13, record.verified ? 1 : 0) == SQLITE_OK &&
        bind_text(statement, 14, record.block_hash) &&
        bind_text(statement, 15, record.chain_status);

    if(!bound || sqlite3_step(statement) != SQLITE_DONE)
    {
        std::cerr << "Insert record failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(statement);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(statement);

    const char* edge_sql =
        "INSERT INTO block_edges (from_block_id, to_block_id, batch_id, relation) "
        "VALUES (?, ?, ?, ?);";
    for(const BlockEdge& edge : edges)
    {
        sqlite3_stmt* edge_statement = nullptr;
        if(sqlite3_prepare_v2(db, edge_sql, -1, &edge_statement, nullptr) != SQLITE_OK)
        {
            std::cerr << "Prepare block edge insert failed: " << sqlite3_errmsg(db) << '\n';
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            return false;
        }

        const bool edge_bound =
            sqlite3_bind_int(edge_statement, 1, edge.from_block_id) == SQLITE_OK &&
            sqlite3_bind_int(edge_statement, 2, edge.to_block_id) == SQLITE_OK &&
            bind_text(edge_statement, 3, edge.batch_id) &&
            bind_text(edge_statement, 4, edge.relation);
        if(!edge_bound || sqlite3_step(edge_statement) != SQLITE_DONE)
        {
            std::cerr << "Insert block edge failed: " << sqlite3_errmsg(db) << '\n';
            sqlite3_finalize(edge_statement);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            sqlite3_close(db);
            return false;
        }
        sqlite3_finalize(edge_statement);
    }

    if(sqlite3_exec(db, "COMMIT;", nullptr, nullptr, &error) != SQLITE_OK)
    {
        std::cerr << "Commit record transaction failed: " << (error ? error : "") << '\n';
        sqlite3_free(error);
        sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
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
        "SELECT block_id, batch_id, product, origin, stage, confirmed_by, "
        "uid, role, organization_id, canonical_record, root_hash, proof, "
        "verified, block_hash, chain_status, created_at "
        "FROM supply_chain_records ORDER BY block_id ASC;";
    sqlite3_stmt* statement = nullptr;

    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare query failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_close(db);
        return false;
    }

    int result = SQLITE_ROW;
    while((result = sqlite3_step(statement)) == SQLITE_ROW)
    {
        SupplyChainRecord record;
        record.block_id = sqlite3_column_int(statement, 0);
        record.batch_id = column_text(statement, 1);
        record.product = column_text(statement, 2);
        record.origin = column_text(statement, 3);
        record.stage = column_text(statement, 4);
        record.confirmed_by = column_text(statement, 5);
        record.uid = column_text(statement, 6);
        record.role = column_text(statement, 7);
        record.organization_id = column_text(statement, 8);
        record.canonical_record = column_text(statement, 9);
        record.root_hash = column_text(statement, 10);
        record.proof = column_text(statement, 11);
        record.verified = sqlite3_column_int(statement, 12) != 0;
        record.block_hash = column_text(statement, 13);
        record.chain_status = column_text(statement, 14);
        record.created_at = column_text(statement, 15);
        records.push_back(std::move(record));
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read records failed: " << sqlite3_errmsg(db) << '\n';

    sqlite3_finalize(statement);
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
        "SELECT from_block_id, to_block_id, batch_id, relation "
        "FROM block_edges ORDER BY to_block_id ASC, from_block_id ASC;";
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
        BlockEdge edge;
        edge.from_block_id = sqlite3_column_int(statement, 0);
        edge.to_block_id = sqlite3_column_int(statement, 1);
        edge.batch_id = column_text(statement, 2);
        edge.relation = column_text(statement, 3);
        edges.push_back(std::move(edge));
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read block edges failed: " << sqlite3_errmsg(db) << '\n';

    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}
