#include "db_utils.hpp"
#include <sqlite3.h>
#include <iostream>

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
        "canonical_record TEXT NOT NULL,"
        "root_hash TEXT NOT NULL,"
        "proof TEXT NOT NULL,"
        "verified INTEGER NOT NULL CHECK (verified IN (0, 1)),"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";

    if(sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK)
    {
        std::cerr << "Create table failed: " << err_msg << '\n';
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

bool insert_supply_chain_record(const std::string& db_path,
                                const SupplyChainRecord& record)
{
    sqlite3* db = nullptr;
    if(sqlite3_open(db_path.c_str(), &db) != SQLITE_OK)
    {
        std::cerr << "Cannot open database: " << sqlite3_errmsg(db) << '\n';
        if(db) sqlite3_close(db);
        return false;
    }

    const char* sql =
        "INSERT INTO supply_chain_records ("
        "block_id, batch_id, product, origin, stage, confirmed_by, "
        "canonical_record, root_hash, proof, verified"
        ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* statement = nullptr;

    if(sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK)
    {
        std::cerr << "Prepare insert failed: " << sqlite3_errmsg(db) << '\n';
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
        bind_text(statement, 7, record.canonical_record) &&
        bind_text(statement, 8, record.root_hash) &&
        bind_text(statement, 9, record.proof) &&
        sqlite3_bind_int(statement, 10, record.verified ? 1 : 0) == SQLITE_OK;

    if(!bound || sqlite3_step(statement) != SQLITE_DONE)
    {
        std::cerr << "Insert record failed: " << sqlite3_errmsg(db) << '\n';
        sqlite3_finalize(statement);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(statement);
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
        "canonical_record, root_hash, proof, verified, created_at "
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
        record.canonical_record = column_text(statement, 6);
        record.root_hash = column_text(statement, 7);
        record.proof = column_text(statement, 8);
        record.verified = sqlite3_column_int(statement, 9) != 0;
        record.created_at = column_text(statement, 10);
        records.push_back(std::move(record));
    }

    const bool succeeded = result == SQLITE_DONE;
    if(!succeeded)
        std::cerr << "Read records failed: " << sqlite3_errmsg(db) << '\n';

    sqlite3_finalize(statement);
    sqlite3_close(db);
    return succeeded;
}
