/*
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef DATASTAX_INTERNAL_REQUEST_HPP
#define DATASTAX_INTERNAL_REQUEST_HPP

#include "buffer.hpp"
#include "cassandra.h"
#include "constants.hpp"
#include "external.hpp"
#include "macros.hpp"
#include "map.hpp"
#include "ref_counted.hpp"
#include "retry_policy.hpp"
#include "socket.hpp"
#include "string_ref.hpp"

#include <stdint.h>
#include <utility>

namespace datastax { namespace internal { namespace core {

class RequestCallback;

class CustomPayload : public RefCounted<CustomPayload> {
public:
  typedef SharedRefPtr<const CustomPayload> ConstPtr;

  virtual ~CustomPayload() {}

  void set(const char* name, size_t name_length, const uint8_t* value, size_t value_size);

  void remove(const char* name, size_t name_length) { items_.erase(String(name, name_length)); }

  int32_t encode(BufferVec* bufs) const;

  inline bool empty() const { return items_.empty(); }

  inline size_t size() const { return items_.size(); }

private:
  typedef Map<String, Buffer> ItemMap;
  ItemMap items_;
};

// A grouping of common request settings that can be easily inherited (copied).
// Important: If a member is added to this structure "cassandra.h" should also
// be updated to reflect the new inherited setting(s).
struct RequestSettings {
  RequestSettings()
      : consistency(CASS_CONSISTENCY_UNKNOWN)
      , serial_consistency(CASS_CONSISTENCY_UNKNOWN)
      , request_timeout_ms(CASS_UINT64_MAX)
      , is_idempotent(false) {}
  CassConsistency consistency;
  CassConsistency serial_consistency;
  uint64_t request_timeout_ms;
  RetryPolicy::Ptr retry_policy;
  bool is_idempotent;
  String keyspace;
};

class Request : public RefCounted<Request> {
public:
  typedef SharedRefPtr<const Request> ConstPtr;

  enum {
    REQUEST_ERROR_UNSUPPORTED_PROTOCOL = SocketRequest::SOCKET_REQUEST_ERROR_LAST_ENTRY,
    REQUEST_ERROR_BATCH_WITH_NAMED_VALUES,
    REQUEST_ERROR_PARAMETER_UNSET,
    REQUEST_ERROR_NO_AVAILABLE_STREAM_IDS,
    REQUEST_ERROR_NO_DATA_WRITTEN
  };

  Request(uint8_t opcode)
      : opcode_(opcode)
      , flags_(0)
      , timestamp_(CASS_INT64_MIN)
      , now_in_seconds_(CASS_INT32_MIN)
      , record_attempted_addresses_(false) {}

  virtual ~Request() {}

  uint8_t opcode() const { return opcode_; }

  uint8_t flags() const { return flags_; }

  void set_tracing(bool is_tracing) {
    if (is_tracing) {
      flags_ |= CASS_FLAG_TRACING;
    } else {
      flags_ &= ~CASS_FLAG_TRACING;
    }
  }

  const RequestSettings& settings() const { return settings_; }

  void set_settings(const RequestSettings& settings) { settings_ = settings; }

  CassConsistency consistency() const { return settings_.consistency; }

  void set_consistency(CassConsistency consistency) { settings_.consistency = consistency; }

  CassConsistency serial_consistency() const { return settings_.serial_consistency; }

  void set_serial_consistency(CassConsistency serial_consistency) {
    settings_.serial_consistency = serial_consistency;
  }

  uint64_t request_timeout_ms() const { return settings_.request_timeout_ms; }

  void set_request_timeout_ms(uint64_t request_timeout_ms) {
    settings_.request_timeout_ms = request_timeout_ms;
  }

  const RetryPolicy::Ptr& retry_policy() const { return settings_.retry_policy; }

  void set_retry_policy(RetryPolicy* retry_policy) { settings_.retry_policy.reset(retry_policy); }

  bool is_idempotent() const {
    // Prepare requests are idempotent and should be retried regardless of the
    // setting inherited from an existing statement.
    return opcode_ == CQL_OPCODE_PREPARE || settings_.is_idempotent;
  }

  void set_is_idempotent(bool is_idempotent) { settings_.is_idempotent = is_idempotent; }

  const String& keyspace() const { return settings_.keyspace; }

  void set_keyspace(const String& keyspace) { settings_.keyspace = keyspace; }

  int64_t timestamp() const { return timestamp_; }

  void set_timestamp(int64_t timestamp) { timestamp_ = timestamp; }

  /**
   * Protocol v5 only. Seconds since the epoch to be used as "now" for the
   * request, superseding the connection's server side time. `CASS_INT32_MIN`
   * means unset, in which case the flag is not sent.
   */
  int32_t now_in_seconds() const { return now_in_seconds_; }

  void set_now_in_seconds(int32_t now_in_seconds) { now_in_seconds_ = now_in_seconds; }

  bool record_attempted_addresses() const { return record_attempted_addresses_; }

  void set_record_attempted_addresses(bool record_attempted_addresses) {
    record_attempted_addresses_ = record_attempted_addresses;
  }

  const CustomPayload::ConstPtr& custom_payload() const { return custom_payload_; }

  bool has_custom_payload() const { return custom_payload_ || !custom_payload_extra_.empty(); }

  void set_custom_payload(const CustomPayload* payload) { custom_payload_.reset(payload); }

  void set_custom_payload(const char* key, const uint8_t* value, size_t value_len) {
    custom_payload_extra_.set(key, strlen(key), value, value_len);
  }

  bool has_execution_profile() const { return !profile_name_.empty(); }

  const String& execution_profile_name() const { return profile_name_; }

  void set_execution_profile_name(const String& name) { profile_name_ = name; }

  int32_t encode_custom_payload(BufferVec* bufs) const {
    int32_t length = sizeof(uint16_t);
    uint16_t count = 0;

    Buffer buf(sizeof(uint16_t));
    count += custom_payload_ ? custom_payload_->size() : 0;
    count += custom_payload_extra_.size();
    buf.encode_uint16(0, count);
    bufs->push_back(buf);

    if (custom_payload_) {
      length += custom_payload_->encode(bufs);
    }
    length += custom_payload_extra_.encode(bufs);
    return length;
  }

  void set_host(const Address& host) { host_.reset(new Address(host)); }
  const Address* host() const { return host_.get(); }

  virtual int encode(ProtocolVersion version, RequestCallback* callback, BufferVec* bufs) const = 0;

private:
  uint8_t opcode_;
  uint8_t flags_;
  RequestSettings settings_;
  int64_t timestamp_;
  int32_t now_in_seconds_;
  bool record_attempted_addresses_;
  CustomPayload::ConstPtr custom_payload_;
  CustomPayload custom_payload_extra_;
  String profile_name_;
  ScopedPtr<Address> host_;

private:
  DISALLOW_COPY_AND_ASSIGN(Request);
};

class RoutableRequest : public Request {
public:
  RoutableRequest(uint8_t opcode)
      : Request(opcode) {}

  virtual bool get_routing_key(String* routing_key) const = 0;
};

}}} // namespace datastax::internal::core

EXTERNAL_TYPE(datastax::internal::core::CustomPayload, CassCustomPayload)

#endif
