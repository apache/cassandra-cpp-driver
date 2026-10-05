/*
  Copyright (c) DataStax, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "crc.hpp"

#include <string.h>

namespace datastax { namespace internal { namespace core {

namespace {

const uint32_t CRC24_INIT = 0x875060;
const uint32_t CRC24_POLY = 0x1974F0B;
const uint32_t CRC24_MASK = 0xFFFFFF;

// The seed Cassandra prepends to every v5 frame payload before running CRC-32.
const char CRC32_SEED[4] = { '\xFA', '\x2D', '\x55', '\xCA' };

// Standard reflected CRC-32 table. Lazily built to avoid a static initializer
// running before main() in the dynamic library case.
struct Crc32Table {
  uint32_t entries[256];

  Crc32Table() {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      entries[i] = c;
    }
  }
};

const uint32_t* crc32_table() {
  static const Crc32Table table;
  return table.entries;
}

} // namespace

uint32_t compute_crc24(const char* data, size_t size) {
  uint32_t crc = CRC24_INIT;
  for (size_t i = 0; i < size; ++i) {
    crc ^= static_cast<uint32_t>(static_cast<uint8_t>(data[i])) << 16;
    for (int bit = 0; bit < 8; ++bit) {
      crc <<= 1;
      if (crc & 0x1000000) {
        crc ^= CRC24_POLY;
      }
    }
  }
  return crc & CRC24_MASK;
}

uint32_t compute_crc32_update(uint32_t crc, const char* data, size_t size) {
  const uint32_t* table = crc32_table();
  uint32_t c = crc ^ 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) {
    c = table[(c ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (c >> 8);
  }
  return c ^ 0xFFFFFFFFu;
}

uint32_t compute_crc32_init() { return compute_crc32_update(0, CRC32_SEED, sizeof(CRC32_SEED)); }

uint32_t compute_crc32(const char* data, size_t size) {
  return compute_crc32_update(compute_crc32_init(), data, size);
}

}}} // namespace datastax::internal::core