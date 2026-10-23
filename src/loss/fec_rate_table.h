// See fec_rate_table.cc.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sm::loss::internal {

inline constexpr size_t kFecRateTableSize = 6450;
extern const uint8_t kFecRateTable[kFecRateTableSize];

}  // namespace sm::loss::internal
