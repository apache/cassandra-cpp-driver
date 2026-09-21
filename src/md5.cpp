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

// Based on public domain source found here:
// http://openwall.info/wiki/people/solar/software/public-domain-source-code/md5

#include "md5.hpp"

#include "utils.hpp"

#include <string.h>

// The basic MD5 functions.
//
// F and G are optimized compared to their RFC 1321 definitions for
// architectures that lack an AND-NOT instruction, just like in Colin Plumb's
// implementation.
#define F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define H(x, y, z) ((x) ^ (y) ^ (z))
#define I(x, y, z) ((y) ^ ((x) | ~(z)))

// The MD5 transformation for all four rounds.
#define STEP(f, a, b, c, d, x, t, s)                       \
  (a) += f((b), (c), (d)) + (x) + (t);                     \
  (a) = (((a) << (s)) | (((a)&0xffffffff) >> (32 - (s)))); \
  (a) += (b);

// SET reads 4 input bytes in little-endian byte order and stores them
// in a properly aligned word in host byte order.
//
// The check for little-endian architectures that tolerate unaligned
// memory accesses is just an optimization.  Nothing will break if it
// doesn't work.
#if defined(__i386__) || defined(__x86_64__) || defined(__vax__)
#define SET(n) (*(MD5_u32plus*)&ptr[(n)*4])
#define GET(n) SET(n)
#else
#define SET(n)                                                                  \
  (block_[(n)] = (MD5_u32plus)ptr[(n)*4] | ((MD5_u32plus)ptr[(n)*4 + 1] << 8) | \
                 ((MD5_u32plus)ptr[(n)*4 + 2] << 16) | ((MD5_u32plus)ptr[(n)*4 + 3] << 24))
#define GET(n) (block_[(n)])
#endif

using namespace datastax::internal;

Md5::Md5()
    : lo_(0)
    , hi_(0)
    , a_(0x67452301)
    , b_(0xefcdab89)
    , c_(0x98badcfe)
    , d_(0x10325476) {}

void Md5::update(const uint8_t* data, size_t size) {
  MD5_u32plus saved_lo;
  size_t used, free;

  saved_lo = lo_;
  if ((lo_ = (saved_lo + size) & 0x1fffffff) < saved_lo) hi_++;
  hi_ += size >> 29;

  used = saved_lo & 0x3f;

  if (used) {
    free = 64 - used;

    if (size < free) {
      memcpy(&buffer_[used], data, size);
      return;
    }

    memcpy(&buffer_[used], data, free);
    data = (unsigned char*)data + free;
    size -= free;
    body(buffer_, 64);
  }

  if (size >= 64) {
    data = body(data, size & ~(unsigned long)0x3f);
    size &= 0x3f;
  }

  memcpy(buffer_, data, size);
}

void Md5::final(uint8_t* result) {
  unsigned long used, free;

  used = lo_ & 0x3f;

  buffer_[used++] = 0x80;

  free = 64 - used;

  if (free < 8) {
    memset(&buffer_[used], 0, free);
    body(buffer_, 64);
    used = 0;
    free = 64;
  }

  memset(&buffer_[used], 0, free - 8);

  lo_ <<= 3;
  buffer_[56] = static_cast<unsigned char>(lo_);
  buffer_[57] = static_cast<unsigned char>(lo_ >> 8);
  buffer_[58] = static_cast<unsigned char>(lo_ >> 16);
  buffer_[59] = static_cast<unsigned char>(lo_ >> 24);
  buffer_[60] = static_cast<unsigned char>(hi_);
  buffer_[61] = static_cast<unsigned char>(hi_ >> 8);
  buffer_[62] = static_cast<unsigned char>(hi_ >> 16);
  buffer_[63] = static_cast<unsigned char>(hi_ >> 24);

  body(buffer_, 64);

  result[0] = static_cast<uint8_t>(a_);
  result[1] = static_cast<uint8_t>(a_ >> 8);
  result[2] = static_cast<uint8_t>(a_ >> 16);
  result[3] = static_cast<uint8_t>(a_ >> 24);
  result[4] = static_cast<uint8_t>(b_);
  result[5] = static_cast<uint8_t>(b_ >> 8);
  result[6] = static_cast<uint8_t>(b_ >> 16);
  result[7] = static_cast<uint8_t>(b_ >> 24);
  result[8] = static_cast<uint8_t>(c_);
  result[9] = static_cast<uint8_t>(c_ >> 8);
  result[10] = static_cast<uint8_t>(c_ >> 16);
  result[11] = static_cast<uint8_t>(c_ >> 24);
  result[12] = static_cast<uint8_t>(d_);
  result[13] = static_cast<uint8_t>(d_ >> 8);
  result[14] = static_cast<uint8_t>(d_ >> 16);
  result[15] = static_cast<uint8_t>(d_ >> 24);

  memset((void *)this, 0, sizeof(Md5));
}

