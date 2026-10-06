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

#include <gtest/gtest.h>

#include "cassandra.h"
#include "cassandra_uuid_operators.hpp"
#include "get_time.hpp"
#include "scoped_ptr.hpp"

#include <algorithm>
#include <ctype.h>
#include <string.h>

using namespace datastax;
using namespace datastax::internal;

inline bool operator!=(const CassUuid& u1, const CassUuid& u2) {
  return u1.clock_seq_and_node != u2.clock_seq_and_node ||
         u1.time_and_version != u2.time_and_version;
}

TEST(UuidUnitTest, V1) {
  CassUuidGen* uuid_gen = cass_uuid_gen_new();

  CassUuid prev_uuid;
  cass_uuid_gen_time(uuid_gen, &prev_uuid);
  EXPECT_EQ(cass_uuid_version(prev_uuid), 1);

  for (int i = 0; i < 1000; ++i) {
    CassUuid uuid;
    uint64_t curr_ts = get_time_since_epoch_ms();
    cass_uuid_gen_time(uuid_gen, &uuid);
    cass_uint64_t ts = cass_uuid_timestamp(uuid);

    EXPECT_EQ(cass_uuid_version(uuid), 1);
    EXPECT_TRUE(ts == curr_ts || ts - 1 == curr_ts);

    // This can't compare the uuids directly because a uuid timestamp is
    // only accurate to the millisecond. The generated uuid might have more
    // granularity.
    CassUuid from_ts_uuid;
    cass_uuid_gen_from_time(uuid_gen, ts, &from_ts_uuid);
    EXPECT_EQ(ts, cass_uuid_timestamp(from_ts_uuid));
    EXPECT_EQ(cass_uuid_version(from_ts_uuid), 1);

    EXPECT_NE(uuid, prev_uuid);
    prev_uuid = uuid;
  }

  cass_uuid_gen_free(uuid_gen);
}

TEST(UuidUnitTest, V1MinMax) {
  cass_uint64_t founded_ts = 1270080000; // April 2010
  cass_uint64_t curr_ts = get_time_since_epoch_ms();

  CassUuid min_uuid_1;
  CassUuid min_uuid_2;
  cass_uuid_min_from_time(founded_ts, &min_uuid_1);
  cass_uuid_min_from_time(curr_ts, &min_uuid_2);
  EXPECT_EQ(founded_ts, cass_uuid_timestamp(min_uuid_1));
  EXPECT_EQ(curr_ts, cass_uuid_timestamp(min_uuid_2));
  EXPECT_EQ(cass_uuid_version(min_uuid_1), 1);
  EXPECT_EQ(cass_uuid_version(min_uuid_2), 1);
  EXPECT_EQ(min_uuid_1.clock_seq_and_node, min_uuid_2.clock_seq_and_node);
  EXPECT_NE(min_uuid_1.time_and_version, min_uuid_2.time_and_version);

  CassUuid max_uuid_1;
  CassUuid max_uuid_2;
  cass_uuid_max_from_time(founded_ts, &max_uuid_1);
  cass_uuid_max_from_time(curr_ts, &max_uuid_2);
  EXPECT_EQ(founded_ts, cass_uuid_timestamp(max_uuid_1));
  EXPECT_EQ(curr_ts, cass_uuid_timestamp(max_uuid_2));
  EXPECT_EQ(cass_uuid_version(max_uuid_1), 1);
  EXPECT_EQ(cass_uuid_version(max_uuid_2), 1);
  EXPECT_EQ(max_uuid_1.clock_seq_and_node, max_uuid_2.clock_seq_and_node);
  EXPECT_NE(max_uuid_1.time_and_version, max_uuid_2.time_and_version);

  EXPECT_NE(min_uuid_1.clock_seq_and_node, max_uuid_1.clock_seq_and_node);
  EXPECT_NE(min_uuid_1.clock_seq_and_node, max_uuid_2.clock_seq_and_node);
  EXPECT_NE(min_uuid_2.clock_seq_and_node, max_uuid_1.clock_seq_and_node);
  EXPECT_NE(min_uuid_2.clock_seq_and_node, max_uuid_2.clock_seq_and_node);
}

TEST(UuidUnitTest, V1Node) {
  CassUuidGen* uuid_gen = cass_uuid_gen_new_with_node(0x0000112233445566LL);

  CassUuid uuid;
  cass_uuid_gen_time(uuid_gen, &uuid);
  EXPECT_EQ(cass_uuid_version(uuid), 1);

  char str[CASS_UUID_STRING_LENGTH];
  cass_uuid_string(uuid, str);

  EXPECT_TRUE(strstr(str, "-112233445566") != NULL);

  cass_uuid_gen_free(uuid_gen);
}

