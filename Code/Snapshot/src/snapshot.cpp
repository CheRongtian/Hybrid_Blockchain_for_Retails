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

const std::vector<std::string>& expected_stages()
{
    static const std::vector<std::string> stages = {
        "supplier", "logistics", "warehouse", "supermarket"
    };
    return stages;
}

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

const StageInput* find_stage(const BatchInput& input,
                             const std::string& stage)
{
    for(const StageInput& item : input.stages)
    {
        if(item.stage == stage) return &item;
    }
    return nullptr;
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

int stage_rank(const std::string& stage)
{
    const auto& stages = expected_stages();
    const auto item = std::find(stages.begin(), stages.end(), stage);
    return item == stages.end()
        ? static_cast<int>(stages.size())
        : static_cast<int>(std::distance(stages.begin(), item));
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

    const auto& stages = expected_stages();
    if(input.stages.size() != stages.size())
    {
        result.errors.push_back("The batch must contain exactly four route stages");
    }

    for(std::size_t index = 0; index < stages.size(); ++index)
    {
        const StageInput* stage = find_stage(input, stages[index]);
        if(!stage)
        {
            result.errors.push_back("Missing " + stages[index] + " stage");
            continue;
        }

        if(!stage->verified)
            result.errors.push_back(stages[index] + " Merkle verification failed");
        if(!stage->signature_verified)
            result.errors.push_back(stages[index] + " signature verification failed");
        if(stage->block_hash.empty())
            result.errors.push_back(stages[index] + " block hash is missing");

        if(index == 0)
        {
            if(stage->parent_block_id != -1 || stage->parent_block_hash != "GENESIS")
                result.errors.push_back("Supplier stage is not the genesis block");
        }
        else
        {
            const StageInput* parent = find_stage(input, stages[index - 1]);
            if(parent && (stage->parent_block_id != parent->block_id ||
                          stage->parent_block_hash != parent->block_hash))
            {
                result.errors.push_back(stages[index] +
                                        " does not link to the previous stage");
            }
        }
    }

    if(const StageInput* logistics = find_stage(input, "logistics"))
    {
        for(const char* field : {
                "pickupLocation", "deliveryLocation", "departureTime",
                "arrivalTime", "temperature", "temperatureUnit", "humidity"})
            require_value(*logistics, field, result.errors);
    }
    if(const StageInput* warehouse = find_stage(input, "warehouse"))
    {
        for(const char* field : {
                "inboundTime", "outboundTime", "temperature",
                "temperatureUnit", "humidity"})
            require_value(*warehouse, field, result.errors);
    }
    if(const StageInput* supermarket = find_stage(input, "supermarket"))
    {
        for(const char* field : {
                "shelfPlacementDate", "expirationSellByDate", "storeLocationId"})
            require_value(*supermarket, field, result.errors);
        if(supermarket->chain_status != "completed")
            result.errors.push_back("The Supermarket block is not marked completed");
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

    const StageInput& logistics = *find_stage(input, "logistics");
    const StageInput& warehouse = *find_stage(input, "warehouse");
    const StageInput& supermarket = *find_stage(input, "supermarket");
    const auto logistics_temperature = parse_measurement(
        event_value(logistics, "temperature"), -1000.0, 1000.0);
    const auto logistics_humidity = parse_measurement(
        event_value(logistics, "humidity"), 0.0, 100.0);
    const auto warehouse_temperature = parse_measurement(
        event_value(warehouse, "temperature"), -1000.0, 1000.0);
    const auto warehouse_humidity = parse_measurement(
        event_value(warehouse, "humidity"), 0.0, 100.0);
    if(!logistics_temperature || !logistics_humidity ||
       !warehouse_temperature || !warehouse_humidity)
    {
        error = "A public temperature or humidity summary is malformed";
        return std::nullopt;
    }
    const std::string logistics_temperature_unit =
        event_value(logistics, "temperatureUnit");
    const std::string warehouse_temperature_unit =
        event_value(warehouse, "temperatureUnit");
    if((logistics_temperature_unit != "C" &&
        logistics_temperature_unit != "F") ||
       (warehouse_temperature_unit != "C" &&
        warehouse_temperature_unit != "F"))
    {
        error = "A public temperature unit must be C or F";
        return std::nullopt;
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
                  const int left_rank = stage_rank(left.stage);
                  const int right_rank = stage_rank(right.stage);
                  if(left_rank != right_rank) return left_rank < right_rank;
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
    add_field("transport.pickup_location", event_value(logistics, "pickupLocation"));
    add_field("transport.delivery_location", event_value(logistics, "deliveryLocation"));
    add_field("transport.departure_local_time", event_value(logistics, "departureTime"));
    add_field("transport.arrival_local_time", event_value(logistics, "arrivalTime"));
    add_field("transport.time_zone", "unspecified");
    add_field("transport.temperature.minimum", logistics_temperature->minimum);
    add_field("transport.temperature.maximum", logistics_temperature->maximum);
    add_field("transport.temperature.unit", logistics_temperature_unit);
    add_field("transport.humidity.minimum", logistics_humidity->minimum);
    add_field("transport.humidity.maximum", logistics_humidity->maximum);
    add_field("transport.humidity.unit", "percent_rh");
    add_field("storage.inbound_local_time", event_value(warehouse, "inboundTime"));
    add_field("storage.outbound_local_time", event_value(warehouse, "outboundTime"));
    add_field("storage.time_zone", "unspecified");
    add_field("storage.temperature.minimum", warehouse_temperature->minimum);
    add_field("storage.temperature.maximum", warehouse_temperature->maximum);
    add_field("storage.temperature.unit", warehouse_temperature_unit);
    add_field("storage.humidity.minimum", warehouse_humidity->minimum);
    add_field("storage.humidity.maximum", warehouse_humidity->maximum);
    add_field("storage.humidity.unit", "percent_rh");
    add_field("retail.store_location_id", event_value(supermarket, "storeLocationId"));
    add_field("retail.shelf_placement_date", event_value(supermarket, "shelfPlacementDate"));
    add_field("retail.sell_by_date", event_value(supermarket, "expirationSellByDate"));
    for(std::size_t index = 0; index < expected_stages().size(); ++index)
        add_field("verification.route." + std::to_string(index), expected_stages()[index]);
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
        << ",\"transport\":{\"pickup_location\":"
        << json_string(event_value(logistics, "pickupLocation"))
        << ",\"delivery_location\":"
        << json_string(event_value(logistics, "deliveryLocation"))
        << ",\"departure_local_time\":"
        << json_string(event_value(logistics, "departureTime"))
        << ",\"arrival_local_time\":"
        << json_string(event_value(logistics, "arrivalTime"))
        << ",\"time_zone\":\"unspecified\",\"temperature\":"
        << measurement_json(*logistics_temperature,
                            logistics_temperature_unit)
        << ",\"humidity\":"
        << measurement_json(*logistics_humidity, "percent_rh") << "}"
        << ",\"storage\":{\"inbound_local_time\":"
        << json_string(event_value(warehouse, "inboundTime"))
        << ",\"outbound_local_time\":"
        << json_string(event_value(warehouse, "outboundTime"))
        << ",\"time_zone\":\"unspecified\",\"temperature\":"
        << measurement_json(*warehouse_temperature,
                            warehouse_temperature_unit)
        << ",\"humidity\":"
        << measurement_json(*warehouse_humidity, "percent_rh") << "}"
        << ",\"retail\":{\"store_location_id\":"
        << json_string(event_value(supermarket, "storeLocationId"))
        << ",\"shelf_placement_date\":"
        << json_string(event_value(supermarket, "shelfPlacementDate"))
        << ",\"sell_by_date\":"
        << json_string(event_value(supermarket, "expirationSellByDate")) << "}"
        << ",\"verification\":{\"route\":[\"supplier\",\"logistics\","
           "\"warehouse\",\"supermarket\"],\"route_completed\":true,"
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
