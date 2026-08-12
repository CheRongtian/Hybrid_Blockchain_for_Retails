#ifndef SUPPLY_CHAIN_SNAPSHOT_ADAPTER_HPP
#define SUPPLY_CHAIN_SNAPSHOT_ADAPTER_HPP

#include "block_data.hpp"
#include "snapshot.hpp"

#include <string>
#include <vector>

schnucks::snapshot::BatchInput make_snapshot_batch_input(
    const SupplyChainBatch& batch,
    const std::vector<SupplyChainRecord>& records);

#endif
