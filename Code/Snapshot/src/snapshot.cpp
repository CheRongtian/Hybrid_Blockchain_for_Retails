#include "snapshot.hpp"

#include "MerkleTree.hpp"
#include "snapshot_policy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <exception>
#include <iomanip>
#include <iterator>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace supermarket::snapshot
{
namespace
{
constexpr const char* SNAPSHOT_PROTOCOL = "Supermarket-Trace-v1";
constexpr int SNAPSHOT_VERSION = 1;

std::string trim(std::string value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if(first == std::string::npos) return "";
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string json_escape(const std::string& value)
{
    std::ostringstream escaped;
    for(const unsigned char ch : value)
    {
        switch(ch)
        {
            case '"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if(ch < 0x20)
                {
                    escaped << "\\u00" << std::hex << std::setw(2)
                            << std::setfill('0') << static_cast<int>(ch)
                            << std::dec << std::setfill(' ');
                }
                else
                {
                    escaped << static_cast<char>(ch);
                }
        }
    }
    return escaped.str();
}

std::string json_string(const std::string& value)
{
    return "\"" + json_escape(value) + "\"";
}

std::string utc_timestamp()
{
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream timestamp;
    timestamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return timestamp.str();
}

std::string version_suffix(int version)
{
    std::ostringstream suffix;
    suffix << 'V' << std::setw(4) << std::setfill('0') << version;
    return suffix.str();
}

std::string timestamp_id_component(const std::string& timestamp)
{
    std::string component;
    component.reserve(timestamp.size());
    for(const char ch : timestamp)
    {
        if(ch != '-' && ch != ':') component += ch;
    }
    return component;
}

std::string event_value(const StageInput& stage, const std::string& name)
{
    const auto item = stage.event_fields.find(name);
    return item == stage.event_fields.end() ? "" : trim(item->second);
}

void require_value(const StageInput& stage,
                   const std::string& field,
                   std::vector<std::string>& errors)
{
    if(event_value(stage, field).empty())
        errors.push_back(stage.stage + " is missing " + field);
}

std::string join_errors(const std::vector<std::string>& errors)
{
    std::ostringstream joined;
    for(std::size_t index = 0; index < errors.size(); ++index)
    {
        if(index > 0) joined << "; ";
        joined << errors[index];
    }
    return joined.str();
}

std::optional<std::string> canonical_number(const std::string& value)
{
    try
    {
        std::size_t parsed = 0;
        double number = std::stod(value, &parsed);
        if(parsed != value.size() || !std::isfinite(number)) return std::nullopt;
        if(number == 0) number = 0;
        std::ostringstream normalized;
        normalized.imbue(std::locale::classic());
        normalized << std::setprecision(15) << std::defaultfloat << number;
        return normalized.str();
    }
    catch(const std::exception&)
    {
        return std::nullopt;
    }
}

struct Measurement
{
    std::string minimum;
    std::string maximum;
};

std::optional<Measurement> parse_measurement(const std::string& source,
                                             double allowed_minimum,
                                             double allowed_maximum)
{
    static const std::regex pattern(
        R"(^\s*([+-]?[0-9]+(?:\.[0-9]+)?)\s*(?:-\s*([+-]?[0-9]+(?:\.[0-9]+)?))?\s*$)");
    std::smatch match;
    if(!std::regex_match(source, match, pattern)) return std::nullopt;
    const auto minimum = canonical_number(match[1].str());
    const auto maximum = match[2].matched
        ? canonical_number(match[2].str())
        : minimum;
    if(!minimum || !maximum) return std::nullopt;
    const double minimum_value = std::stod(*minimum);
    const double maximum_value = std::stod(*maximum);
    if(minimum_value > maximum_value || minimum_value < allowed_minimum ||
       maximum_value > allowed_maximum)
        return std::nullopt;
    return Measurement{*minimum, *maximum};
}

std::string measurement_json(const Measurement& measurement,
                             const std::string& unit)
{
    return "{\"minimum\":" + measurement.minimum +
           ",\"maximum\":" + measurement.maximum +
           ",\"unit\":" + json_string(unit) + "}";
}

std::string public_leaf_input(const PublicField& field)
{
    return field.name + ":" + std::to_string(field.value.size()) + ":" +
           field.value;
}

std::string evidence_key(const std::string& stage,
                         const std::string& category,
                         const std::string& cid)
{
    return stage + '\x1f' + category + '\x1f' + cid;
}
}

Eligibility evaluate_eligibility(const BatchInput& input)
{
    Eligibility result;
    result.errors = input.source_errors;

    if(input.batch_id.empty()) result.errors.push_back("Batch ID is missing");
    if(input.product.empty()) result.errors.push_back("Product is missing");
    if(input.harvest_date.empty()) result.errors.push_back("Harvest date is missing");
    if(input.farm_location.empty()) result.errors.push_back("Farm location is missing");
    if(input.certificate_id.empty()) result.errors.push_back("Certificate ID is missing");
    if(input.status != "completed")
        result.errors.push_back("The batch has not completed the route");

    if(input.stages.size() < 2)
        result.errors.push_back("The batch route must contain at least two stages");
    if(!input.stages.empty() && input.stages.front().stage != "supplier")
        result.errors.push_back("The route must start with a Supplier stage");
    if(!input.stages.empty() && input.stages.back().stage != "supermarket")
        result.errors.push_back("The route must end with a Supermarket stage");

    std::vector<RouteNodeInput> ordered_route_nodes = input.route_nodes;
    std::sort(ordered_route_nodes.begin(), ordered_route_nodes.end(),
              [](const RouteNodeInput& left, const RouteNodeInput& right) {
                  if(left.step_index != right.step_index)
                      return left.step_index < right.step_index;
                  return left.node_id < right.node_id;
              });
    if(ordered_route_nodes.empty())
    {
        result.errors.push_back("The batch has no saved route definition");
    }
    else if(ordered_route_nodes.size() != input.stages.size())
    {
        result.errors.push_back(
            "The saved route and submitted blocks contain different stage counts");
    }
    else
    {
        for(std::size_t index = 0; index < input.stages.size(); ++index)
        {
            const RouteNodeInput& route_node = ordered_route_nodes[index];
            const StageInput& stage = input.stages[index];
            const std::string label = "Route stage " + std::to_string(index + 1);
            if(stage.route_node_id.empty() || stage.route_node_id != route_node.node_id)
                result.errors.push_back(label + " is not linked to its saved route node");
            if(stage.route_step_index >= 0 &&
               stage.route_step_index != route_node.step_index)
                result.errors.push_back(label + " has an inconsistent route step");
            if(route_node.role != stage.stage)
                result.errors.push_back(label + " role does not match its saved route role");
        }
    }

    for(std::size_t index = 0; index < input.stages.size(); ++index)
    {
        const StageInput& stage = input.stages[index];
        const bool supported = stage.stage == "supplier" ||
            stage.stage == "logistics" || stage.stage == "warehouse" ||
            stage.stage == "supermarket";
        if(!supported)
            result.errors.push_back("Unsupported route stage: " + stage.stage);

        const std::string label = stage.stage + " block " +
                                  std::to_string(stage.block_id);
        if(!stage.verified)
            result.errors.push_back(label + " Merkle verification failed");
        if(!stage.signature_verified)
            result.errors.push_back(label + " signature verification failed");
        if(stage.block_hash.empty())
            result.errors.push_back(label + " hash is missing");

        if(index == 0)
        {
            if(stage.parent_block_id != -1 || stage.parent_block_hash != "GENESIS")
                result.errors.push_back("Supplier stage is not the genesis block");
        }
        else
        {
            const StageInput& parent = input.stages[index - 1];
            if(stage.parent_block_id != parent.block_id ||
               stage.parent_block_hash != parent.block_hash)
            {
                result.errors.push_back(label + " does not link to the previous stage");
            }
        }

        if(stage.stage == "logistics")
        {
            for(const char* field : {
                    "pickupLocation", "deliveryLocation", "departureTime",
                    "arrivalTime", "temperature", "temperatureUnit", "humidity"})
                require_value(stage, field, result.errors);
        }
        else if(stage.stage == "warehouse")
        {
            for(const char* field : {
                    "inboundTime", "outboundTime", "temperature",
                    "temperatureUnit", "humidity"})
                require_value(stage, field, result.errors);
        }
        else if(stage.stage == "supermarket")
        {
            for(const char* field : {
                    "shelfPlacementDate", "expirationSellByDate", "storeLocationId"})
                require_value(stage, field, result.errors);
            if(stage.chain_status != "completed")
                result.errors.push_back("The Supermarket block is not marked completed");
        }
    }

    result.eligible = result.errors.empty();
    return result;
}

std::optional<Preview> build_preview(
    const BatchInput& input,
    const std::vector<EvidenceInput>& selected_evidence,
    std::string& error)
{
    error.clear();
    const Eligibility eligibility = evaluate_eligibility(input);
    if(!eligibility.eligible)
    {
        error = join_errors(eligibility.errors);
        return std::nullopt;
    }

    const StageInput& supermarket = input.stages.back();

    struct PublicRouteStage
    {
        const StageInput* stage = nullptr;
        std::optional<Measurement> temperature;
        std::optional<Measurement> humidity;
        std::string temperature_unit;
    };
    std::vector<PublicRouteStage> route_stages;
    route_stages.reserve(input.stages.size());
    for(const StageInput& stage : input.stages)
    {
        PublicRouteStage item;
        item.stage = &stage;
        if(stage.stage == "logistics" || stage.stage == "warehouse")
        {
            item.temperature = parse_measurement(
                event_value(stage, "temperature"), -1000.0, 1000.0);
            item.humidity = parse_measurement(
                event_value(stage, "humidity"), 0.0, 100.0);
            item.temperature_unit = event_value(stage, "temperatureUnit");
            if(!item.temperature || !item.humidity)
            {
                error = "A public temperature or humidity summary is malformed";
                return std::nullopt;
            }
            if(item.temperature_unit != "C" && item.temperature_unit != "F")
            {
                error = "A public temperature unit must be C or F";
                return std::nullopt;
            }
        }
        route_stages.push_back(std::move(item));
    }

    std::map<std::string, PublicEvidence> available_evidence;
    for(const StageInput& stage : input.stages)
    {
        for(const EvidenceInput& evidence : stage.evidence)
        {
            const EvidencePolicy* policy = find_evidence_policy(evidence.category);
            if(policy && !evidence.cid.empty())
            {
                available_evidence.emplace(
                    evidence_key(stage.stage, evidence.category, evidence.cid),
                    PublicEvidence{stage.stage, policy->public_type, evidence.cid});
            }
        }
    }

    std::set<std::string> unique_selected;
    std::vector<PublicEvidence> public_evidence;
    for(const EvidenceInput& selection : selected_evidence)
    {
        const std::string stage = trim(selection.stage);
        const std::string category = trim(selection.category);
        const std::string cid = trim(selection.cid);
        const std::string selected = evidence_key(stage, category, cid);
        if(stage.empty() || category.empty() || cid.empty() ||
           !unique_selected.insert(selected).second)
            continue;
        const auto evidence = available_evidence.find(selected);
        if(evidence == available_evidence.end())
        {
            error = "A selected CID is unavailable or not approved for public snapshots";
            return std::nullopt;
        }
        public_evidence.push_back(evidence->second);
    }
    std::sort(public_evidence.begin(), public_evidence.end(),
              [](const PublicEvidence& left, const PublicEvidence& right) {
                  if(left.stage != right.stage) return left.stage < right.stage;
                  if(left.type != right.type) return left.type < right.type;
                  return left.cid < right.cid;
              });

    Preview preview;
    preview.protocol = SNAPSHOT_PROTOCOL;
    preview.snapshot_version = SNAPSHOT_VERSION;
    preview.generated_at = utc_timestamp();
    preview.snapshot_id = "SNAP-" + input.batch_id + "-" +
                          timestamp_id_component(preview.generated_at) + "-" +
                          version_suffix(SNAPSHOT_VERSION);
    preview.batch_id = input.batch_id;
    preview.final_private_block_hash = supermarket.block_hash;
    preview.public_evidence = public_evidence;
    preview.excluded_fields = excluded_private_fields();

    auto add_field = [&](const std::string& name, const std::string& value) {
        preview.public_fields.push_back(PublicField{name, value});
    };

    add_field("protocol", preview.protocol);
    add_field("snapshot_id", preview.snapshot_id);
    add_field("snapshot_version", std::to_string(preview.snapshot_version));
    add_field("generated_at", preview.generated_at);
    add_field("batch.batch_id", input.batch_id);
    add_field("batch.product_name", input.product);
    add_field("batch.category", "Fresh Produce");
    add_field("batch.status", "completed");
    add_field("origin.harvest_date", input.harvest_date);
    add_field("origin.farm_location", input.farm_location);
    add_field("compliance.certificate_id", input.certificate_id);
    add_field("retail.store_location_id", event_value(supermarket, "storeLocationId"));
    add_field("retail.shelf_placement_date", event_value(supermarket, "shelfPlacementDate"));
    add_field("retail.sell_by_date", event_value(supermarket, "expirationSellByDate"));
    for(std::size_t index = 0; index < route_stages.size(); ++index)
    {
        const StageInput& stage = *route_stages[index].stage;
        const std::string prefix = "route." + std::to_string(index) + ".";
        add_field(prefix + "stage", stage.stage);
        add_field(prefix + "block_id", std::to_string(stage.block_id));
        add_field(prefix + "route_node_id", stage.route_node_id);
        add_field(prefix + "route_node_label", stage.route_node_label);
        add_field(prefix + "route_step_index", std::to_string(stage.route_step_index));
        if(stage.stage == "supplier")
        {
            add_field(prefix + "location", input.farm_location);
            add_field(prefix + "harvest_date", input.harvest_date);
        }
        else if(stage.stage == "logistics")
        {
            add_field(prefix + "pickup_location", event_value(stage, "pickupLocation"));
            add_field(prefix + "delivery_location", event_value(stage, "deliveryLocation"));
            add_field(prefix + "departure_local_time", event_value(stage, "departureTime"));
            add_field(prefix + "arrival_local_time", event_value(stage, "arrivalTime"));
            add_field(prefix + "temperature.minimum", route_stages[index].temperature->minimum);
            add_field(prefix + "temperature.maximum", route_stages[index].temperature->maximum);
            add_field(prefix + "temperature.unit", route_stages[index].temperature_unit);
            add_field(prefix + "humidity.minimum", route_stages[index].humidity->minimum);
            add_field(prefix + "humidity.maximum", route_stages[index].humidity->maximum);
            add_field(prefix + "humidity.unit", "percent_rh");
        }
        else if(stage.stage == "warehouse")
        {
            add_field(prefix + "inbound_local_time", event_value(stage, "inboundTime"));
            add_field(prefix + "outbound_local_time", event_value(stage, "outboundTime"));
            add_field(prefix + "temperature.minimum", route_stages[index].temperature->minimum);
            add_field(prefix + "temperature.maximum", route_stages[index].temperature->maximum);
            add_field(prefix + "temperature.unit", route_stages[index].temperature_unit);
            add_field(prefix + "humidity.minimum", route_stages[index].humidity->minimum);
            add_field(prefix + "humidity.maximum", route_stages[index].humidity->maximum);
            add_field(prefix + "humidity.unit", "percent_rh");
        }
        else if(stage.stage == "supermarket")
        {
            add_field(prefix + "store_location_id", event_value(stage, "storeLocationId"));
            add_field(prefix + "shelf_placement_date", event_value(stage, "shelfPlacementDate"));
            add_field(prefix + "sell_by_date", event_value(stage, "expirationSellByDate"));
        }
        add_field("verification.route." + std::to_string(index), stage.stage);
    }
    add_field("verification.route_completed", "true");
    add_field("verification.all_stages_verified", "true");
    add_field("verification.all_signatures_verified", "true");
    add_field("verification.final_private_block_hash",
              preview.final_private_block_hash);
    for(std::size_t index = 0; index < public_evidence.size(); ++index)
    {
        const std::string prefix = "public_evidence." + std::to_string(index) + ".";
        add_field(prefix + "stage", public_evidence[index].stage);
        add_field(prefix + "type", public_evidence[index].type);
        add_field(prefix + "cid", public_evidence[index].cid);
    }

    MerkleTree tree(1);
    for(const PublicField& field : preview.public_fields)
    {
        if(!tree.Append(public_leaf_input(field)))
        {
            error = "Failed to build the public snapshot Merkle Tree";
            return std::nullopt;
        }
    }
    preview.public_root = tree.GetRootHash();
    if(preview.public_root.empty())
    {
        error = "The public snapshot Merkle root is empty";
        return std::nullopt;
    }

    std::ostringstream manifest;
    manifest
        << "{\"protocol\":" << json_string(preview.protocol)
        << ",\"snapshot_id\":" << json_string(preview.snapshot_id)
        << ",\"snapshot_version\":" << preview.snapshot_version
        << ",\"generated_at\":" << json_string(preview.generated_at)
        << ",\"batch\":{\"batch_id\":" << json_string(input.batch_id)
        << ",\"product_name\":" << json_string(input.product)
        << ",\"category\":\"Fresh Produce\",\"status\":\"completed\"}"
        << ",\"origin\":{\"harvest_date\":" << json_string(input.harvest_date)
        << ",\"farm_location\":" << json_string(input.farm_location) << "}"
        << ",\"compliance\":{\"certificate_id\":"
        << json_string(input.certificate_id) << "}"
        << ",\"route\":[";
    for(std::size_t index = 0; index < route_stages.size(); ++index)
    {
        if(index > 0) manifest << ',';
        const StageInput& stage = *route_stages[index].stage;
        manifest << "{\"sequence\":" << index + 1
                 << ",\"stage\":" << json_string(stage.stage)
                 << ",\"block_id\":" << stage.block_id
                 << ",\"route_node_id\":"
                 << json_string(stage.route_node_id)
                 << ",\"route_node_label\":"
                 << json_string(stage.route_node_label)
                 << ",\"route_step_index\":"
                 << stage.route_step_index;
        if(stage.stage == "supplier")
        {
            manifest << ",\"location\":" << json_string(input.farm_location)
                     << ",\"harvest_date\":" << json_string(input.harvest_date);
        }
        else if(stage.stage == "logistics")
        {
            manifest << ",\"pickup_location\":"
                     << json_string(event_value(stage, "pickupLocation"))
                     << ",\"delivery_location\":"
                     << json_string(event_value(stage, "deliveryLocation"))
                     << ",\"departure_local_time\":"
                     << json_string(event_value(stage, "departureTime"))
                     << ",\"arrival_local_time\":"
                     << json_string(event_value(stage, "arrivalTime"))
                     << ",\"time_zone\":\"unspecified\",\"temperature\":"
                     << measurement_json(*route_stages[index].temperature,
                                         route_stages[index].temperature_unit)
                     << ",\"humidity\":"
                     << measurement_json(*route_stages[index].humidity, "percent_rh");
        }
        else if(stage.stage == "warehouse")
        {
            manifest << ",\"inbound_local_time\":"
                     << json_string(event_value(stage, "inboundTime"))
                     << ",\"outbound_local_time\":"
                     << json_string(event_value(stage, "outboundTime"))
                     << ",\"time_zone\":\"unspecified\",\"temperature\":"
                     << measurement_json(*route_stages[index].temperature,
                                         route_stages[index].temperature_unit)
                     << ",\"humidity\":"
                     << measurement_json(*route_stages[index].humidity, "percent_rh");
        }
        else if(stage.stage == "supermarket")
        {
            manifest << ",\"store_location_id\":"
                     << json_string(event_value(stage, "storeLocationId"))
                     << ",\"shelf_placement_date\":"
                     << json_string(event_value(stage, "shelfPlacementDate"))
                     << ",\"sell_by_date\":"
                     << json_string(event_value(stage, "expirationSellByDate"));
        }
        manifest << '}';
    }
    manifest
        << "]"
        << ",\"retail\":{\"store_location_id\":"
        << json_string(event_value(supermarket, "storeLocationId"))
        << ",\"shelf_placement_date\":"
        << json_string(event_value(supermarket, "shelfPlacementDate"))
        << ",\"sell_by_date\":"
        << json_string(event_value(supermarket, "expirationSellByDate")) << "}"
        << ",\"verification\":{\"route\":[";
    for(std::size_t index = 0; index < input.stages.size(); ++index)
    {
        if(index > 0) manifest << ',';
        manifest << json_string(input.stages[index].stage);
    }
    manifest
        << "],\"route_completed\":true,"
           "\"all_stages_verified\":true,\"all_signatures_verified\":true,"
           "\"final_private_block_hash\":"
        << json_string(preview.final_private_block_hash) << "}"
        << ",\"public_evidence\":[";
    for(std::size_t index = 0; index < public_evidence.size(); ++index)
    {
        if(index > 0) manifest << ',';
        manifest << "{\"stage\":" << json_string(public_evidence[index].stage)
                 << ",\"type\":" << json_string(public_evidence[index].type)
                 << ",\"cid\":" << json_string(public_evidence[index].cid)
                 << '}';
    }
    manifest << "]}";
    preview.manifest_json = manifest.str();
    return preview;
}
}
