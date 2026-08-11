#ifndef DB_UTILS_HPP
#define DB_UTILS_HPP

#include <optional>
#include <string>
#include <vector>

struct UserAccount
{
    std::string uid;
    std::string username;
    std::string password_salt;
    std::string password_hash;
    std::string role;
    std::string organization_id;
    bool active = true;
};

struct SupplyChainRecord
{
    int block_id = -1;
    std::string batch_id;
    std::string product;
    std::string origin;
    std::string stage;
    std::string confirmed_by;
    std::string uid;
    std::string role;
    std::string organization_id;
    std::string canonical_record;
    std::string root_hash;
    std::string proof;
    bool verified = false;
    std::string block_hash;
    std::string chain_status = "in_progress";
    std::string created_at;
};

struct BlockEdge
{
    int from_block_id = -1;
    int to_block_id = -1;
    std::string batch_id;
    std::string relation = "continues";
};

bool init_database(const std::string& db_path);
bool insert_user_account(const std::string& db_path,
                         const UserAccount& account);
std::optional<UserAccount> find_user_account(const std::string& db_path,
                                             const std::string& username);
bool insert_supply_chain_record(const std::string& db_path,
                                const SupplyChainRecord& record);
bool insert_supply_chain_block(const std::string& db_path,
                               const SupplyChainRecord& record,
                               const std::vector<BlockEdge>& edges);
bool load_supply_chain_records(const std::string& db_path,
                               std::vector<SupplyChainRecord>& records);
bool load_block_edges(const std::string& db_path,
                      std::vector<BlockEdge>& edges);

#endif
