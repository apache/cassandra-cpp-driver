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

#include "integration.hpp"

/**
 * Graceful disconnect (CEP-59 / CASSANDRA-21191) integration tests.
 *
 * When a node is drained it sends a GRACEFUL_DISCONNECT event on every
 * registered connection before closing the transport, and the driver must
 * drain its pool to that node and fail over without surfacing any error to
 * the application.
 *
 * Requires a server that implements the GRACEFUL_DISCONNECT event; on older
 * servers the tests are skipped by the version check.
 */
class GracefulDisconnectTests : public Integration {
public:
  void SetUp() {
    // Call the parent setup function (override startup for the server-side
    // graceful disconnect configuration; the feature is disabled by default)
    is_ccm_start_requested_ = false;
    is_session_requested_ = false;
    number_dc1_nodes_ = 2;
    Integration::SetUp();

    if (server_version_ >= "7.0.0") {
      ccm_->update_cluster_configuration("graceful_disconnect_enabled", "true");
    }
    ccm_->start_cluster();
  }
};

/**
 * Drain a node while running a steady query load and ensure the shutdown is
 * invisible to the application.
 *
 * This test ensures the driver observes the GRACEFUL_DISCONNECT event, drains
 * in-flight requests to the node, and fails over to the remaining node
 * without surfacing any error to the application.
 *
 * @jira_ticket CASSANDRA-21191
 * @cassandra_version 7.0.0
 * @expected_result The GRACEFUL_DISCONNECT event is received, no request
 *                  fails, and queries keep succeeding after the drain.
 */
CASSANDRA_INTEGRATION_TEST_F(GracefulDisconnectTests, FailOverWithoutDisruptionWhenNodeDrains) {
  CHECK_FAILURE;
  CHECK_VERSION(7.0.0);

  logger_.add_critera("Received GRACEFUL_DISCONNECT");
  connect();

  // Sanity check before the drain:
  session_.execute(SELECT_ALL_SYSTEM_LOCAL_CQL);

  unsigned int successes = 0;
  unsigned int failures = 0;

  // Drain node 2: the server stops accepting new requests and sends
  // GRACEFUL_DISCONNECT on every connection registered for it, then closes
  // the transport.
  ccm_->drain_node(2);

  // Steady query load while the drain is processed, collecting any error
  // that reaches the application:
  start_timer();
  while (elapsed_time() < 10000) {
    Result result = session_.execute(SELECT_ALL_SYSTEM_LOCAL_CQL, CASS_CONSISTENCY_LOCAL_ONE,
                                     false, false);
    if (result.error_code() == CASS_OK) {
      ++successes;
    } else {
      ++failures;
    }
    msleep(5);
  }

  // The driver must have observed the event:
  EXPECT_GE(logger_.count(), 1u);
  // Queries must keep succeeding after the drain (load fails over to the
  // other node):
  EXPECT_GT(successes, 100u);
  // The whole point of graceful disconnect: the shutdown must be invisible
  // to the application, no request may fail.
  EXPECT_EQ(failures, 0u);
}

/**
 * Ensure the driver does not register for GRACEFUL_DISCONNECT events when the
 * feature is disabled on the cluster object.
 *
 * @jira_ticket CASSANDRA-21191
 * @cassandra_version 7.0.0
 * @expected_result The driver connects and queries succeed with graceful
 *                  disconnect disabled.
 */
CASSANDRA_INTEGRATION_TEST_F(GracefulDisconnectTests, ConnectWithGracefulDisconnectDisabled) {
  CHECK_FAILURE;
  CHECK_VERSION(7.0.0);

  cluster_ = default_cluster();
  ASSERT_EQ(CASS_OK, cass_cluster_set_graceful_disconnect(cluster_.get(), cass_false));
  connect(cluster_);

  Result result = session_.execute(SELECT_ALL_SYSTEM_LOCAL_CQL);
  ASSERT_EQ(CASS_OK, result.error_code());
}
