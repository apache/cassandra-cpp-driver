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

#ifndef DATASTAX_INTERNAL_PROTOCOL_HPP
#define DATASTAX_INTERNAL_PROTOCOL_HPP

#include "cassandra.h"
#include "constants.hpp"
#include "string.hpp"

namespace datastax { namespace internal { namespace core {

/**
 * A type that represents the protocol version for Cassandra/DSE.
 */
class ProtocolVersion {
public:
  /**
   * Constructs an invalid (uninitialized) protocol version.
   */
  ProtocolVersion();

  /**
   * Constructs a protocol version from a value. Use `is_valid()` to check if
   * the value is valid.
   *
   * @see is_valid()
   *
   * @param value The value to use for the protocol version.
   */
  ProtocolVersion(int value);

public:
  /**
   * Returns the lowest supported protocol version.
   *
   * @return The lowest protocol version.
   */
  static ProtocolVersion lowest_supported();

  /**
   * Returns the highest supported protocol version.
   *
   * @param is_dse If true the highest DSE protocol version is returned; otherwise the highest
   * Apache Cassandra version is returned.
   * @return The highest protocol version.
   */
  static ProtocolVersion highest_supported(bool is_dse = true);

  /**
   * Returns the newest supported beta protocol version.
   *
   * @return The newest beta protocol version.
   */
  static ProtocolVersion newest_beta();

public:
  /**
   * Get the raw value for the protocol version.
   *
   * @return The protocol version value.
   */
  int value() const;

  /**
   * Check to see if the protocol version's value is valid. Beta versions
   * are valid but will return false. Use `is_beta()` for beta versions.
   *
   * @see is_beta()
   *
   * @return true if valid, otherwise false.
   */
  bool is_valid() const;

/**
   * Check to see if the protocol version is a beta version. Beta versions
   * require the USE_BETA frame flag and are not reported as valid.
   *
   * @return true if a beta version, otherwise false.
   */
  bool is_beta() const;

  /**
   * Check to see if the protocol version is DSE.
   *
   * @return true if DSE, otherwise false;
   */
  bool is_dse() const;

  /**
   * Returns the string representation for the protocol version.
   *
   * @return A protocol version string.
   */
  String to_string() const;

  /**
   * Attempt to lower the protocol version.
   *
   * @return The previous protocol version if valid; otherwise an invalid protocol object (Use:
   * is_valid())
   */
  ProtocolVersion previous() const;

public:
  /**
   * Check to see if the set keyspace operation is supported by the current
   * protocol version.
   *
   * @return true if supported, otherwise false.
   */
  bool supports_set_keyspace() const;

  /**
   * Check to see if result metadata IDs are supported by the current protocol
   * version.
   *
   * @return true if supported, otherwise false.
   */
  bool supports_result_metadata_id() const;

  /**
   * Check to see if the v5 frame format (framed envelopes with a CRC24 header
   * checksum and a CRC32 payload trailer) is supported by the current protocol
   * version.
   *
   * @return true if supported, otherwise false.
   */
  bool supports_framing() const;

  /**
   * The wire size of the query/batch <flags> field. Protocol v5 widened this
   * from [byte] to [int].
   *
   * @return The number of bytes used to encode <flags>.
   */
  size_t query_flags_size() const;

  /**
   * Check to see if the With_now_in_seconds query flag is supported by the
   * current protocol version.
   *
   * @return true if supported, otherwise false.
   */
  bool supports_now_in_seconds() const;

public:
  bool operator<(ProtocolVersion version) const { return value_ < version.value_; }
  bool operator>(ProtocolVersion version) const { return value_ > version.value_; }
  bool operator<=(ProtocolVersion version) const { return value_ <= version.value_; }
  bool operator>=(ProtocolVersion version) const { return value_ >= version.value_; }
  bool operator==(ProtocolVersion version) const { return value_ == version.value_; }
  bool operator!=(ProtocolVersion version) const { return value_ != version.value_; }

private:
  int value_;
};

}}} // namespace datastax::internal::core

#endif
