#pragma once
// rtc_base/unique_id_generator's UniqueRandomIdGenerator (C6 part 2, #1063): random non-zero 32-bit ids, never the
// same one twice (SSRCs for the m= sections). Not in compare/webrtc/reference/; it draws from
// sm::tls::CreateRandomNonZeroId as libwebrtc draws from its crypto random source.
#include <cstdint>
#include <set>
#include <vector>

#include "sm/tls/crypto_random.h"

namespace sm::pc {

class UniqueRandomIdGenerator {
 public:
  UniqueRandomIdGenerator() = default;
  explicit UniqueRandomIdGenerator(const std::vector<uint32_t>& known_ids) : known_ids_(known_ids.begin(), known_ids.end()) {}
  uint32_t GenerateId() {
    while (true) {
      auto pair = known_ids_.insert(tls::CreateRandomNonZeroId());
      if (pair.second) return *pair.first;
    }
  }
  bool AddKnownId(uint32_t value) { return known_ids_.insert(value).second; }

 private:
  std::set<uint32_t> known_ids_;
};

}  // namespace sm::pc
