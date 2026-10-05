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

#ifndef DATASTAX_INTERNAL_CRC_HPP
#define DATASTAX_INTERNAL_CRC_HPP

#include "types.hpp"

namespace datastax { namespace internal { namespace core {

/**
 * The CRC-24 used by the protocol v5 frame header.
 *
 * Initialized with 0x875060 and using the polynomial 0x1974F0B, processing
 * each byte most significant bit first. Unlike CRC-32 this is not reflected.
 *
 * @param data The bytes to checksum. For a frame header this is the packed
 * little-endian header integer, meaning the bytes are consumed in the order
 * they appear on the wire.
 * @param size The number of bytes to checksum.
 * @return The 24 bit checksum.
 */
uint32_t compute_crc24(const char* data, size_t size);

/**
 * The CRC-32 used by the protocol v5 frame trailer.
 *
 * This is the standard (reflected, polynomial 0xEDB88320) CRC-32 computed over
 * the four seed bytes 0xFA 0x2D 0x55 0xCA followed by `data`. Cassandra uses
 * this seed so that a CRC-32 of a payload is computed with a single pass.
 *
 * @param data The bytes to checksum.
 * @param size The number of bytes to checksum.
 * @return The 32 bit checksum.
 */
uint32_t compute_crc32(const char* data, size_t size);

/**
 * The starting state for a protocol v5 frame payload checksum.
 *
 * A frame payload CRC is a standard CRC-32 over the four seed bytes
 * 0xFA 0x2D 0x55 0xCA followed by the payload, so the seed has to be folded in
 * before any payload bytes. Use this together with compute_crc32_update() when
 * the payload is not contiguous.
 */
uint32_t compute_crc32_init();

/**
 * Incrementally extend a CRC-32 over additional bytes.
 *
 * `crc` is a finalized checksum as returned by compute_crc32() (or
 * compute_crc32_init() for a fresh checksum). This allows a payload split
 * across several buffers to be checksummed without copying it into a
 * contiguous buffer first.
 *
 * @param crc The running checksum.
 * @param data The bytes to append to the checksum.
 * @param size The number of bytes to append.
 * @return The updated checksum.
 */
uint32_t compute_crc32_update(uint32_t crc, const char* data, size_t size);

}}} // namespace datastax::internal::core

#endif