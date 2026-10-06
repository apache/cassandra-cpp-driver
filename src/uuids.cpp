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

#include "uuids.hpp"

#include "cassandra.h"
#include "external.hpp"
#include "get_time.hpp"
#include "logger.hpp"
#include "md5.hpp"
#include "scoped_lock.hpp"
#include "serialization.hpp"

#include <ctype.h>
#include <stdio.h>

#define TIME_OFFSET_BETWEEN_UTC_AND_EPOCH 0x01B21DD213814000LL // Nanoseconds
#define MIN_CLOCK_SEQ_AND_NODE 0x8080808080808080LL
#define MAX_CLOCK_SEQ_AND_NODE 0x7f7f7f7f7f7f7f7fLL

using namespace datastax::internal;
using namespace datastax::internal::core;

static uint64_t to_milliseconds(uint64_t timestamp) { return timestamp / 10000L; }

static uint64_t from_unix_timestamp(uint64_t timestamp) {
  return (timestamp * 10000L) + TIME_OFFSET_BETWEEN_UTC_AND_EPOCH;
}

static uint64_t set_version(uint64_t timestamp, uint8_t version) {
  return (timestamp & 0x0FFFFFFFFFFFFFFFLL) | (static_cast<uint64_t>(version) << 60);
}

bool operator<(const CassUuid& uuid1, const CassUuid& uuid2) {
  if (uuid1.time_and_version == uuid2.time_and_version)
    return uuid1.clock_seq_and_node < uuid2.clock_seq_and_node;
  else
    return uuid1.time_and_version < uuid2.time_and_version;
}

bool operator==(const CassUuid& uuid1, const CassUuid& uuid2) {
  return uuid1.time_and_version == uuid2.time_and_version
    && uuid1.clock_seq_and_node == uuid2.clock_seq_and_node;
}

extern "C" {

CassUuidGen* cass_uuid_gen_new() { return CassUuidGen::to(new UuidGen()); }

CassUuidGen* cass_uuid_gen_new_with_node(cass_uint64_t node) {
  return CassUuidGen::to(new UuidGen(node));
}

void cass_uuid_gen_free(CassUuidGen* uuid_gen) { delete uuid_gen->from(); }

void cass_uuid_gen_time(CassUuidGen* uuid_gen, CassUuid* output) {
  uuid_gen->generate_time(output);
}

void cass_uuid_gen_random(CassUuidGen* uuid_gen, CassUuid* output) {
  uuid_gen->generate_random(output);
}

void cass_uuid_gen_from_time(CassUuidGen* uuid_gen, cass_uint64_t timestamp, CassUuid* output) {
  uuid_gen->from_time(timestamp, output);
}

void cass_uuid_min_from_time(cass_uint64_t timestamp, CassUuid* output) {
  output->time_and_version = set_version(from_unix_timestamp(timestamp), 1);
  output->clock_seq_and_node = MIN_CLOCK_SEQ_AND_NODE;
}

void cass_uuid_max_from_time(cass_uint64_t timestamp, CassUuid* output) {
  output->time_and_version = set_version(from_unix_timestamp(timestamp), 1);
  output->clock_seq_and_node = MAX_CLOCK_SEQ_AND_NODE;
}

cass_uint64_t cass_uuid_timestamp(CassUuid uuid) {
  uint64_t timestamp = uuid.time_and_version & 0x0FFFFFFFFFFFFFFFLL; // Clear version
  return to_milliseconds(timestamp - TIME_OFFSET_BETWEEN_UTC_AND_EPOCH);
}

cass_uint8_t cass_uuid_version(CassUuid uuid) { return (uuid.time_and_version >> 60) & 0x0F; }

void cass_uuid_string(CassUuid uuid, char* output) {
  size_t pos = 0;
  char encoded[16];
  encode_uuid(encoded, uuid);
  static const char half_byte_to_hex[] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
  for (size_t i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      output[pos++] = '-';
    }
    uint8_t byte = static_cast<uint8_t>(encoded[i]);
    output[pos++] = half_byte_to_hex[(byte >> 4) & 0x0F];
    output[pos++] = half_byte_to_hex[byte & 0x0F];
  }
  output[pos] = '\0';
}

CassError cass_uuid_from_string(const char* str, CassUuid* output) {
  if (str == NULL) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  return cass_uuid_from_string_n(str, strlen(str), output);
}

