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

#include "frame.hpp"

#include "crc.hpp"

#include <string.h>

using namespace datastax;
using namespace datastax::internal::core;

namespace datastax { namespace internal { namespace core {

namespace {

// Bit 17 of the packed header integer is the "self contained" flag; bits 0-16
// hold the payload length. The remaining 6 bits are padding.
const uint32_t SELF_CONTAINED_BIT = 17;
const uint32_t PAYLOAD_LENGTH_MASK = 0x1FFFF;

} // namespace

size_t FrameCodec::overhead(size_t payload_size) {
  if (payload_size == 0) return OVERHEAD;
  const size_t frames = (payload_size + MAX_PAYLOAD_LENGTH - 1) / MAX_PAYLOAD_LENGTH;
  return frames * OVERHEAD;
}

void FrameCodec::encode_uint24(char* output, uint32_t value) {
  output[0] = static_cast<char>(value & 0xFF);
  output[1] = static_cast<char>((value >> 8) & 0xFF);
  output[2] = static_cast<char>((value >> 16) & 0xFF);
}

uint32_t FrameCodec::decode_uint24(const char* input) {
  return static_cast<uint32_t>(static_cast<uint8_t>(input[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(input[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(input[2])) << 16);
}

void FrameCodec::encode_uint32(char* output, uint32_t value) {
  output[0] = static_cast<char>(value & 0xFF);
  output[1] = static_cast<char>((value >> 8) & 0xFF);
  output[2] = static_cast<char>((value >> 16) & 0xFF);
  output[3] = static_cast<char>((value >> 24) & 0xFF);
}

uint32_t FrameCodec::decode_uint32(const char* input) {
  return static_cast<uint32_t>(static_cast<uint8_t>(input[0])) |
         (static_cast<uint32_t>(static_cast<uint8_t>(input[1])) << 8) |
         (static_cast<uint32_t>(static_cast<uint8_t>(input[2])) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(input[3])) << 24);
}

void FrameCodec::encode_header(char* output, size_t payload_size) {
  encode_header(output, payload_size, true);
}

void FrameCodec::encode_header(char* output, size_t payload_size, bool self_contained) {
  assert(payload_size <= MAX_PAYLOAD_LENGTH && "Frame payload is too large");
  const uint32_t header = static_cast<uint32_t>(payload_size) |
                          (self_contained ? (1u << SELF_CONTAINED_BIT) : 0u);
  encode_uint24(output, header);
  encode_uint24(output + 3, compute_crc24(output, 3));
}

void FrameCodec::encode_trailer(char* output, uint32_t crc) { encode_uint32(output, crc); }

uint32_t FrameCodec::payload_crc(const char* data, size_t size) {
  return compute_crc32(data, size);
}

uint32_t FrameCodec::payload_crc(const BufferVec& payload, size_t start, size_t end) {
  uint32_t crc = compute_crc32_init();
  for (size_t i = start; i < end; ++i) {
    const Buffer& buf = payload[i];
    if (buf.size() > 0) {
      crc = compute_crc32_update(crc, buf.data(), buf.size());
    }
  }
  return crc;
}

int32_t FrameCodec::encode(const BufferVec& payload, BufferVec* output) {
  return encode(payload, 0, payload.size(), output);
}

int32_t FrameCodec::encode(const BufferVec& payload, size_t start, size_t end,
                           BufferVec* output) {
  output->clear();

  size_t total = 0;
  for (size_t i = start; i < end; ++i) {
    total += payload[i].size();
  }

  if (total <= MAX_PAYLOAD_LENGTH) {
    // Common case: a single self contained frame. The payload buffers are
    // referenced directly so no copy is made.
    Buffer header(UNCOMPRESSED_HEADER_SIZE);
    encode_header(header.data(), total, true);
    output->push_back(header);

    for (size_t i = start; i < end; ++i) {
      if (payload[i].size() > 0) {
        output->push_back(payload[i]);
      }
    }

    Buffer trailer(TRAILER_SIZE);
    encode_trailer(trailer.data(), payload_crc(payload, start, end));
    output->push_back(trailer);

    return static_cast<int32_t>(total + OVERHEAD);
  }

  // Rare case: the payload is too large for one frame and has to be split. The
  // chunks are copied because they cannot be expressed as whole buffers.
  size_t copied = 0;
  // Cursor into the payload: buffer `index` and the offset already consumed
  // from it. This has to persist across chunks, otherwise every chunk would
  // restart at the beginning of the payload.
  size_t index = start;
  size_t within = 0;

  while (copied < total) {
    const size_t chunk_size = (total - copied < MAX_PAYLOAD_LENGTH) ? (total - copied)
                                                                   : MAX_PAYLOAD_LENGTH;
    const bool self_contained = (copied + chunk_size == total);

    Buffer header(UNCOMPRESSED_HEADER_SIZE);
    encode_header(header.data(), chunk_size, self_contained);
    output->push_back(header);

    Buffer chunk(chunk_size);
    char* out = chunk.data();
    size_t written = 0;
    while (written < chunk_size) {
      while (index < end && payload[index].size() == 0) {
        ++index;
      }
      if (index >= end) {
        break; // Should not happen: chunk_size is bounded by the total size.
      }
      const Buffer& buf = payload[index];
      const size_t available = buf.size() - within;
      const size_t n = (available < chunk_size - written) ? available : chunk_size - written;
      memcpy(out + written, buf.data() + within, n);
      written += n;
      within += n;
      if (within == buf.size()) {
        ++index;
        within = 0;
      }
    }
    output->push_back(chunk);

    Buffer trailer(TRAILER_SIZE);
    encode_trailer(trailer.data(), compute_crc32(chunk.data(), chunk.size()));
    output->push_back(trailer);

    copied += chunk_size;
  }

  return static_cast<int32_t>(total + overhead(total));
}

int32_t FrameCodec::encode(const char* payload, size_t size, BufferVec* output) {
  Buffer buf(payload, size);
  BufferVec vec;
  vec.push_back(buf);
  return encode(vec, output);
}

String FrameCodec::encode(const String& payload) {
  BufferVec frames;
  encode(payload.data(), payload.size(), &frames);

  String result;
  size_t size = 0;
  for (size_t i = 0; i < frames.size(); ++i) {
    size += frames[i].size();
  }
  result.reserve(size);
  for (size_t i = 0; i < frames.size(); ++i) {
    result.append(frames[i].data(), frames[i].size());
  }
  return result;
}

FrameDecoder::FrameDecoder()
    : accumulation_offset_(0)
    , payload_size_(0) {}

void FrameDecoder::reset() {
  accumulation_.clear();
  accumulation_offset_ = 0;
  payload_.clear();
  payload_size_ = 0;
  error_.clear();
}

void FrameDecoder::feed(const char* data, size_t size) {
  // Reclaim the already parsed prefix before growing.
  if (accumulation_offset_ > 0) {
    if (accumulation_offset_ == accumulation_.size()) {
      accumulation_.clear();
      accumulation_offset_ = 0;
    } else if (accumulation_offset_ >= 64 * 1024) {
      accumulation_.erase(accumulation_.begin(), accumulation_.begin() + accumulation_offset_);
      accumulation_offset_ = 0;
    }
  }

  if (size == 0) return;

  const size_t old_size = accumulation_.size();
  accumulation_.resize(old_size + size);
  if (size > 0) {
    memcpy(&accumulation_[old_size], data, size);
  }
}

FrameDecoder::Result FrameDecoder::fail(const char* message) {
  error_ = message;
  return RESULT_ERROR;
}

FrameDecoder::Result FrameDecoder::fail_crc(const char* what, uint32_t expected, uint32_t actual) {
  OStringStream ss;
  ss << what << " (expected 0x" << std::hex << expected << ", calculated 0x" << actual << ")";
  error_ = ss.str();
  return RESULT_ERROR;
}

FrameDecoder::Result FrameDecoder::next(const char** payload, size_t* size) {
  const char* base;
  size_t available;
  size_t offset;

  // Start a fresh payload; bytes from any preceding non self contained frames
  // have already been appended to payload_.
  payload_.clear();
  payload_size_ = 0;

  for (;;) {
    base = accumulation_.empty() ? NULL : &accumulation_[0];
    available = accumulation_.size() - accumulation_offset_;
    offset = accumulation_offset_;

    if (available < FrameCodec::MIN_HEADER_SIZE) {
      return RESULT_NEED_MORE;
    }

    const uint32_t header = FrameCodec::decode_uint24(base + offset);
    const bool self_contained = (header & (1u << SELF_CONTAINED_BIT)) != 0;
    const size_t payload_length = header & PAYLOAD_LENGTH_MASK;
    const uint32_t expected_header_crc = FrameCodec::decode_uint24(base + offset + 3);
    const uint32_t actual_header_crc = compute_crc24(base + offset, 3);
    if (expected_header_crc != actual_header_crc) {
      return fail_crc("Header CRC mismatch", expected_header_crc, actual_header_crc);
    }

    const size_t frame_size = FrameCodec::UNCOMPRESSED_HEADER_SIZE + payload_length +
                              FrameCodec::TRAILER_SIZE;
    if (available < frame_size) {
      return RESULT_NEED_MORE;
    }

    const char* frame_payload = base + offset + FrameCodec::UNCOMPRESSED_HEADER_SIZE;
    const uint32_t expected_payload_crc =
        FrameCodec::decode_uint32(frame_payload + payload_length);
    const uint32_t actual_payload_crc = compute_crc32(frame_payload, payload_length);
    if (expected_payload_crc != actual_payload_crc) {
      return fail_crc("Payload CRC mismatch", expected_payload_crc, actual_payload_crc);
    }

    // Keep the payload if it continues into the next frame.
    if (!self_contained || payload_size_ > 0) {
      if (payload_size_ + payload_length > MAX_PAYLOAD_SIZE) {
        return fail("Frame payload exceeds the maximum reassembled size");
      }
      const size_t old_size = payload_.size();
      payload_.resize(old_size + payload_length);
      memcpy(&payload_[old_size], frame_payload, payload_length);
      payload_size_ += payload_length;
    } else {
      // Self contained and nothing accumulated: hand back the frame's own
      // bytes without copying them.
      *payload = frame_payload;
      *size = payload_length;
      accumulation_offset_ += frame_size;
      return RESULT_OK;
    }

    accumulation_offset_ += frame_size;

    if (self_contained) {
      *payload = payload_.empty() ? NULL : &payload_[0];
      *size = payload_size_;
      return RESULT_OK;
    }
  }
}

}}} // namespace datastax::internal::core