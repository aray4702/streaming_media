// libwebrtc M157's ULPFEC packet mask tables (fec_private_tables_random.h,
// fec_private_tables_bursty.h). The random masks favour recovery from random
// loss; the bursty ones recover every run of up to m consecutive losses. Both
// cover up to 12 media packets; larger codes are generated at run time
// (PacketMaskTable::LookUp).
#pragma once

#include <cstdint>

namespace sm::loss::ulpfec::internal {

extern const uint8_t kPacketMaskRandomTbl[];
extern const uint8_t kPacketMaskBurstyTbl[];

}  // namespace sm::loss::ulpfec::internal
