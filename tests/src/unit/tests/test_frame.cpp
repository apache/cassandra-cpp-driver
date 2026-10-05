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

#include <gtest/gtest.h>

#include "unit.hpp"

#include <string.h>

#include "batch_request.hpp"
#include "buffer.hpp"
#include "crc.hpp"
#include "decoder.hpp"
#include "frame.hpp"
#include "protocol.hpp"
#include "query_request.hpp"
#include "request_callback.hpp"
#include "utils.hpp"

using datastax::internal::core::Buffer;
using datastax::internal::core::BufferVec;
using datastax::internal::core::FrameCodec;
using datastax::internal::core::FrameDecoder;
using datastax::internal::core::compute_crc24;
using datastax::internal::core::compute_crc32;
using datastax::internal::core::ProtocolVersion;

class FrameUnitTest : public Unit {
protected:
  static void expect_bytes(const String& expected, const char* actual, size_t actual_size) {
    ASSERT_EQ(expected.size(), actual_size);
    EXPECT_EQ(0, memcmp(expected.data(), actual, actual_size));
  }

  static String payload(size_t size, char seed = 'a') {
    String s;
    s.reserve(size);
    for (size_t i = 0; i < size; ++i) {
      s.push_back(static_cast<char>(seed + (i % 26)));
    }
    return s;
  }
};

// The reference values below were produced with the DataStax Python driver's
// frame segment implementation.
TEST_F(FrameUnitTest, Crc24CheckValue) {
  EXPECT_EQ(0x4b3f02u, compute_crc24("123456789", 9));
}

TEST_F(FrameUnitTest, Crc32CheckValue) {
  EXPECT_EQ(0xe2a261a7u, compute_crc32("123456789", 9));
}

TEST_F(FrameUnitTest, ReferenceFrameBytes) {
  const String body = payload(50);
  const String framed = FrameCodec::encode(body);

  // 6 byte header + 50 byte payload + 4 byte trailer.
  ASSERT_EQ(50u + FrameCodec::OVERHEAD, framed.size());
  // Payload length 50 with the self contained flag set, little-endian.
  EXPECT_EQ(0x32, static_cast<uint8_t>(framed[0]));
  EXPECT_EQ(0x00, static_cast<uint8_t>(framed[1]));
  EXPECT_EQ(0x02, static_cast<uint8_t>(framed[2]));
  // CRC24 of the header.
  EXPECT_EQ(0xa5, static_cast<uint8_t>(framed[3]));
  EXPECT_EQ(0xff, static_cast<uint8_t>(framed[4]));
  EXPECT_EQ(0xee, static_cast<uint8_t>(framed[5]));
  EXPECT_EQ(0x13, static_cast<uint8_t>(framed[56]));
  EXPECT_EQ(0xbd, static_cast<uint8_t>(framed[57]));
  EXPECT_EQ(0x8c, static_cast<uint8_t>(framed[58]));
  EXPECT_EQ(0x4f, static_cast<uint8_t>(framed[59]));
}

TEST_F(FrameUnitTest, RoundTripContiguous) {
  for (size_t size = 0; size < 300; ++size) {
    const String body = payload(size);
    const String framed = FrameCodec::encode(body);

    FrameDecoder decoder;
    decoder.feed(framed.data(), framed.size());

    const char* out = NULL;
    size_t out_size = 0;
    ASSERT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size))
        << "size " << size << ": " << decoder.error();
    EXPECT_EQ(size, out_size);
    expect_bytes(body, out, out_size);
  }
}

TEST_F(FrameUnitTest, RoundTripOversizedIsSplit) {
  const size_t size = FrameCodec::MAX_PAYLOAD_LENGTH * 2 + 7;
  const String body = payload(size);
  const String framed = FrameCodec::encode(body);

  // Two full sized frames plus a remainder, each with its own header/trailer.
  EXPECT_EQ(size + 3 * FrameCodec::OVERHEAD, framed.size());

  FrameDecoder decoder;
  decoder.feed(framed.data(), framed.size());

  const char* out = NULL;
  size_t out_size = 0;
  ASSERT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size)) << decoder.error();
  EXPECT_EQ(size, out_size);
  expect_bytes(body, out, out_size);
}