// This processes one or more 64-byte data blocks, but does NOT update
// the bit counters.  There are no alignment requirements.
const uint8_t* Md5::body(const uint8_t* data, size_t size) {
  const uint8_t* ptr = data;
  MD5_u32plus a, b, c, d;
  MD5_u32plus saved_a, saved_b, saved_c, saved_d;

  a = a_;
  b = b_;
  c = c_;
  d = d_;

  do {
    saved_a = a;
    saved_b = b;
    saved_c = c;
    saved_d = d;

    // Round 1
    STEP(F, a, b, c, d, SET(0), 0xd76aa478, 7)
    STEP(F, d, a, b, c, SET(1), 0xe8c7b756, 12)
    STEP(F, c, d, a, b, SET(2), 0x242070db, 17)
    STEP(F, b, c, d, a, SET(3), 0xc1bdceee, 22)
    STEP(F, a, b, c, d, SET(4), 0xf57c0faf, 7)
    STEP(F, d, a, b, c, SET(5), 0x4787c62a, 12)
    STEP(F, c, d, a, b, SET(6), 0xa8304613, 17)
    STEP(F, b, c, d, a, SET(7), 0xfd469501, 22)
    STEP(F, a, b, c, d, SET(8), 0x698098d8, 7)
    STEP(F, d, a, b, c, SET(9), 0x8b44f7af, 12)
    STEP(F, c, d, a, b, SET(10), 0xffff5bb1, 17)
    STEP(F, b, c, d, a, SET(11), 0x895cd7be, 22)
    STEP(F, a, b, c, d, SET(12), 0x6b901122, 7)
    STEP(F, d, a, b, c, SET(13), 0xfd987193, 12)
    STEP(F, c, d, a, b, SET(14), 0xa679438e, 17)
    STEP(F, b, c, d, a, SET(15), 0x49b40821, 22)

    // Round 2
    STEP(G, a, b, c, d, GET(1), 0xf61e2562, 5)
    STEP(G, d, a, b, c, GET(6), 0xc040b340, 9)
    STEP(G, c, d, a, b, GET(11), 0x265e5a51, 14)
    STEP(G, b, c, d, a, GET(0), 0xe9b6c7aa, 20)
    STEP(G, a, b, c, d, GET(5), 0xd62f105d, 5)
    STEP(G, d, a, b, c, GET(10), 0x02441453, 9)
    STEP(G, c, d, a, b, GET(15), 0xd8a1e681, 14)
    STEP(G, b, c, d, a, GET(4), 0xe7d3fbc8, 20)
    STEP(G, a, b, c, d, GET(9), 0x21e1cde6, 5)
    STEP(G, d, a, b, c, GET(14), 0xc33707d6, 9)
    STEP(G, c, d, a, b, GET(3), 0xf4d50d87, 14)
    STEP(G, b, c, d, a, GET(8), 0x455a14ed, 20)
    STEP(G, a, b, c, d, GET(13), 0xa9e3e905, 5)
    STEP(G, d, a, b, c, GET(2), 0xfcefa3f8, 9)
    STEP(G, c, d, a, b, GET(7), 0x676f02d9, 14)
    STEP(G, b, c, d, a, GET(12), 0x8d2a4c8a, 20)

    // Round 3
    STEP(H, a, b, c, d, GET(5), 0xfffa3942, 4)
    STEP(H, d, a, b, c, GET(8), 0x8771f681, 11)
    STEP(H, c, d, a, b, GET(11), 0x6d9d6122, 16)
    STEP(H, b, c, d, a, GET(14), 0xfde5380c, 23)
    STEP(H, a, b, c, d, GET(1), 0xa4beea44, 4)
    STEP(H, d, a, b, c, GET(4), 0x4bdecfa9, 11)
    STEP(H, c, d, a, b, GET(7), 0xf6bb4b60, 16)
    STEP(H, b, c, d, a, GET(10), 0xbebfbc70, 23)
    STEP(H, a, b, c, d, GET(13), 0x289b7ec6, 4)
    STEP(H, d, a, b, c, GET(0), 0xeaa127fa, 11)
    STEP(H, c, d, a, b, GET(3), 0xd4ef3085, 16)
    STEP(H, b, c, d, a, GET(6), 0x04881d05, 23)
    STEP(H, a, b, c, d, GET(9), 0xd9d4d039, 4)
    STEP(H, d, a, b, c, GET(12), 0xe6db99e5, 11)
    STEP(H, c, d, a, b, GET(15), 0x1fa27cf8, 16)
    STEP(H, b, c, d, a, GET(2), 0xc4ac5665, 23)

    // Round 4
    STEP(I, a, b, c, d, GET(0), 0xf4292244, 6)
    STEP(I, d, a, b, c, GET(7), 0x432aff97, 10)
    STEP(I, c, d, a, b, GET(14), 0xab9423a7, 15)
    STEP(I, b, c, d, a, GET(5), 0xfc93a039, 21)
    STEP(I, a, b, c, d, GET(12), 0x655b59c3, 6)
    STEP(I, d, a, b, c, GET(3), 0x8f0ccc92, 10)
    STEP(I, c, d, a, b, GET(10), 0xffeff47d, 15)
    STEP(I, b, c, d, a, GET(1), 0x85845dd1, 21)
    STEP(I, a, b, c, d, GET(8), 0x6fa87e4f, 6)
    STEP(I, d, a, b, c, GET(15), 0xfe2ce6e0, 10)
    STEP(I, c, d, a, b, GET(6), 0xa3014314, 15)
    STEP(I, b, c, d, a, GET(13), 0x4e0811a1, 21)
    STEP(I, a, b, c, d, GET(4), 0xf7537e82, 6)
    STEP(I, d, a, b, c, GET(11), 0xbd3af235, 10)
    STEP(I, c, d, a, b, GET(2), 0x2ad7d2bb, 15)
    STEP(I, b, c, d, a, GET(9), 0xeb86d391, 21)

    a += saved_a;
    b += saved_b;
    c += saved_c;
    d += saved_d;

    ptr += 64;
  } while (size -= 64);

  a_ = a;
  b_ = b;
  c_ = c;
  d_ = d;

  return ptr;
}
