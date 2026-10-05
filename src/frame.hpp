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

#ifndef DATASTAX_INTERNAL_FRAME_HPP
#define DATASTAX_INTERNAL_FRAME_HPP

#include "buffer.hpp"
#include "string.hpp"
#include "types.hpp"
#include "vector.hpp"

namespace datastax { namespace internal { namespace core {

/**
 * Codec for the protocol v5 frame format.
 *
 * A frame wraps a stream of CQL envelopes and adds integrity checking plus a
 * bound on how much data must be buffered before a message can be processed:
 *
 *   [ 6 byte header ][ payload (<= 2^17 - 1 bytes) ][ 4 byte CRC32 trailer ]
 *
 * The header is a 3 byte little-endian integer holding a 17 bit payload
 * length, a 1 bit "self contained" flag and 6 padding bits, followed by a 3
 * byte CRC24 of those first 3 bytes. The trailer is the CRC32 of the payload
 * as a little-endian integer. See the native protocol v5 specification, §2.1.
 *
 * This driver never negotiates frame compression, so only the uncompressed
 * format is produced and accepted.
 */
class FrameCodec {
public:
  /** The maximum payload of a single frame: 2^17 - 1. */
  static const size_t MAX_PAYLOAD_LENGTH = 128 * 1024 - 1;

  /** Header (including its CRC24) for the uncompressed format. */
  static const size_t UNCOMPRESSED_HEADER_SIZE = 6;

  /** Size of the CRC32 payload trailer. */
  static const size_t TRAILER_SIZE = 4;

  /** Total framing overhead added to a single self contained frame. */
  static const size_t OVERHEAD = UNCOMPRESSED_HEADER_SIZE + TRAILER_SIZE;

  /** Smallest header we can decode before knowing the payload length. */
  static const size_t MIN_HEADER_SIZE = UNCOMPRESSED_HEADER_SIZE;

  /**
   * The number of framing bytes added to a payload of the given size.
   *
   * @param payload_size The total payload to be framed.
   * @return The overhead, including any additional headers needed to split
   * payloads larger than MAX_PAYLOAD_LENGTH.
   */
  static size_t overhead(size_t payload_size);

  /**
   * Write the header for an uncompressed, self contained frame.
   *
   * @param output At least UNCOMPRESSED_HEADER_SIZE writable bytes.
   * @param payload_size The payload length; must be <= MAX_PAYLOAD_LENGTH.
   */
  static void encode_header(char* output, size_t payload_size);

  /**
   * Write the header for an uncompressed frame that is (or is not) self
   * contained.
   *
   * @param output At least UNCOMPRESSED_HEADER_SIZE writable bytes.
   * @param payload_size The payload length; must be <= MAX_PAYLOAD_LENGTH.
   * @param self_contained False when this frame is one piece of a larger
   * payload that continues in subsequent frames.
   */
  static void encode_header(char* output, size_t payload_size, bool self_contained);

  /**
   * Write a payload CRC32 as a little-endian trailer.
   *
   * @param output At least TRAILER_SIZE writable bytes.
   * @param crc The payload checksum.
   */
  static void encode_trailer(char* output, uint32_t crc);

  /**
   * Frame the concatenation of the given buffers, appending the resulting frame
   * buffers to `output` (which is cleared first).
   *
   * Buffers are not copied when the payload fits in a single frame. Payloads
   * larger than MAX_PAYLOAD_LENGTH are split over several frames, each marked
   * as not self contained except for the last.
   *
   * @param payload The buffers holding the bytes to frame.
   * @param output Receives the framed buffers.
   * @return The total number of framed bytes.
   */
  static int32_t encode(const BufferVec& payload, BufferVec* output);

  /**
   * Frame the concatenation of the buffers in the range [start, end),
   * replacing the contents of `output` (which is cleared first).
   *
   * This lets a caller frame a sub-range of an existing buffer list in place,
   * which is how a request's already encoded envelope is wrapped.
   *
   * @return The total number of framed bytes.
   */
  static int32_t encode(const BufferVec& payload, size_t start, size_t end,
                        BufferVec* output);

  /**
   * Frame a contiguous payload, appending the resulting frame buffers to
   * `output` (which is cleared first).
   *
   * @param payload The bytes to frame.
   * @param size The number of bytes to frame.
   * @param output Receives the framed buffers.
   * @return The total number of framed bytes.
   */
  static int32_t encode(const char* payload, size_t size, BufferVec* output);

  /**
   * Frame a contiguous payload into a single string, mostly useful for tests
   * and for callers that already hold the payload in one piece.
   */
  static String encode(const String& payload);

  /** The CRC32 of a payload, as written to the trailer. */
  static uint32_t payload_crc(const char* data, size_t size);

  /** The CRC32 of the concatenation of the given buffers. */
  static uint32_t payload_crc(const BufferVec& payload, size_t start, size_t end);

  /** Write a 24 bit unsigned integer in little-endian order. */
  static void encode_uint24(char* output, uint32_t value);

  /** Read a 24 bit unsigned little-endian integer. */
  static uint32_t decode_uint24(const char* input);

  /** Write a 32 bit unsigned integer in little-endian order. */
  static void encode_uint32(char* output, uint32_t value);

  /** Read a 32 bit unsigned little-endian integer. */
  static uint32_t decode_uint32(const char* input);
};

/**
 * Incremental decoder for the protocol v5 frame format.
 *
 * Bytes are handed to feed() in whatever chunks the socket produces and
 * complete payloads are pulled out with next(). Payloads that span several
 * frames (non self contained frames) are transparently reassembled.
 */
class FrameDecoder {
public:
  enum Result {
    /** A complete payload is available via next()'s out parameters. */
    RESULT_OK,
    /** More bytes are required before a payload can be produced. */
    RESULT_NEED_MORE,
    /** The byte stream is corrupt and the connection must be closed. */
    RESULT_ERROR
  };

  /** Guards against a peer streaming unbounded non self contained frames. */
  static const size_t MAX_PAYLOAD_SIZE = 256 * 1024 * 1024;

  FrameDecoder();

  /** Discard all buffered state. */
  void reset();

  /** Append bytes read from the socket. */
  void feed(const char* data, size_t size);

  /**
   * Attempt to produce the next complete payload of de-framed envelope bytes.
   *
   * A single call may consume several frames when the payload was split across
   * non self contained frames.
   *
   * The returned pointer aliases either the decoder's internal buffer or its
   * accumulation buffer, so it is only valid until the next call to next() or
   * feed(). Consume it before calling either.
   *
   * @param payload Set to the de-framed bytes when returning RESULT_OK.
   * @param size Set to the length of `payload` when returning RESULT_OK.
   * @return The result of the attempt.
   */
  Result next(const char** payload, size_t* size);

  /** The reason for the most recent RESULT_ERROR. */
  const char* error() const { return error_.c_str(); }

private:
  Result fail(const char* message);
  Result fail_crc(const char* what, uint32_t expected, uint32_t actual);

private:
  Vector<char> accumulation_;
  size_t accumulation_offset_;
  Vector<char> payload_;
  size_t payload_size_;
  String error_;
};

}}} // namespace datastax::internal::core

#endif