TEST_F(FrameUnitTest, RoundTripSplitAcrossFeeds) {
  const String body = payload(1000);
  const String framed = FrameCodec::encode(body);

  // Feed the frame one byte at a time; only the final feed may yield a payload.
  FrameDecoder decoder;
  for (size_t i = 0; i + 1 < framed.size(); ++i) {
    decoder.feed(framed.data() + i, 1);
    const char* out = NULL;
    size_t out_size = 0;
    ASSERT_EQ(FrameDecoder::RESULT_NEED_MORE, decoder.next(&out, &out_size)) << "at byte " << i;
  }
  decoder.feed(framed.data() + framed.size() - 1, 1);

  const char* out = NULL;
  size_t out_size = 0;
  ASSERT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size)) << decoder.error();
  EXPECT_EQ(1000u, out_size);
  expect_bytes(body, out, out_size);
}

TEST_F(FrameUnitTest, MultipleFramesInOneFeed) {
  const String first = payload(100);
  const String second = payload(200);
  const String third = payload(300);

  String stream = FrameCodec::encode(first);
  stream += FrameCodec::encode(second);
  stream += FrameCodec::encode(third);

  FrameDecoder decoder;
  decoder.feed(stream.data(), stream.size());

  const String* expected[] = { &first, &second, &third };
  for (int i = 0; i < 3; ++i) {
    const char* out = NULL;
    size_t out_size = 0;
    ASSERT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size)) << decoder.error();
    EXPECT_EQ(expected[i]->size(), out_size);
    expect_bytes(*expected[i], out, out_size);
  }

  const char* out = NULL;
  size_t out_size = 0;
  EXPECT_EQ(FrameDecoder::RESULT_NEED_MORE, decoder.next(&out, &out_size));
}

TEST_F(FrameUnitTest, MultiBufferPayloadMatchesContiguousCrc) {
  // A payload split across several buffers must checksum identically to the
  // same payload held in one buffer.
  const String whole = payload(300);

  BufferVec buffers;
  for (size_t i = 0; i < whole.size(); i += 37) {
    const size_t n = (whole.size() - i < 37) ? whole.size() - i : 37;
    buffers.push_back(Buffer(whole.data() + i, n));
  }

  EXPECT_EQ(compute_crc32(whole.data(), whole.size()),
            FrameCodec::payload_crc(buffers, 0, buffers.size()));

  BufferVec output;
  FrameCodec::encode(buffers, 0, buffers.size(), &output);

  String joined;
  for (size_t i = 0; i < output.size(); ++i) {
    joined.append(output[i].data(), output[i].size());
  }

  FrameDecoder decoder;
  decoder.feed(joined.data(), joined.size());
  const char* out = NULL;
  size_t out_size = 0;
  ASSERT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size)) << decoder.error();
  EXPECT_EQ(whole.size(), out_size);
  expect_bytes(whole, out, out_size);
}

TEST_F(FrameUnitTest, DetectsCorruptedHeader) {
  String framed = FrameCodec::encode(payload(100));
  framed[1] = static_cast<char>(framed[1] ^ 0x01);

  FrameDecoder decoder;
  decoder.feed(framed.data(), framed.size());

  const char* out = NULL;
  size_t out_size = 0;
  EXPECT_EQ(FrameDecoder::RESULT_ERROR, decoder.next(&out, &out_size));
  EXPECT_NE(std::string::npos, std::string(decoder.error()).find("Header CRC mismatch"));
}

TEST_F(FrameUnitTest, DetectsCorruptedPayload) {
  String framed = FrameCodec::encode(payload(100));
  framed[6 + 50] = static_cast<char>(framed[6 + 50] ^ 0x01);

  FrameDecoder decoder;
  decoder.feed(framed.data(), framed.size());

  const char* out = NULL;
  size_t out_size = 0;
  EXPECT_EQ(FrameDecoder::RESULT_ERROR, decoder.next(&out, &out_size));
  EXPECT_NE(std::string::npos, std::string(decoder.error()).find("Payload CRC mismatch"));
}

TEST_F(FrameUnitTest, DetectsTruncatedFrame) {
  const String framed = FrameCodec::encode(payload(100));

  FrameDecoder decoder;
  decoder.feed(framed.data(), framed.size() - 1);

  const char* out = NULL;
  size_t out_size = 0;
  EXPECT_EQ(FrameDecoder::RESULT_NEED_MORE, decoder.next(&out, &out_size));

  // Completing the frame makes it decodable.
  decoder.feed(framed.data() + framed.size() - 1, 1);
  EXPECT_EQ(FrameDecoder::RESULT_OK, decoder.next(&out, &out_size));
}
/**
 * The driver accepts keyspaces as CQL identifiers, so a case sensitive keyspace
 * reaches us quoted ("CaseSensitive"). Protocol v5 carries the keyspace name in
 * a dedicated field where the quotes must be removed.
 */
