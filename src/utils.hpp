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

#ifndef DATASTAX_INTERNAL_COMMON_HPP
#define DATASTAX_INTERNAL_COMMON_HPP

#include "cassandra.h"
#include "macros.hpp"
#include "string.hpp"
#include "vector.hpp"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace datastax { namespace internal {

typedef Vector<String> ContactPointList;
typedef Vector<String> DcList;

// copy_cast<> prevents incorrect code from being generated when two unrelated
// types reference the same memory location and strict aliasing is enabled.
// The type "char*" is an exception and is allowed to alias any other
// pointer type. This allows memcpy() to copy bytes from one type to the other
// without violating strict aliasing and usually optimizes away on a modern
// compiler (GCC, Clang, and MSVC).

template <typename From, typename To>
inline To copy_cast(const From& from) {
  STATIC_ASSERT(sizeof(From) == sizeof(To));

  To to;
  memcpy(&to, &from, sizeof(from));
  return to;
}

inline size_t next_pow_2(size_t num) {
  size_t next = 2;
  size_t i = 0;
  while (next < num) {
    next = static_cast<size_t>(1) << i++;
  }
  return next;
}

String opcode_to_string(int opcode);

String to_string(const CassUuid& uuid);

void explode(const String& str, Vector<String>& vec, const char delimiter = ',');
String implode(const Vector<String>& vec, const char delimiter = ',');

String& trim(String& str);

String& escape_id(String& str);

/**
 * Reverse of `escape_id()`. Removes the surrounding double quotes of a quoted
 * identifier and collapses doubled quotes. Needed because the driver accepts
 * keyspaces as CQL identifiers (e.g. `"CaseSensitive"`), while protocol v5
 * carries the keyspace name in a dedicated field where the quotes must not
 * appear.
 */
String unescape_id(const String& str);

inline size_t num_leading_zeros(int64_t value) {
  if (value == 0) return 64;

#if defined(_MSC_VER)
  unsigned long index;
#if defined(_M_AMD64)
  _BitScanReverse64(&index, value);
#else
  // On 32-bit this needs to be split into two operations
  char isNonzero = _BitScanReverse(&index, (unsigned long)(value >> 32));

  if (isNonzero)
    // The most significant 4 bytes has a bit set, and our index is relative to that.
    // Add 32 to account for the lower 4 bytes that make up our 64-bit number.
    index += 32;
  else {
    // Scan the last 32 bits by truncating the 64-bit value
    _BitScanReverse(&index, (unsigned long)value);
  }
#endif
  // index is the (zero based) index, counting from lsb, of the most-significant 1 bit.
  // For example, a value of 12 (b1100) would return 3. The 4th bit is set, so there are
  // 60 leading zeros.
  return 64 - index - 1;
#else
  return __builtin_clzll(value);
#endif
}

inline size_t vint_size(int64_t value) {
  // | with 1 to ensure magnitude <= 63, so (63 - 1) / 7 <= 8
  size_t magnitude = num_leading_zeros(value | 1);
  return magnitude ? (9 - ((magnitude - 1) / 7)) : 9;
}

int32_t get_pid();

void set_thread_name(const String& thread_name);

template <class C>
static void set_pointer_keys(C& container) {
  container.set_empty_key(reinterpret_cast<typename C::key_type>(0x0));
  container.set_deleted_key(reinterpret_cast<typename C::key_type>(0x1));
}

void thread_yield();

}} // namespace datastax::internal

#endif