TEST(UuidUnitTest, V4) {
  CassUuidGen* uuid_gen = cass_uuid_gen_new();

  CassUuid prev_uuid;
  cass_uuid_gen_random(uuid_gen, &prev_uuid);
  EXPECT_EQ(cass_uuid_version(prev_uuid), 4);

  for (int i = 0; i < 1000; ++i) {
    CassUuid uuid;
    cass_uuid_gen_random(uuid_gen, &uuid);
    EXPECT_EQ(cass_uuid_version(uuid), 4);
    EXPECT_NE(uuid, prev_uuid);
    prev_uuid = uuid;
  }

  cass_uuid_gen_free(uuid_gen);
}

TEST(UuidUnitTest, FromString) {
  CassUuid uuid;
  const String expected = "c3b54ca0-7b01-11e4-aea6-c30dd51eaa64";
  char actual[CASS_UUID_STRING_LENGTH];

  EXPECT_EQ(cass_uuid_from_string(expected.c_str(), &uuid), CASS_OK);
  cass_uuid_string(uuid, actual);
  EXPECT_EQ(expected, actual);

  String upper = expected;
  std::transform(upper.begin(), upper.end(), upper.begin(), toupper);
  EXPECT_EQ(cass_uuid_from_string(upper.c_str(), &uuid), CASS_OK);
  cass_uuid_string(uuid, actual);
  EXPECT_EQ(expected, actual);
}

TEST(UuidUnitTest, FromStringInvalid) {
  CassUuid uuid;
  // Empty
  EXPECT_EQ(cass_uuid_from_string("", &uuid), CASS_ERROR_LIB_BAD_PARAMS);
  // One char short
  EXPECT_EQ(cass_uuid_from_string("c3b54ca0-7b01-11e4-aea6-c30dd51eaa6", &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
  // All '-'
  EXPECT_EQ(cass_uuid_from_string("------------------------------------", &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
  // Invalid char
  EXPECT_EQ(cass_uuid_from_string("c3b54ca0-7b01-11e4-aea6-c30dd51eaz64", &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
  // Extra '-'
  EXPECT_EQ(cass_uuid_from_string("c3b54ca0-7b01-11e4-aea6-c30dd51eaa-4", &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
  // Invalid group
  EXPECT_EQ(cass_uuid_from_string("c3b54ca07b0-1-11e4-aea6-c30dd51eaa64", &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
  // String longer then str_length
  EXPECT_EQ(cass_uuid_from_string_n("00-00-00-00-11-11-11-11-22-22-22-22-deadbeaf", 36, &uuid),
            CASS_ERROR_LIB_BAD_PARAMS);
}

TEST(UuidUnitTest, Operators) {
  CassUuid uuid1 = { 0x0000112233445566LL, 0x0000112233445566LL };
  CassUuid uuid2 = { 0x0000112233445566LL, 0x0000112233445566LL };
  CassUuid uuid3 = { 0x9988776655443322LL, 0x0000112233445566LL };
  CassUuid uuid4 = { 0x9988776655443322LL, 0x9988776655443322LL };

  // uuid1 == uuid2
  EXPECT_TRUE(uuid1 == uuid2);
  // uuid1 != uuid3
  EXPECT_FALSE(uuid1 == uuid3);
  // uuid1 != uuid4
  EXPECT_FALSE(uuid1 == uuid4);
  // uuid2 != uuid3
  EXPECT_FALSE(uuid2 == uuid3);
  // uuid2 != uuid3
  EXPECT_FALSE(uuid2 == uuid4);
  // uuid3 != uuid4
  EXPECT_FALSE(uuid3 == uuid4);
  // uuid1 >= uuid2
  EXPECT_FALSE(uuid1 < uuid2);
  // uuid1 < uuid3
  EXPECT_TRUE(uuid1 < uuid3);
  // uuid1 < uuid4
  EXPECT_TRUE(uuid1 < uuid4);
  // uuid2 < uuid3
  EXPECT_TRUE(uuid2 < uuid3);
  // uuid2 < uuid3
  EXPECT_TRUE(uuid2 < uuid4);
  // uuid3 < uuid4
  EXPECT_TRUE(uuid3 < uuid4);
}
