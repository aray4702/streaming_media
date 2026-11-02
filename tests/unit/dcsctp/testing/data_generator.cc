/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/testing/data_generator.cc (compare/webrtc/reference/net/dcsctp/testing/data_generator.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h, tests on sm_test.h with tests/unit/dcsctp/gmock_lite.h.
#include "testing/data_generator.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/common/internal_types.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/public/types.h"

namespace sm::dcsctp {
constexpr PPID kPpid = PPID(53);

Data DataGenerator::Ordered(std::vector<uint8_t> payload,
                            std::string_view flags,
                            const DataGeneratorOptions opts) {
  Data::IsBeginning is_beginning(flags.find('B') != std::string::npos);
  Data::IsEnd is_end(flags.find('E') != std::string::npos);

  if (is_beginning) {
    fsn_ = FSN(0);
  } else {
    fsn_ = FSN(*fsn_ + 1);
  }
  MID mid = opts.mid.value_or(mid_);
  Data ret = Data(opts.stream_id, SSN(static_cast<uint16_t>(*mid)), mid, fsn_,
                  opts.ppid, std::move(payload), is_beginning, is_end,
                  IsUnordered(false));

  if (is_end) {
    mid_ = MID(*mid + 1);
  }
  return ret;
}

Data DataGenerator::Unordered(std::vector<uint8_t> payload,
                              std::string_view flags,
                              const DataGeneratorOptions opts) {
  Data::IsBeginning is_beginning(flags.find('B') != std::string::npos);
  Data::IsEnd is_end(flags.find('E') != std::string::npos);

  if (is_beginning) {
    fsn_ = FSN(0);
  } else {
    fsn_ = FSN(*fsn_ + 1);
  }
  MID mid = opts.mid.value_or(mid_);
  Data ret = Data(opts.stream_id, SSN(0), mid, fsn_, kPpid, std::move(payload),
                  is_beginning, is_end, IsUnordered(true));
  if (is_end) {
    mid_ = MID(*mid + 1);
  }
  return ret;
}
}  // namespace sm::dcsctp
