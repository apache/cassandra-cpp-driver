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

#include "unit.hpp"

#include "cassandra.h"
#include "protocol.hpp"

using datastax::internal::core::ProtocolVersion;

class ProtocolVersionUnitTest : public Unit {};

TEST_F(ProtocolVersionUnitTest, LowestSupported) {
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V3), ProtocolVersion::lowest_supported());
}

TEST_F(ProtocolVersionUnitTest, HighestSupported) {
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_DSEV2), ProtocolVersion::highest_supported());
}

TEST_F(ProtocolVersionUnitTest, NewestBeta) {
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V5), ProtocolVersion::newest_beta());
}

TEST_F(ProtocolVersionUnitTest, IsValid) {
  { // Invalid
    ProtocolVersion invalid;
    EXPECT_FALSE(invalid.is_valid());
  }

  { // Invalid
    ProtocolVersion zero(0);
    EXPECT_FALSE(zero.is_valid());
  }

  { // Invalid (no longer supported)
    ProtocolVersion v1(CASS_PROTOCOL_VERSION_V1);
    EXPECT_FALSE(v1.is_valid());
  }

  { // Invalid (no longer supported)
    ProtocolVersion v2(CASS_PROTOCOL_VERSION_V2);
    EXPECT_FALSE(v2.is_valid());
  }

  { // Valid
    ProtocolVersion v3(CASS_PROTOCOL_VERSION_V3);
    EXPECT_TRUE(v3.is_valid());
  }

  { // Valid
    ProtocolVersion v4(CASS_PROTOCOL_VERSION_V4);
    EXPECT_TRUE(v4.is_valid());
  }

  { // Valid
    ProtocolVersion v5(CASS_PROTOCOL_VERSION_V5);
    EXPECT_TRUE(v5.is_valid());
  }

  { // Valid
    ProtocolVersion vDSE1(CASS_PROTOCOL_VERSION_DSEV1);
    EXPECT_TRUE(vDSE1.is_valid());
  }

  { // Valid (Latest DSE protocol version)
    ProtocolVersion vDSE2(CASS_PROTOCOL_VERSION_DSEV2);
    EXPECT_TRUE(vDSE2.is_valid());
  }
}

TEST_F(ProtocolVersionUnitTest, IsBeta) {
  { // Not valid beta
    ProtocolVersion invalid;
    EXPECT_FALSE(invalid.is_beta());
  }

  { // Invalid beta (Latest DSE protocol version)
    ProtocolVersion vDSE2(CASS_PROTOCOL_VERSION_DSEV2);
    EXPECT_FALSE(vDSE2.is_beta());
  }

  { // Not beta (v5 is GA)
    ProtocolVersion v5(CASS_PROTOCOL_VERSION_V5);
    EXPECT_FALSE(v5.is_beta());
  }
}

TEST_F(ProtocolVersionUnitTest, ToString) {
  {
    ProtocolVersion invalid;
    EXPECT_EQ(invalid.to_string(), "<invalid>");
  }

  {
    ProtocolVersion v4(CASS_PROTOCOL_VERSION_V4);
    EXPECT_EQ(v4.to_string(), "v4");
  }

  {
    ProtocolVersion v5(CASS_PROTOCOL_VERSION_V5);
    EXPECT_EQ(v5.to_string(), "v5");
  }

  {
    ProtocolVersion DSEv1(CASS_PROTOCOL_VERSION_DSEV1);
    EXPECT_EQ(DSEv1.to_string(), "DSEv1");
  }
}

TEST_F(ProtocolVersionUnitTest, AttemptLowerSupported) {
  ProtocolVersion version(CASS_PROTOCOL_VERSION_DSEV2);
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_DSEV2), version);

  EXPECT_TRUE((version = version.previous()).is_valid());
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_DSEV1), version);

  EXPECT_TRUE((version = version.previous()).is_valid());
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V5), version);

  EXPECT_TRUE((version = version.previous()).is_valid());
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V4), version);

  EXPECT_TRUE((version = version.previous()).is_valid());
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V3), version);

  EXPECT_FALSE(version.previous().is_valid()); // Can't go any lower
  EXPECT_EQ(ProtocolVersion(CASS_PROTOCOL_VERSION_V3), version);
}

TEST_F(ProtocolVersionUnitTest, SupportsSetKeyspace) {
  { // Supported
    ProtocolVersion v5(CASS_PROTOCOL_VERSION_V5);
    EXPECT_TRUE(v5.supports_set_keyspace());
  }

  { // Supported
    ProtocolVersion DSEv2(CASS_PROTOCOL_VERSION_DSEV2);
    EXPECT_TRUE(DSEv2.supports_set_keyspace());
  }

  { // Not supported
    ProtocolVersion DSEv1(CASS_PROTOCOL_VERSION_DSEV1);
    EXPECT_FALSE(DSEv1.supports_set_keyspace());

    for (int i = CASS_PROTOCOL_VERSION_V1; i <= CASS_PROTOCOL_VERSION_V4; ++i) {
      ProtocolVersion version(i);
      EXPECT_FALSE(version.supports_set_keyspace());
    }
  }
}

TEST_F(ProtocolVersionUnitTest, SupportsResultMetadataId) {
  { // Supported
    ProtocolVersion v5(CASS_PROTOCOL_VERSION_V5);
    EXPECT_TRUE(v5.supports_result_metadata_id());
  }

  { // Supported
    ProtocolVersion DSEv2(CASS_PROTOCOL_VERSION_DSEV2);
    EXPECT_TRUE(DSEv2.supports_result_metadata_id());
  }

  { // Not supported
    ProtocolVersion DSEv1(CASS_PROTOCOL_VERSION_DSEV1);
    EXPECT_FALSE(DSEv1.supports_result_metadata_id());

    for (int i = CASS_PROTOCOL_VERSION_V1; i <= CASS_PROTOCOL_VERSION_V4; ++i) {
      ProtocolVersion version(i);
      EXPECT_FALSE(version.supports_result_metadata_id());
    }
  }
}
