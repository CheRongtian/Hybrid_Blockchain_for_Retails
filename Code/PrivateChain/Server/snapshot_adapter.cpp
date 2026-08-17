#include "snapshot_adapter.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <unordered_set>
#include <utility>

namespace
{
void skip_whitespace(const std::string& json, std::size_t& cursor)
{
    while(cursor < json.size() &&
          std::isspace(static_cast<unsigned char>(json[cursor])))
        ++cursor;
}

int hex_value(char ch)
{
    if(ch >= '0' && ch <= '9') return ch - '0';
    if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::optional<std::string> parse_json_string(const std::string& json,
                                             std::size_t& cursor)
{
    if(cursor >= json.size() || json[cursor] != '"') return std::nullopt;
    ++cursor;
    std::string value;
    while(cursor < json.size())
    {
        const char ch = json[cursor++];
        if(ch == '"') return value;
        if(ch != '\\')
        {
            value += ch;
            continue;
        }
        if(cursor >= json.size()) return std::nullopt;
        const char escape = json[cursor++];
        switch(escape)
        {
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '/': value += '/'; break;
            case 'b': value += '\b'; break;
            case 'f': value += '\f'; break;
            case 'n': value += '\n'; break;
            case 'r': value += '\r'; break;
            case 't': value += '\t'; break;
            case 'u':
            {
                if(cursor + 4 > json.size()) return std::nullopt;
                int code = 0;
                for(int index = 0; index < 4; ++index)
                {
                    const int digit = hex_value(json[cursor + index]);
                    if(digit < 0) return std::nullopt;
                    code = (code << 4) | digit;
                }
                cursor += 4;
                if(code <= 0x7f)
                    value += static_cast<char>(code);
                else
                    return std::nullopt;
                break;
            }
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::map<std::string, std::string>> parse_flat_string_object(
    const std::string& json)
{
    std::map<std::string, std::string> fields;
    std::size_t cursor = 0;
    skip_whitespace(json, cursor);
    if(cursor >= json.size() || json[cursor++] != '{') return std::nullopt;
    skip_whitespace(json, cursor);
    if(cursor < json.size() && json[cursor] == '}')
    {
        ++cursor;
        skip_whitespace(json, cursor);
        return cursor == json.size()
            ? std::optional<std::map<std::string, std::string>>(fields)
            : std::nullopt;
    }

    while(cursor < json.size())
    {
        const auto key = parse_json_string(json, cursor);
        if(!key) return std::nullopt;
        skip_whitespace(json, cursor);
        if(cursor >= json.size() || json[cursor++] != ':') return std::nullopt;
        skip_whitespace(json, cursor);
        const auto value = parse_json_string(json, cursor);
        if(!value) return std::nullopt;
        fields[*key] = *value;
        skip_whitespace(json, cursor);
        if(cursor >= json.size()) return std::nullopt;
        if(json[cursor] == '}')
        {
            ++cursor;
            skip_whitespace(json, cursor);
            return cursor == json.size()
                ? std::optional<std::map<std::string, std::string>>(fields)
                : std::nullopt;
        }
        if(json[cursor++] != ',') return std::nullopt;
        skip_whitespace(json, cursor);
    }
    return std::nullopt;
}

const SupplyRouteNode* route_node_for_record(
    const std::vector<SupplyRouteNode>& route_nodes,
    const SupplyChainRecord& record,
    const std::string& route_id)
{
    if(!record.route_id.empty() && record.route_id != route_id)
        return nullptr;

    if(!record.route_node_id.empty())
    {
        for(const SupplyRouteNode& node : route_nodes)
        {
            if(node.node_id == record.route_node_id &&
               node.role == record.role &&
               node.username == record.confirmed_by &&
               (record.route_step_index < 0 ||
                node.step_index == record.route_step_index))
                return &node;
        }
        return nullptr;
    }

    const SupplyRouteNode* legacy_match = nullptr;
    for(const SupplyRouteNode& node : route_nodes)
    {
        if(node.role == record.role && !node.username.empty() &&
           node.username == record.confirmed_by &&
           (record.route_step_index < 0 ||
            node.step_index == record.route_step_index))
        {
            if(legacy_match) return nullptr;
            legacy_match = &node;
        }
    }
    return legacy_match;
}
}

supermarket::snapshot::BatchInput make_snapshot_batch_input(
    const SupplyChainBatch& batch,
    const std::vector<SupplyChainRecord>& records,
    const std::vector<SupplyRouteNode>& route_nodes)
{
    supermarket::snapshot::BatchInput input;
    input.batch_id = batch.batch_id;
    input.product = batch.product;
    input.harvest_date = batch.harvest_date;
    input.farm_location = batch.farm_location;
    input.certificate_id = batch.certificate_id;
    input.status = batch.status;

    const std::string expected_route_id = !batch.route_id.empty()
        ? batch.route_id
        : (route_nodes.empty() ? "" : route_nodes.front().route_id);
    std::unordered_set<std::string> recorded_route_nodes;

    for(const SupplyRouteNode& node : route_nodes)
    {
        input.route_nodes.push_back(supermarket::snapshot::RouteNodeInput{
            node.node_id, node.label, node.role, node.username, node.step_index
        });
    }

    for(const SupplyChainRecord& record : records)
    {
        if(record.batch_id != batch.batch_id) continue;
        supermarket::snapshot::StageInput stage;
        stage.block_id = record.block_id;
        stage.parent_block_id = record.parent_block_id;
        stage.parent_block_hash = record.parent_block_hash;
        stage.stage = record.stage;
        stage.block_hash = record.block_hash;
        stage.chain_status = record.chain_status;
        stage.verified = record.verified;
        stage.signature_verified = record.signature_verified;
        stage.route_node_id = record.route_node_id;
        stage.route_step_index = record.route_step_index;

        const SupplyRouteNode* route_node =
            route_node_for_record(route_nodes, record, expected_route_id);
        if(route_node)
        {
            stage.route_node_id = route_node->node_id;
            stage.route_node_label = route_node->label;
            stage.route_node_username = route_node->username;
            stage.route_step_index = route_node->step_index;
            recorded_route_nodes.insert(route_node->node_id);
        }
        else
        {
            input.source_errors.push_back(
                "Block " + std::to_string(record.block_id) +
                " is not linked to the selected route");
        }

        const auto fields = parse_flat_string_object(record.event_data);
        if(fields)
        {
            stage.event_fields = *fields;
        }
        else
        {
            input.source_errors.push_back(
                "Block " + std::to_string(record.block_id) +
                " contains malformed event data");
        }

        for(const IpfsReference& reference : record.ipfs_refs)
        {
            stage.evidence.push_back(supermarket::snapshot::EvidenceInput{
                record.stage, reference.category, reference.cid
            });
        }
        input.stages.push_back(std::move(stage));
    }

    std::sort(input.stages.begin(), input.stages.end(),
              [](const supermarket::snapshot::StageInput& left,
                 const supermarket::snapshot::StageInput& right) {
                  if(left.route_step_index >= 0 && right.route_step_index >= 0 &&
                     left.route_step_index != right.route_step_index)
                      return left.route_step_index < right.route_step_index;
                  return left.block_id < right.block_id;
              });

    if(!route_nodes.empty())
    {
        const bool route_complete =
            recorded_route_nodes.size() == route_nodes.size() &&
            std::all_of(
                route_nodes.begin(), route_nodes.end(),
                [&](const SupplyRouteNode& node) {
                    return recorded_route_nodes.count(node.node_id) != 0;
                });
        input.status = route_complete ? "completed" : "in_progress";
    }
    return input;
}