TEST_F(FrameUnitTest, UnescapeId) {
  using datastax::internal::unescape_id;

  EXPECT_EQ("casesensitive", unescape_id("casesensitive"));
  EXPECT_EQ("CaseSensitive", unescape_id("\"CaseSensitive\""));
  EXPECT_EQ("", unescape_id(""));
  EXPECT_EQ("", unescape_id("\"\""));
  EXPECT_EQ("a\"b", unescape_id("\"a\"\"b\""));
  // Not a quoted identifier: returned as-is.
  EXPECT_EQ("\"unterminated", unescape_id("\"unterminated"));
  EXPECT_EQ("\"mismatched'", unescape_id("\"mismatched'"));
}

/**
 * A duration is three signed vints (zig-zag encoded) for months, days and
 * nanoseconds. Reference values produced with the DataStax Python driver's
 * duration implementation.
 */
TEST_F(FrameUnitTest, DecodesDuration) {
  using datastax::internal::core::Decoder;

  // 1 month, 2 days, 3 nanoseconds -> zig-zag 2, 4, 6
  const char encoded[] = { '\x02', '\x04', '\x06' };
  Decoder decoder(encoded, sizeof(encoded), ProtocolVersion(CASS_PROTOCOL_VERSION_V5));

  int32_t months = -1;
  int32_t days = -1;
  int64_t nanos = -1;
  ASSERT_TRUE(decoder.as_duration(&months, &days, &nanos));
  EXPECT_EQ(1, months);
  EXPECT_EQ(2, days);
  EXPECT_EQ(3, nanos);

  // -1 month, 0 days, 0 nanoseconds -> zig-zag 1, 0, 0
  const char negative[] = { '\x01', '\x00', '\x00' };
  Decoder negative_decoder(negative, sizeof(negative), ProtocolVersion(CASS_PROTOCOL_VERSION_V5));
  months = 0;
  days = 0;
  nanos = 0;
  ASSERT_TRUE(negative_decoder.as_duration(&months, &days, &nanos));
  EXPECT_EQ(-1, months);
  EXPECT_EQ(0, days);
  EXPECT_EQ(0, nanos);
}

namespace {

using datastax::internal::core::BatchRequest;
using datastax::internal::core::QueryRequest;
using datastax::internal::core::Request;
using datastax::internal::core::ResponseMessage;
using datastax::internal::core::SimpleRequestCallback;

/**
 * The batch encoder only reads settings from the request, so a callback that
 * swallows the response is enough to drive it.
 */
class NoOpRequestCallback : public SimpleRequestCallback {
public:
  explicit NoOpRequestCallback(const Request::ConstPtr& request)
      : SimpleRequestCallback(request) {}

protected:
  virtual void on_internal_set(ResponseMessage* response) {}
  virtual void on_internal_error(CassError code, const String& message) {}
  virtual void on_internal_timeout() {}
};

/**
 * Encodes a batch and hands back the trailing <consistency><flags>[...]
 * parameter buffer, which is the last buffer the encoder appends.
 */
String encode_batch_params(ProtocolVersion version) {
  QueryRequest::Ptr statement(new QueryRequest("INSERT INTO t (k) VALUES (1)", 0));

  // SharedRefPtr takes ownership, so the request must be heap allocated.
  BatchRequest* batch = new BatchRequest(CASS_BATCH_TYPE_LOGGED);
  batch->add_statement(statement.get());
  batch->set_consistency(CASS_CONSISTENCY_ONE);
  batch->set_serial_consistency(CASS_CONSISTENCY_LOCAL_SERIAL);
  batch->set_timestamp(0x0102030405060708LL);
  batch->set_now_in_seconds(1234);
  // Quoted on the way in, unescaped on the wire.
  batch->set_keyspace("\"CaseSensitive\"");

  Request::ConstPtr request(batch);
  NoOpRequestCallback callback(request);

  BufferVec bufs;
  // BatchRequest::encode() is private; dispatch through the base interface.
  const Request* base = request.get();
  EXPECT_GT(base->encode(version, &callback, &bufs), 0);

  if (bufs.empty()) {
    return String();
  }
  const Buffer& params = bufs.back();
  return String(params.data(), params.size());
}

} // namespace

