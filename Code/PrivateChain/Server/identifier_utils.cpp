#include "identifier_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{
std::string trim_copy(const std::string& value)
{
    const auto first = std::find_if_not(
        value.begin(), value.end(), [](unsigned char character) {
            return std::isspace(character) != 0;
        });
    const auto last = std::find_if_not(
        value.rbegin(), value.rend(), [](unsigned char character) {
            return std::isspace(character) != 0;
        }).base();
    if(first >= last) return "";
    return std::string(first, last);
}

bool is_four_digit_suffix(const std::string& value, std::size_t offset)
{
    if(value.size() != offset + 4) return false;
    for(std::size_t index = offset; index < value.size(); ++index)
    {
        if(!std::isdigit(static_cast<unsigned char>(value[index]))) return false;
    }
    return true;
}

bool matches_prefix(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0 &&
           is_four_digit_suffix(value, prefix.size());
}

bool matches_any_prefix(const std::string& value,
                        const std::vector<std::string>& prefixes)
{
    for(const std::string& prefix : prefixes)
    {
        if(matches_prefix(value, prefix)) return true;
    }
    return false;
}

void skip_spaces(const std::string& value, std::size_t& cursor)
{
    while(cursor < value.size() && std::isspace(
              static_cast<unsigned char>(value[cursor])) != 0)
        ++cursor;
}

bool parse_measurement_number(const std::string& value,
                              std::size_t& cursor,
                              double& number)
{
    skip_spaces(value, cursor);
    const std::size_t start = cursor;
    if(cursor < value.size() && (value[cursor] == '+' || value[cursor] == '-'))
        ++cursor;

    const std::size_t integer_start = cursor;
    while(cursor < value.size() && std::isdigit(
              static_cast<unsigned char>(value[cursor])) != 0)
        ++cursor;
    if(cursor == integer_start) return false;

    if(cursor < value.size() && value[cursor] == '.')
    {
        ++cursor;
        const std::size_t fraction_start = cursor;
        while(cursor < value.size() && std::isdigit(
                  static_cast<unsigned char>(value[cursor])) != 0)
            ++cursor;
        if(cursor == fraction_start) return false;
    }

    try
    {
        std::size_t parsed = 0;
        number = std::stod(value.substr(start, cursor - start), &parsed);
        return parsed == cursor - start && std::isfinite(number);
    }
    catch(const std::exception&)
    {
        return false;
    }
}

bool parse_measurement_value(const std::string& value,
                             double minimum,
                             double maximum)
{
    std::size_t cursor = 0;
    double first = 0.0;
    if(!parse_measurement_number(value, cursor, first)) return false;
    skip_spaces(value, cursor);

    double last = first;
    if(cursor < value.size())
    {
        if(value[cursor] != '-') return false;
        ++cursor;
        if(!parse_measurement_number(value, cursor, last)) return false;
        skip_spaces(value, cursor);
        if(first > last) return false;
    }

    if(cursor != value.size()) return false;
    return first >= minimum && last <= maximum;
}

std::string product_code(const std::string& product)
{
    const std::string trimmed = trim_copy(product);
    std::string code;
    bool separator_pending = false;
    for(const unsigned char character : trimmed)
    {
        if(std::isalnum(character))
        {
            if(separator_pending && !code.empty()) code.push_back('-');
            code.push_back(static_cast<char>(std::toupper(character)));
            separator_pending = false;
        }
        else
        {
            separator_pending = !code.empty();
        }
    }
    return code;
}

std::string format_batch_id(const std::string& code, int sequence)
{
    std::ostringstream result;
    result << "BATCH-" << code << '-' << std::setw(4) << std::setfill('0')
           << sequence;
    return result.str();
}

int batch_sequence(const std::string& batch_id, const std::string& prefix)
{
    if(!matches_prefix(batch_id, prefix)) return 0;
    return std::stoi(batch_id.substr(prefix.size()));
}
}

std::optional<std::string> normalize_product_code(const std::string& product)
{
    const std::string code = product_code(product);
    if(code.empty()) return std::nullopt;
    return code;
}

std::optional<std::string> next_batch_id_for_product(
    const std::vector<SupplyChainBatch>& batches,
    const std::string& product)
{
    const auto code = normalize_product_code(product);
    if(!code) return std::nullopt;

    const std::string prefix = "BATCH-" + *code + '-';
    int highest_sequence = 0;
    for(const SupplyChainBatch& batch : batches)
    {
        highest_sequence = std::max(
            highest_sequence, batch_sequence(batch.batch_id, prefix));
    }

    if(highest_sequence >= 9999) return std::nullopt;
    return format_batch_id(*code, highest_sequence + 1);
}

std::string identifier_format_error(const std::string& field_name,
                                    const std::string& value)
{
    static const std::vector<std::pair<std::string, std::vector<std::string>>> rules = {
        {"certificateId", {"CERT-"}},
        {"shipmentId", {"SHIP-"}},
        {"vehicleContainerId", {"VEHICLE-", "CONTAINER-"}},
        {"storageLotId", {"STORAGE-"}},
        {"storageZoneRackId", {"ZONE-", "RACK-"}},
        {"storeLocationId", {"STORE-"}}
    };

    for(const auto& rule : rules)
    {
        if(rule.first != field_name) continue;
        if(matches_any_prefix(value, rule.second)) return "";

        std::ostringstream expected;
        for(std::size_t index = 0; index < rule.second.size(); ++index)
        {
            if(index > 0) expected << " or ";
            expected << rule.second[index] << "0001";
        }
        return field_name + " must use format " + expected.str();
    }
    return "";
}

std::string measurement_format_error(const std::string& field_name,
                                     const std::string& value)
{
    if(field_name == "temperature")
    {
        if(!parse_measurement_value(value, -1000.0, 1000.0))
            return "temperature must be one number or an ordered range";
    }
    else if(field_name == "temperatureUnit")
    {
        if(value != "C" && value != "F")
            return "temperatureUnit must be C or F";
    }
    else if(field_name == "humidity")
    {
        if(!parse_measurement_value(value, 0.0, 100.0))
            return "humidity must be a percentage from 0 to 100 or an ordered range";
    }
    return "";
}
