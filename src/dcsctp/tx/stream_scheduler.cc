/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
// Ported from libwebrtc M157 net/dcsctp/tx/stream_scheduler.cc (compare/webrtc/reference/net/dcsctp/tx/stream_scheduler.cc) for #1060 (C7
// part 2): namespace sm::dcsctp, rtc_base pieces from sm/dcsctp/rtc_compat.h.
#include "sm/dcsctp/tx/stream_scheduler.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>

#include "sm/core/units.h"
#include "sm/dcsctp/rtc_compat.h"
#include "sm/dcsctp/packet/data.h"
#include "sm/dcsctp/public/types.h"
#include "sm/dcsctp/tx/send_queue.h"

namespace sm::dcsctp {

void StreamScheduler::Stream::SetPriority(StreamPriority priority) {
  priority_ = priority;
  inverse_weight_ = InverseWeight(priority);
}

std::optional<SendQueue::DataToSend> StreamScheduler::Produce(
    Timestamp now,
    size_t max_size) {
  // For non-interleaved streams, avoid rescheduling while still sending a
  // message as it needs to be sent in full. For interleaved messaging,
  // reschedule for every I-DATA chunk sent.
  bool rescheduling =
      enable_message_interleaving_ || !currently_sending_a_message_;

  SM_DCSCTP_DLOG << log_prefix_
                       << "Producing data, rescheduling=" << rescheduling
                       << ", active="
                       << StrJoin(
                              active_streams_, ", ",
                              [&](StringBuilder& sb, const auto& p) {
                                sb << *p->stream_id() << "@"
                                   << *p->next_finish_time();
                              });

  SM_DCSCTP_DCHECK(rescheduling || current_stream_ != nullptr);

  std::optional<SendQueue::DataToSend> data;
  while (!data.has_value() && !active_streams_.empty()) {
    if (rescheduling) {
      auto it = active_streams_.begin();
      current_stream_ = *it;
      SM_DCSCTP_DLOG << log_prefix_ << "Rescheduling to stream "
                           << *current_stream_->stream_id();

      active_streams_.erase(it);
      current_stream_->ForceMarkInactive();
    } else {
      SM_DCSCTP_DLOG << log_prefix_ << "Producing from previous stream: "
                           << *current_stream_->stream_id();
      SM_DCSCTP_DCHECK(c_any_of(active_streams_, [this](const auto* p) {
        return p == current_stream_;
      }));
    }

    data = current_stream_->Produce(now, max_size);
  }

  if (!data.has_value()) {
    SM_DCSCTP_DLOG
        << log_prefix_
        << "There is no stream with data; Can't produce any data.";
    SM_DCSCTP_DCHECK(IsConsistent());

    return std::nullopt;
  }

  SM_DCSCTP_DCHECK(data->data.stream_id == current_stream_->stream_id());

  SM_DCSCTP_DLOG << log_prefix_ << "Producing DATA, type="
                       << (data->data.is_unordered ? "unordered" : "ordered")
                       << "::"
                       << (*data->data.is_beginning && *data->data.is_end
                               ? "complete"
                           : *data->data.is_beginning ? "first"
                           : *data->data.is_end       ? "last"
                                                      : "middle")
                       << ", stream_id=" << *current_stream_->stream_id()
                       << ", ppid=" << *data->data.ppid
                       << ", length=" << data->data.payload.size();

  currently_sending_a_message_ = !*data->data.is_end;
  virtual_time_ = current_stream_->current_time();

  // One side-effect of rescheduling is that the new stream will not be present
  // in `active_streams`.
  size_t bytes_to_send_next = current_stream_->bytes_to_send_in_next_message();
  if (rescheduling && bytes_to_send_next > 0) {
    current_stream_->MakeActive(bytes_to_send_next);
  } else if (!rescheduling && bytes_to_send_next == 0) {
    current_stream_->MakeInactive();
  }

  SM_DCSCTP_DCHECK(IsConsistent());
  return data;
}

StreamScheduler::VirtualTime StreamScheduler::Stream::CalculateFinishTime(
    size_t bytes_to_send_next) const {
  if (parent_.enable_message_interleaving_) {
    // Perform weighted fair queuing scheduling.
    return VirtualTime(*current_virtual_time_ +
                       bytes_to_send_next * *inverse_weight_);
  }

  // Perform round-robin scheduling by letting the stream have its next virtual
  // finish time in the future. It doesn't matter how far into the future, just
  // any positive number so that any other stream that has the same virtual
  // finish time as this stream gets to produce their data before revisiting
  // this stream.
  return VirtualTime(*current_virtual_time_ + 1);
}

std::optional<SendQueue::DataToSend> StreamScheduler::Stream::Produce(
    Timestamp now,
    size_t max_size) {
  std::optional<SendQueue::DataToSend> data = producer_.Produce(now, max_size);

  if (data.has_value()) {
    VirtualTime new_current = CalculateFinishTime(data->data.payload.size());
    SM_DCSCTP_DLOG << parent_.log_prefix_
                         << "Virtual time changed: " << *current_virtual_time_
                         << " -> " << *new_current;
    current_virtual_time_ = new_current;
  }

  return data;
}

bool StreamScheduler::IsConsistent() const {
  for (Stream* stream : active_streams_) {
    if (stream->next_finish_time_ == VirtualTime::Zero()) {
      SM_DCSCTP_DLOG << log_prefix_ << "Stream " << *stream->stream_id()
                           << " is active, but has no next-finish-time";
      return false;
    }
  }
  return true;
}

void StreamScheduler::Stream::MaybeMakeActive() {
  SM_DCSCTP_DLOG << parent_.log_prefix_ << "MaybeMakeActive("
                       << *stream_id() << ")";
  SM_DCSCTP_DCHECK(next_finish_time_ == VirtualTime::Zero());
  size_t bytes_to_send_next = bytes_to_send_in_next_message();
  if (bytes_to_send_next == 0) {
    return;
  }

  MakeActive(bytes_to_send_next);
}

void StreamScheduler::Stream::MakeActive(size_t bytes_to_send_next) {
  current_virtual_time_ = parent_.virtual_time_;
  SM_DCSCTP_DCHECK_GT(bytes_to_send_next, 0);
  VirtualTime next_finish_time = CalculateFinishTime(
      std::min(bytes_to_send_next, parent_.max_payload_bytes_));
  SM_DCSCTP_DCHECK_GT(*next_finish_time, 0);
  SM_DCSCTP_DLOG << parent_.log_prefix_ << "Making stream "
                       << *stream_id() << " active, expiring at "
                       << *next_finish_time;
  SM_DCSCTP_DCHECK(next_finish_time_ == VirtualTime::Zero());
  next_finish_time_ = next_finish_time;
  SM_DCSCTP_DCHECK(!c_any_of(parent_.active_streams_,
                             [this](const auto* p) { return p == this; }));
  parent_.active_streams_.emplace(this);
}

void StreamScheduler::Stream::ForceMarkInactive() {
  SM_DCSCTP_DLOG << parent_.log_prefix_ << "Making stream "
                       << *stream_id() << " inactive";
  SM_DCSCTP_DCHECK(next_finish_time_ != VirtualTime::Zero());
  next_finish_time_ = VirtualTime::Zero();
}

void StreamScheduler::Stream::MakeInactive() {
  ForceMarkInactive();
  std::erase_if(parent_.active_streams_,
                  [&](const auto* s) { return s == this; });
}

std::set<StreamID> StreamScheduler::ActiveStreamsForTesting() const {
  std::set<StreamID> stream_ids;
  for (const auto& stream : active_streams_) {
    stream_ids.insert(stream->stream_id());
  }
  return stream_ids;
}

}  // namespace sm::dcsctp