/**
 * Protocol v5 widened <flags> to [int] and requires the parameter block to be
 * ordered <serial_consistency><timestamp><keyspace><now_in_seconds>. Encoding
 * <now_in_seconds> ahead of <keyspace> desynchronizes the reader, so assert the
 * exact bytes. CQL integers are big endian.
 */
TEST_F(FrameUnitTest, BatchV5QueryParamOrder) {
  String params = encode_batch_params(ProtocolVersion(CASS_PROTOCOL_VERSION_V5));
  const char* p = params.data();

  // consistency[2] flags[4] serial[2] timestamp[8] keyspace[2+13] now[4]
  ASSERT_EQ(2u + 4u + 2u + 8u + 2u + 13u + 4u, params.size());

  // <consistency> [short] = ONE
  EXPECT_EQ(0x00, p[0]);
  EXPECT_EQ(0x01, p[1]);

  // <flags> [int]: serial | timestamp | keyspace | now_in_seconds
  EXPECT_EQ(0x00, p[2]);
  EXPECT_EQ(0x00, p[3]);
  EXPECT_EQ(0x01, p[4]);
  EXPECT_EQ(0xb0, static_cast<unsigned char>(p[5]));

  // <serial_consistency> [short] = LOCAL_SERIAL (0x0009)
  EXPECT_EQ(0x00, p[6]);
  EXPECT_EQ(0x09, p[7]);

  // <timestamp> [long]
  for (int i = 0; i < 8; ++i) {
    EXPECT_EQ(static_cast<char>(0x01 + i), p[8 + i]);
  }

  // <keyspace> [string]: 13 characters, quotes stripped by unescape_id()
  EXPECT_EQ(0x00, p[16]);
  EXPECT_EQ(0x0d, p[17]);
  EXPECT_EQ("CaseSensitive", String(p + 18, 13));

  // <now_in_seconds> [int] = 1234 -- must come last.
  EXPECT_EQ(0x00, p[31]);
  EXPECT_EQ(0x00, p[32]);
  EXPECT_EQ(0x04, p[33]);
  EXPECT_EQ(0xd2, static_cast<unsigned char>(p[34]));
}

/**
 * DSE v2 predates the With_now_in_seconds flag but still uses the widened
 * four byte <flags> introduced by v5, so the field width must not be tied to
 * now_in_seconds support.
 */
TEST_F(FrameUnitTest, BatchV4KeepsNarrowFlags) {
  const String v4 = encode_batch_params(ProtocolVersion(CASS_PROTOCOL_VERSION_V4));

  // v4 keeps <flags> as a single byte and never carries a keyspace or
  // now_in_seconds, so only consistency/serial/timestamp are present.
  ASSERT_EQ(2u + 1u + 2u + 8u, v4.size());
  EXPECT_EQ(0x30, static_cast<unsigned char>(v4.data()[2])); // serial | timestamp
}

/**
 * DSE v2 predates the With_now_in_seconds flag but still uses the widened four
 * byte <flags>, so the field width must follow query_flags_size() rather than
 * now_in_seconds support. Encoding a single byte here would desynchronize every
 * following field.
 */
TEST_F(FrameUnitTest, BatchDseV2KeepsWideFlags) {
  const String dse = encode_batch_params(ProtocolVersion(CASS_PROTOCOL_VERSION_DSEV2));

  // DSE v2 supports the keyspace field but not now_in_seconds, so it carries
  // every field except the trailing [int].
  // consistency[2] flags[4] serial[2] timestamp[8] keyspace[2+13]
  ASSERT_EQ(2u + 4u + 2u + 8u + 2u + 13u, dse.size());
  const char* p = dse.data();

  // <flags> [int] = serial | timestamp | keyspace (no now_in_seconds)
  EXPECT_EQ(0x00, p[2]);
  EXPECT_EQ(0x00, p[3]);
  EXPECT_EQ(0x00, p[4]);
  EXPECT_EQ(0xb0, static_cast<unsigned char>(p[5]));

  // <serial_consistency> [short] = LOCAL_SERIAL
  EXPECT_EQ(0x00, p[6]);
  EXPECT_EQ(0x09, p[7]);

  // <keyspace> [string], still unescaped
  EXPECT_EQ("CaseSensitive", String(p + 18, 13));
}
