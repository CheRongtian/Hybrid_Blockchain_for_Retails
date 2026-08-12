#ifndef SUPPLY_CHAIN_BLOCK_DATA_HPP
#define SUPPLY_CHAIN_BLOCK_DATA_HPP

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

struct SupplyChainBatch
{
    std::string batch_id;
    std::string product;
    std::string harvest_date;
    std::string farm_location;
    std::string certificate_id;
    std::string created_by_uid;
    std::string current_stage;
    std::string status = "in_progress";
    std::string created_at;
};

struct IpfsReference
{
    std::string category;
    std::string cid;
    std::string filename;
    std::string content_type;
    long long size = 0;
};

struct MerkleField
{
    std::string name;
    std::string value;
};

struct MerkleLeafRecord
{
    int leaf_index = -1;
    std::string field_name;
    std::string leaf_value;
    std::string leaf_hash;
    std::string proof;
    bool verified = false;
};

struct SupplyChainRecord
{
    int block_id = -1;
    int parent_block_id = -1;
    std::string parent_block_hash = "GENESIS";
    std::string batch_id;
    std::string product;
    std::string location_summary;
    std::string batch_harvest_date;
    std::string batch_farm_location;
    std::string certificate_id;
    std::string stage;
    std::string confirmed_by;
    std::string uid;
    std::string role;
    std::string organization_id;
    std::string event_data;
    std::vector<IpfsReference> ipfs_refs;
    std::vector<MerkleField> merkle_fields;
    std::string canonical_record;
    std::string root_hash;
    bool verified = false;
    std::string block_hash;
    std::string chain_status = "in_progress";
    std::string created_at;
    std::vector<MerkleLeafRecord> merkle_leaves;
};

struct BlockEdge
{
    int from_block_id = -1;
    int to_block_id = -1;
    std::string batch_id;
    std::string relation = "continues";
};

#endif
