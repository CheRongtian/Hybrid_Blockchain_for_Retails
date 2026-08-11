#ifndef DB_UTILS_HPP
#define DB_UTILS_HPP

#include <string>
#include <vector>

struct SupplyChainRecord
{
    int block_id = -1;
    std::string batch_id;
    std::string product;
    std::string origin;
    std::string stage;
    std::string confirmed_by;
    std::string canonical_record;
    std::string root_hash;
    std::string proof;
    bool verified = false;
    std::string created_at;
};

bool init_database(const std::string& db_path);
bool insert_supply_chain_record(const std::string& db_path,
                                const SupplyChainRecord& record);
bool load_supply_chain_records(const std::string& db_path,
                               std::vector<SupplyChainRecord>& records);

#endif