CassError cass_uuid_from_string_n(const char* str, size_t str_length, CassUuid* output) {
  const char* pos = str;
  char buf[16];

  // clang-format off
  static const signed char hex_to_half_byte[256] = {
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
     0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  10,  11,  12,  13,  14,  15,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  10,  11,  12,  13,  14,  15,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
    -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,
  };
  // clang-format on

  if (str == NULL || str_length != 36) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  const char* end = str + 36;
  for (size_t i = 0; i < 16; ++i) {
    if (pos < end && *pos == '-') pos++;
    if (pos + 2 > end) {
      return CASS_ERROR_LIB_BAD_PARAMS;
    }
    uint8_t p0 = static_cast<uint8_t>(pos[0]);
    uint8_t p1 = static_cast<uint8_t>(pos[1]);
    if (hex_to_half_byte[p0] == -1 || hex_to_half_byte[p1] == -1) {
      return CASS_ERROR_LIB_BAD_PARAMS;
    }
    buf[i] = static_cast<char>(hex_to_half_byte[p0] << 4) + hex_to_half_byte[p1];
    pos += 2;
  }

  decode_uuid(buf, output);

  return CASS_OK;
}

} // extern "C"

UuidGen::UuidGen()
    : clock_seq_and_node_(0)
    , last_timestamp_(0LL)
    , ng_(get_random_seed(MT19937_64::DEFAULT_SEED)) {
  uv_mutex_init(&mutex_);

  Md5 md5;
  bool has_unique = false;
  uv_interface_address_t* addresses;
  int address_count;

  if (uv_interface_addresses(&addresses, &address_count) == 0) {
    for (int i = 0; i < address_count; ++i) {
      char buf[256];
      uv_interface_address_t address = addresses[i];
      md5.update(reinterpret_cast<const uint8_t*>(address.name), strlen(address.name));
      if (address.address.address4.sin_family == AF_INET) {
        uv_ip4_name(&address.address.address4, buf, sizeof(buf));
        md5.update(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
        has_unique = true;
      } else if (address.address.address4.sin_family == AF_INET6) {
        uv_ip6_name(&address.address.address6, buf, sizeof(buf));
        md5.update(reinterpret_cast<const uint8_t*>(buf), strlen(buf));
        has_unique = true;
      }
    }
    uv_free_interface_addresses(addresses, address_count);
  }

  uint64_t node = 0;
  if (has_unique) {
    uv_cpu_info_t* cpu_infos;
    int cpu_count;
    if (uv_cpu_info(&cpu_infos, &cpu_count) == 0) {
      for (int i = 0; i < cpu_count; ++i) {
        uv_cpu_info_t cpu_info = cpu_infos[i];
        md5.update(reinterpret_cast<const uint8_t*>(cpu_info.model), strlen(cpu_info.model));
      }
      uv_free_cpu_info(cpu_infos, cpu_count);
    }

    // Tack on the pid
    int32_t pid = get_pid();
    md5.update(reinterpret_cast<const uint8_t*>(&pid), 4);

    uint8_t hash[16];
    md5.final(hash);

    for (int i = 0; i < 6; ++i) {
      node |= (0x00000000000000FFLL & (long)hash[i]) << (i * 8);
    }
  } else {
    LOG_INFO("Unable to determine unique data for this node. Generating a random node value.");
    node = ng_() & 0x0000FFFFFFFFFFFFLL;
  }

  node |= 0x0000010000000000LL; // Multicast bit

  set_clock_seq_and_node(node);
}

UuidGen::UuidGen(uint64_t node)
    : clock_seq_and_node_(0)
    , last_timestamp_(0LL)
    , ng_(get_random_seed(MT19937_64::DEFAULT_SEED)) {
  uv_mutex_init(&mutex_);
  set_clock_seq_and_node(node & 0x0000FFFFFFFFFFFFLL);
}

UuidGen::~UuidGen() { uv_mutex_destroy(&mutex_); }

void UuidGen::generate_time(CassUuid* output) {
  output->time_and_version = set_version(monotonic_timestamp(), 1);
  output->clock_seq_and_node = clock_seq_and_node_;
}

void UuidGen::from_time(uint64_t timestamp, CassUuid* output) {
  output->time_and_version = set_version(from_unix_timestamp(timestamp), 1);
  output->clock_seq_and_node = clock_seq_and_node_;
}

void UuidGen::generate_random(CassUuid* output) {
  ScopedMutex lock(&mutex_);
  uint64_t time_and_version = ng_();
  uint64_t clock_seq_and_node = ng_();
  lock.unlock();

  output->time_and_version = set_version(time_and_version, 4);
  output->clock_seq_and_node =
      (clock_seq_and_node & 0x3FFFFFFFFFFFFFFFLL) | 0x8000000000000000LL; // RFC4122 variant
}

void UuidGen::set_clock_seq_and_node(uint64_t node) {
  uint64_t clock_seq = ng_();
  clock_seq_and_node_ |= (clock_seq & 0x0000000000003FFFLL) << 48;
  clock_seq_and_node_ |= 0x8000000000000000LL; // RFC4122 variant
  clock_seq_and_node_ |= node;
}

uint64_t UuidGen::monotonic_timestamp() {
  while (true) {
    uint64_t now = from_unix_timestamp(get_time_since_epoch_ms());
    uint64_t last = last_timestamp_.load();
    uint64_t candidate = (now > last) ? now : last + 1;
    if (last_timestamp_.compare_exchange_strong(last, candidate)) {
      return candidate;
    }
  }
}
