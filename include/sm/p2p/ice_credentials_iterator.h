// libwebrtc M157's p2p/base/ice_credentials_iterator.{h,cc} (compare/webrtc/reference/p2p/base/): hands out the
// pooled sessions' ICE credentials (PortAllocator::GetPooledIceCredentials), newest first, then random ones (#1002).
#pragma once

#include <vector>

#include "sm/p2p/port_allocator.h"
#include "sm/p2p/transport_description.h"

namespace sm::p2p {

class IceCredentialsIterator {
 public:
  explicit IceCredentialsIterator(const std::vector<IceParameters>& pooled_credentials)
      : pooled_ice_credentials_(pooled_credentials) {}
  virtual ~IceCredentialsIterator() = default;

  // Get next pooled ice credentials. Returns a new random credential if the pool is empty.
  IceParameters GetIceCredentials() {
    if (pooled_ice_credentials_.empty()) return CreateRandomIceCredentials();
    IceParameters credentials = pooled_ice_credentials_.back();
    pooled_ice_credentials_.pop_back();
    return credentials;
  }

  static IceParameters CreateRandomIceCredentials() {
    return p2p::CreateRandomIceCredentials();  // ICE_UFRAG_LENGTH, ICE_PWD_LENGTH
  }

 private:
  std::vector<IceParameters> pooled_ice_credentials_;
};

}  // namespace sm::p2p
