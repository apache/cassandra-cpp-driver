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

#include "cluster_config.hpp"
#include "cassandra.h"
#include "dc_aware_policy.hpp"
#include "string.hpp"

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

class ClusterConfigUnitTest : public testing::Test {
public:
  ClusterConfigUnitTest() {}

  virtual void SetUp() {
    cluster_ = cass_cluster_new();
  }

  virtual void TearDown() {
    cass_cluster_free(cluster_);
  }

protected:
  CassCluster* cluster_;

  const DCAwarePolicy* build_dc_policy() {

    // Disable token-aware routing in this case in order to avoid illegal instructions
    // when creating a TokenAwarePolicy in build_load_balancing_policy()
    cluster_->config().set_token_aware_routing(false);
    cluster_->config().default_profile().build_load_balancing_policy();

    // Verify the policy was set correctly with the right datacenter name
    const LoadBalancingPolicy* policy =
        cluster_->config().load_balancing_policy().get();
    return static_cast<const DCAwarePolicy*>(policy);
  }
};

// ==================== cass_cluster_set_load_balance_dc_aware_n ====================
TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareNHappyPath) {

  // Test valid parameters
  const char* valid_dc = "my_datacenter";
  EXPECT_EQ(CASS_OK, cass_cluster_set_load_balance_dc_aware_n(
                         cluster_, valid_dc, strlen(valid_dc), 
                         2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  // Should be using the partial string as local DC
  EXPECT_EQ(dc_policy->local_dc(), valid_dc);
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}

TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareNWithNullLocalDc) {

  // Test with NULL pointer (should now succeed and use empty string for DC)
  EXPECT_EQ(CASS_OK,
    cass_cluster_set_load_balance_dc_aware_n(
        cluster_, NULL, 10, 2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  EXPECT_EQ(dc_policy->local_dc(), String());
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}

TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareNWithZeroLengthLocalDc) {

  // Test with zero length (should now succeed and use empty string for DC)
  const char* valid_dc = "my_datacenter";
  EXPECT_EQ(CASS_OK,
    cass_cluster_set_load_balance_dc_aware_n(
        cluster_, valid_dc, 0, 2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  EXPECT_EQ(dc_policy->local_dc(), String());
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}

TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareNWithEmptyLocalDc) {

  // Test with empty string (should now succeed)
  const char* empty_string = "";
  EXPECT_EQ(CASS_OK,
            cass_cluster_set_load_balance_dc_aware_n(
                cluster_, empty_string, strlen(empty_string), 2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  EXPECT_EQ(dc_policy->local_dc(), String());
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}

TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareNWithPartialStringLocalDc) {

  // Test with partial string length
  const char* long_dc_name = "my_datacenter_with_a_long_name";
  size_t partial_length = 5;
  EXPECT_EQ(CASS_OK, cass_cluster_set_load_balance_dc_aware_n(
                         cluster_, long_dc_name, partial_length, 
                         2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  // Should just use up to partial_length of the datacenter name here.
  EXPECT_EQ(dc_policy->local_dc(), String(long_dc_name, partial_length));
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}

// ==================== cass_cluster_set_load_balance_dc_aware ====================
TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareWithNullLocalDc) {
  // Test with NULL to use local DC from connected node
  EXPECT_EQ(CASS_OK, cass_cluster_set_load_balance_dc_aware(
                         cluster_, NULL, 3, cass_false));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  // Should be using empty string as local DC (will be determined at runtime)
  EXPECT_EQ(dc_policy->local_dc(), String());
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 3u);
  EXPECT_TRUE(dc_policy->skip_remote_dcs_for_local_cl());
}
  
TEST_F(ClusterConfigUnitTest, SetLoadBalanceDcAwareWithEmptyLocalDc) {
  // Test with empty string to use local DC from connected node
  EXPECT_EQ(CASS_OK, cass_cluster_set_load_balance_dc_aware(
                         cluster_, "", 2, cass_true));

  const DCAwarePolicy* dc_policy = build_dc_policy();

  // Should be using empty string as local DC (will be determined at runtime)
  EXPECT_EQ(dc_policy->local_dc(), String());
  EXPECT_EQ(dc_policy->used_hosts_per_remote_dc(), 2u);
  EXPECT_FALSE(dc_policy->skip_remote_dcs_for_local_cl());
}
