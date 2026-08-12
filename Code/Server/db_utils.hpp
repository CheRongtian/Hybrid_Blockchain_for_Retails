#ifndef DB_UTILS_HPP
#define DB_UTILS_HPP

#include "block_data.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PersistentAuthSession
{
    UserAccount account;
    std::int64_t expires_at = 0;
};

bool init_database(const std::string& db_path);
bool insert_user_account(const std::string& db_path,
                         const UserAccount& account);
std::optional<UserAccount> find_user_account(const std::string& db_path,
                                             const std::string& username);
bool create_persistent_auth_session(const std::string& db_path,
                                    const std::string& token_hash,
                                    const std::string& uid,
                                    std::int64_t expires_at);
std::optional<PersistentAuthSession> find_persistent_auth_session(
    const std::string& db_path,
    const std::string& token_hash,
    std::int64_t now);
bool delete_persistent_auth_session(const std::string& db_path,
                                    const std::string& token_hash,
                                    bool& deleted);
bool delete_expired_auth_sessions(const std::string& db_path,
                                  std::int64_t now);
std::optional<SupplyChainBatch> find_supply_chain_batch(
    const std::string& db_path,
    const std::string& batch_id);
bool load_supply_chain_batches(const std::string& db_path,
                               std::vector<SupplyChainBatch>& batches);
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
