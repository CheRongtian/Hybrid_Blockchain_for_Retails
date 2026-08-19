#ifndef DB_UTILS_HPP
#define DB_UTILS_HPP

#include "block_data.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct PersistentAuthSession
{
    UserAccount account;
    std::int64_t expires_at = 0;
};

bool init_database(const std::string& db_path);
bool load_confirmation_policy(const std::string& db_path,
                              const std::string& role,
                              ConfirmationPolicy& policy);
bool load_confirmation_policies(const std::string& db_path,
                                std::vector<ConfirmationPolicy>& policies);
bool save_confirmation_policies(
    const std::string& db_path,
    const std::vector<ConfirmationPolicy>& policies);
bool ensure_route_confirmation_policies(
    const std::string& db_path,
    const std::string& route_id,
    const std::vector<SupplyRouteNode>& nodes,
    const std::vector<SupplyRouteEdge>& edges);
bool load_route_confirmation_policy(const std::string& db_path,
                                    const std::string& route_id,
                                    const std::string& node_id,
                                    ConfirmationPolicy& policy);
bool load_route_confirmation_policies(
    const std::string& db_path,
    const std::string& route_id,
    std::vector<ConfirmationPolicy>& policies);
bool save_route_confirmation_policies(
    const std::string& db_path,
    const std::string& route_id,
    const std::vector<ConfirmationPolicy>& policies);
std::unordered_set<std::string> supplier_route_path_node_ids(
    const std::vector<SupplyRouteNode>& nodes,
    const std::vector<SupplyRouteEdge>& edges);
bool insert_user_account(const std::string& db_path,
                         const UserAccount& account);
bool save_user_public_key(const std::string& db_path,
                          const std::string& uid,
                          const std::string& public_key);
std::optional<UserAccount> find_user_account(const std::string& db_path,
                                             const std::string& username);
bool load_user_accounts(const std::string& db_path,
                        const std::string& role,
                        std::vector<UserAccount>& accounts);
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
bool ensure_default_workflow(const std::string& db_path);
bool ensure_batch_workflow(const std::string& db_path,
                           const std::string& batch_id,
                           std::string& route_id);
bool load_workflow_route(const std::string& db_path,
                         const std::string& batch_id,
                         std::string& route_id,
                         std::vector<SupplyRouteNode>& nodes,
                         std::vector<SupplyRouteEdge>& edges);
bool load_workflow_route_by_id(const std::string& db_path,
                               const std::string& route_id,
                               std::vector<SupplyRouteNode>& nodes,
                               std::vector<SupplyRouteEdge>& edges);
bool save_workflow_route(const std::string& db_path,
                         const std::string& batch_id,
                         const std::vector<SupplyRouteNode>& nodes,
                         const std::vector<SupplyRouteEdge>& edges,
                         std::string& route_id,
                         std::string& error,
                         bool allow_incomplete = false);
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
