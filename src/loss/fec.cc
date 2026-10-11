#include "sm/loss/fec.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace sm::loss {

namespace {
constexpr size_t kFecHeader = 2 + 8 + 2 + 4 + 1;

void Put(std::vector<uint8_t>& b, uint64_t v, int bytes) {
  for (int i = bytes - 1; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
uint64_t Get(const uint8_t* p, int bytes) {
  uint64_t v = 0;
  for (int i = 0; i < bytes; ++i) v = (v << 8) | p[i];
  return v;
}
}  // namespace

int FecPacketsFor(int k, double protection) {
  if (k <= 0 || protection <= 0) return 0;
  return std::clamp(static_cast<int>(std::ceil(k * protection - 1e-9)), 1, k);
}

uint64_t FecMask(int k, int m, int j, FecMaskType type) {
  uint64_t mask = 0;
  if (type == FecMaskType::kInterleaved) {
    for (int i = j; i < k; i += m) mask |= uint64_t{1} << i;
  } else {
    int lo = j * k / m;
    int hi = (j + 1) * k / m;
    for (int i = lo; i < hi; ++i) mask |= uint64_t{1} << i;
  }
  return mask;
}

// --- Encoder -----------------------------------------------------------------

FecEncoder::FecEncoder(FecConfig cfg) : cfg_(cfg) {}

std::vector<RtpPacket> FecEncoder::AddMediaPacket(const RtpPacket& p, bool end_of_group) {
  ++stats_.media_packets;
  stats_.media_bytes += static_cast<int64_t>(p.size());
  if (params_.protection <= 0) return {};
  group_.push_back(p);
  if (end_of_group || static_cast<int>(group_.size()) >= cfg_.max_group_size) return Flush();
  return {};
}

std::vector<RtpPacket> FecEncoder::Flush() {
  std::vector<RtpPacket> out;
  const int k = static_cast<int>(group_.size());
  const int m = FecPacketsFor(k, params_.protection);
  const uint16_t base = group_.front().seq;
  for (int j = 0; j < m; ++j) {
    uint64_t mask = FecMask(k, m, j, params_.mask);
    uint16_t len_xor = 0;
    uint32_t ts_xor = 0;
    uint8_t ptm_xor = 0;
    std::vector<uint8_t> pay;
    for (int i = 0; i < k; ++i) {
      if (!(mask >> i & 1)) continue;
      const RtpPacket& mp = group_[static_cast<size_t>(i)];
      len_xor ^= static_cast<uint16_t>(mp.payload.size());
      ts_xor ^= mp.timestamp;
      ptm_xor ^= static_cast<uint8_t>((mp.marker ? 0x80 : 0) | mp.payload_type);
      if (pay.size() < mp.payload.size()) pay.resize(mp.payload.size(), 0);
      for (size_t b = 0; b < mp.payload.size(); ++b) pay[b] ^= mp.payload[b];
    }
    RtpPacket f;
    f.ssrc = cfg_.fec_ssrc;
    f.payload_type = cfg_.fec_payload_type;
    f.seq = fec_seq_++;
    f.timestamp = group_.back().timestamp;
    f.payload.reserve(kFecHeader + pay.size());
    Put(f.payload, base, 2);
    Put(f.payload, mask, 8);
    Put(f.payload, len_xor, 2);
    Put(f.payload, ts_xor, 4);
    Put(f.payload, ptm_xor, 1);
    f.payload.insert(f.payload.end(), pay.begin(), pay.end());
    ++stats_.fec_packets;
    stats_.fec_bytes += static_cast<int64_t>(f.size());
    out.push_back(std::move(f));
  }
  group_.clear();
  return out;
}

void FecEncoder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("media_packets", stats_.media_packets);
  w.Counter("fec_packets", stats_.fec_packets);
  w.Counter("media_bytes", static_cast<uint64_t>(stats_.media_bytes));
  w.Counter("fec_bytes", static_cast<uint64_t>(stats_.fec_bytes));
  w.Gauge("protection", params_.protection);
  w.Gauge("overhead", stats_.media_bytes ? static_cast<double>(stats_.fec_bytes) / stats_.media_bytes : 0.0);
}

// --- Decoder -----------------------------------------------------------------

FecDecoder::FecDecoder(FecConfig cfg, size_t window) : cfg_(cfg), window_(window) {}

void FecDecoder::Store(const RtpPacket& p, int64_t useq) {
  media_.emplace(useq, p);
  while (media_.size() > window_) media_.erase(media_.begin());
  // Drop parity packets that only protect packets older than the window.
  int64_t oldest = media_.begin()->first;
  while (!parity_.empty() && parity_.front().base + 64 < oldest) {
    ++stats_.fec_useless;
    parity_.pop_front();
  }
}

std::vector<RtpPacket> FecDecoder::OnMediaPacket(const RtpPacket& p) {
  int64_t u = unwrapper_.Unwrap(p.seq);
  if (media_.count(u)) return {};
  Store(p, u);
  return parity_.empty() ? std::vector<RtpPacket>{} : TryRecover();
}

std::vector<RtpPacket> FecDecoder::OnFecPacket(const RtpPacket& fec) {
  if (fec.payload.size() < kFecHeader) return {};
  ++stats_.fec_received;
  const uint8_t* d = fec.payload.data();
  Parity par;
  uint16_t base16 = static_cast<uint16_t>(Get(d, 2));
  par.base = unwrapper_.PeekUnwrap(base16);
  par.mask = Get(d + 2, 8);
  par.len_xor = static_cast<uint16_t>(Get(d + 10, 2));
  par.ts_xor = static_cast<uint32_t>(Get(d + 12, 4));
  par.pt_marker_xor = static_cast<uint8_t>(Get(d + 16, 1));
  par.payload_xor.assign(fec.payload.begin() + kFecHeader, fec.payload.end());
  par.ssrc = 0;
  if (!media_.empty()) par.ssrc = media_.begin()->second.ssrc;
  parity_.push_back(std::move(par));
  if (parity_.size() > 256) {
    parity_.pop_front();
    ++stats_.fec_useless;
  }
  return TryRecover();
}

std::vector<RtpPacket> FecDecoder::TryRecover() {
  std::vector<RtpPacket> recovered;
  bool progress = true;
  while (progress) {
    progress = false;
    for (auto it = parity_.begin(); it != parity_.end();) {
      int missing = 0;
      int64_t missing_seq = 0;
      for (uint64_t bits = it->mask; bits; bits &= bits - 1) {
        int i = std::countr_zero(bits);
        if (!media_.count(it->base + i)) {
          ++missing;
          missing_seq = it->base + i;
        }
      }
      if (missing == 0) {
        it = parity_.erase(it);  // Fully covered, nothing to do.
        continue;
      }
      if (missing > 1) {
        ++it;
        continue;
      }
      uint16_t len = it->len_xor;
      uint32_t ts = it->ts_xor;
      uint8_t ptm = it->pt_marker_xor;
      std::vector<uint8_t> pay = it->payload_xor;
      uint32_t ssrc = it->ssrc;
      for (uint64_t bits = it->mask; bits; bits &= bits - 1) {
        int64_t s = it->base + std::countr_zero(bits);
        if (s == missing_seq) continue;
        const RtpPacket& mp = media_.at(s);
        ssrc = mp.ssrc;
        len ^= static_cast<uint16_t>(mp.payload.size());
        ts ^= mp.timestamp;
        ptm ^= static_cast<uint8_t>((mp.marker ? 0x80 : 0) | mp.payload_type);
        for (size_t b = 0; b < mp.payload.size() && b < pay.size(); ++b) pay[b] ^= mp.payload[b];
      }
      if (len > pay.size()) {  // Corrupt parity.
        it = parity_.erase(it);
        continue;
      }
      RtpPacket r;
      r.seq = static_cast<uint16_t>(missing_seq);
      r.timestamp = ts;
      r.marker = ptm & 0x80;
      r.payload_type = ptm & 0x7f;
      r.ssrc = ssrc;
      r.payload.assign(pay.begin(), pay.begin() + len);
      media_.emplace(missing_seq, r);
      ++stats_.recovered;
      SM_EVENT(cfg_.events, Timestamp::Micros(0), "fec", "recovered", 1, r.seq);
      recovered.push_back(std::move(r));
      it = parity_.erase(it);
      progress = true;
    }
  }
  return recovered;
}

void FecDecoder::ExportMetrics(MetricsWriter& w) const {
  w.Counter("fec_received", stats_.fec_received);
  w.Counter("recovered", stats_.recovered);
  w.Counter("fec_useless", stats_.fec_useless);
  w.Gauge("pending_parity", static_cast<double>(parity_.size()));
}

}  // namespace sm::loss
