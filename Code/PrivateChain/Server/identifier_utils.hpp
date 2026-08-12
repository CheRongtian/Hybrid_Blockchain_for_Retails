#ifndef SUPPLY_CHAIN_IDENTIFIER_UTILS_HPP
#define SUPPLY_CHAIN_IDENTIFIER_UTILS_HPP

#include "block_data.hpp"

#include <optional>
#include <string>
#include <vector>

std::optional<std::string> normalize_product_code(const std::string& product);

std::optional<std::string> next_batch_id_for_product(
    const std::vector<SupplyChainBatch>& batches,
    const std::string& product);

std::string identifier_format_error(const std::string& field_name,
                                    const std::string& value);

std::string measurement_format_error(const std::string& field_name,
                                     const std::string& value);

#endif
