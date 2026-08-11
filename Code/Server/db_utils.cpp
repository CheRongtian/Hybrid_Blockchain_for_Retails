#include "db_utils.hpp"
#include<sqlite3.h>
#include<iostream>
#include<ctime>

std::string get_current_time()
{
    time_t now = time(nullptr);
    char buf[100];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
    return std::string(buf);
}

bool init_database(const std::string& db_path)
{
    sqlite3 *db;
    char* err_msg = nullptr;

    if(sqlite3_open(db_path.c_str(), &db)!=SQLITE_OK)
    {
        std::cerr << "Can't open DB\n";
        if(db) sqlite3_close(db);
        return false;
    }

    const char*sql = 
        "CREATE TABLE IF NOT EXISTS messages("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT NOT NULL,"
        "message TEXT NOT NULL,"
        "timestamp TEXT NOT NULL);";
    
    if(sqlite3_exec(db, sql, 0, 0, &err_msg)!=SQLITE_OK)
    {
        std::cerr << "Create table failed: "<< err_msg << "\n";
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return false;
    }
    sqlite3_close(db);
    return true;
}

bool insert_message(const std::string& db_path, const std::string& name, const std::string& message)
{
    sqlite3* db;
    if(sqlite3_open(db_path.c_str(), &db)!=SQLITE_OK)
    {
        std::cerr << "Can't open DB\n";
        if(db) sqlite3_close(db);
        return false;
    }
    
    const char* sql=
        "INSERT INTO messages (name, message, timestamp) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, 0)!= SQLITE_OK)
    {
        std::cerr << "Prepare failed\n";
        sqlite3_close(db);
        return false;
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, message.c_str(), -1, SQLITE_TRANSIENT);
    std::string timestamp = get_current_time();
    sqlite3_bind_text(stmt, 3, timestamp.c_str(), -1, SQLITE_TRANSIENT);

    if(sqlite3_step(stmt)!=SQLITE_DONE)
    {
        std::cerr << "Insert failed\n";
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return false;
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return true;
}
