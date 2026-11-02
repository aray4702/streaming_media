// a=simulcast and a=rid (#1062, C6 part 1): libwebrtc M157's pc/simulcast_description.{h,cc} and
// pc/simulcast_sdp_serializer.{h,cc} (compare/webrtc/reference/pc/; simulcast_description.h is not there, so the
// declarations follow the .cc and M157's header).
//
// Difference from libwebrtc: SerializeRidDescription matches a RID's codecs to the m= section's by name
// (case-insensitive), clock rate, channels and fmtp parameters, with the SDP defaults filled in (VP9 profile-id,
// H.264 packetization-mode, AV1 profile, H.265 tx-mode) and the level ones left out (profile-level-id, level-id,
// level-idx, tier), standing in for IsSameRtpCodecIgnoringLevel (media/base/codec_comparators, C6 part 2).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sm/pc/stream_params.h"

namespace sm::pc {

class MediaContentDescription;

// One RID in a=simulcast, "~" when paused.
struct SimulcastLayer final {
  SimulcastLayer(std::string_view rid, bool is_paused) : rid(rid), is_paused(is_paused) {}
  bool operator==(const SimulcastLayer& other) const { return rid == other.rid && is_paused == other.is_paused; }

  std::string rid;
  bool is_paused;
};

// The layers of one direction: alternatives separated by ',' within a layer, layers by ';'.
class SimulcastLayerList final {
 public:
  void AddLayer(const SimulcastLayer& layer) { list_.push_back({layer}); }
  void AddLayerWithAlternatives(const std::vector<SimulcastLayer>& rids) { list_.push_back(rids); }
  const std::vector<SimulcastLayer>& operator[](size_t index) const { return list_[index]; }

  using const_iterator = std::vector<std::vector<SimulcastLayer>>::const_iterator;
  const_iterator begin() const { return list_.begin(); }
  const_iterator end() const { return list_.end(); }
  size_t size() const { return list_.size(); }
  bool empty() const { return list_.empty(); }

  // Every layer, alternatives included, in order.
  std::vector<SimulcastLayer> GetAllLayers() const;

 private:
  std::vector<std::vector<SimulcastLayer>> list_;
};

class SimulcastDescription final {
 public:
  const SimulcastLayerList& send_layers() const { return send_layers_; }
  SimulcastLayerList& send_layers() { return send_layers_; }
  const SimulcastLayerList& receive_layers() const { return receive_layers_; }
  SimulcastLayerList& receive_layers() { return receive_layers_; }
  bool empty() const { return send_layers_.empty() && receive_layers_.empty(); }

 private:
  SimulcastLayerList send_layers_;
  SimulcastLayerList receive_layers_;
};

inline constexpr size_t kMaxSimulcastRids = 16;

// Errors are libwebrtc's RTCError(SYNTAX_ERROR) messages, in `error`.
class SimulcastSdpSerializer {
 public:
  std::string SerializeSimulcastDescription(const SimulcastDescription& simulcast) const;
  std::optional<SimulcastDescription> DeserializeSimulcastDescription(std::string_view string,
                                                                      std::string* error = nullptr) const;
  std::string SerializeRidDescription(const MediaContentDescription& media_desc,
                                      const RidDescription& rid_description) const;
  std::optional<RidDescription> DeserializeRidDescription(const MediaContentDescription& media_desc,
                                                          std::string_view string, std::string* error = nullptr) const;
};

// api/rtp_parameters / rtp_header_extensions' IsLegalRsidName: 1-16 alphanumeric characters.
bool IsLegalRsidName(std::string_view name);

}  // namespace sm::pc
