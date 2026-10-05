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

#include "protocol.hpp"

#include "cassandra.h"

#define DSE_PROTOCOL_VERSION_BIT 0x40
#define DSE_PROTOCOL_VERSION_MASK 0x3F

using namespace datastax;
using namespace datastax::internal::core;

static bool is_protocol_at_least_v5_or_dse_v2(int version) {
  if (version & DSE_PROTOCOL_VERSION_BIT) {
    return version >= CASS_PROTOCOL_VERSION_DSEV2;
  } else {
    return version >= CASS_PROTOCOL_VERSION_V5;
  }
}

ProtocolVersion::ProtocolVersion()
    : value_(-1) {}

ProtocolVersion::ProtocolVersion(int value)
    : value_(value) {}

ProtocolVersion ProtocolVersion::lowest_supported() {
  return ProtocolVersion(CASS_PROTOCOL_VERSION_V3);
}

ProtocolVersion ProtocolVersion::highest_supported(bool is_dse) {
  return ProtocolVersion(is_dse ? CASS_PROTOCOL_VERSION_DSEV2 : CASS_PROTOCOL_VERSION_V5);
}

ProtocolVersion ProtocolVersion::newest_beta() { return ProtocolVersion(CASS_PROTOCOL_VERSION_V5); }

int ProtocolVersion::value() const { return value_; }

bool ProtocolVersion::is_valid() const {
  return *this >= lowest_supported() && *this <= highest_supported(is_dse());
}

bool ProtocolVersion::is_dse() const { return (value_ & DSE_PROTOCOL_VERSION_BIT) != 0; }

// A version is beta only while it is newer than the highest GA version, i.e.
// while the server may still require the USE_BETA frame flag to accept it.
// Protocol v5 graduated to GA in Cassandra 4.0, so this is currently always
// false; the check is kept so that a future beta version is handled correctly.
bool ProtocolVersion::is_beta() const { return *this > highest_supported(is_dse()); }

bool ProtocolVersion::supports_framing() const {
  assert(value_ > 0 && "Invalid protocol version");
  return is_protocol_at_least_v5_or_dse_v2(value_);
}

size_t ProtocolVersion::query_flags_size() const {
  // Protocol v5 widened <flags> from [byte] to [int] to make room for
  // With_now_in_seconds (0x0100).
  return supports_framing() ? sizeof(int32_t) : sizeof(uint8_t);
}

bool ProtocolVersion::supports_now_in_seconds() const {
  assert(value_ > 0 && "Invalid protocol version");
  // With_now_in_seconds only exists in the Apache Cassandra v5 protocol. DSE v2
  // shares the v5 framing and metadata changes but not this flag.
  return !is_dse() && value_ >= CASS_PROTOCOL_VERSION_V5;
}

String ProtocolVersion::to_string() const {
  if (value_ > 0) {
    OStringStream ss;
    if (is_dse()) {
      ss << "DSEv" << (value_ & DSE_PROTOCOL_VERSION_MASK);
    } else {
      ss << "v" << value_;
    }
    return ss.str();
  } else {
    return "<invalid>";
  }
}

ProtocolVersion ProtocolVersion::previous() const {
  if (*this <= lowest_supported()) {
    return ProtocolVersion(); // Invalid
  } else if (is_dse() && value_ <= CASS_PROTOCOL_VERSION_DSEV1) {
    // Start trying Cassandra protocol versions
    return ProtocolVersion::highest_supported(false);
  } else {
    return ProtocolVersion(value_ - 1);
  }
}

bool ProtocolVersion::supports_set_keyspace() const {
  assert(value_ > 0 && "Invalid protocol version");
  return is_protocol_at_least_v5_or_dse_v2(value_);
}

bool ProtocolVersion::supports_result_metadata_id() const {
  assert(value_ > 0 && "Invalid protocol version");
  return is_protocol_at_least_v5_or_dse_v2(value_);
}
