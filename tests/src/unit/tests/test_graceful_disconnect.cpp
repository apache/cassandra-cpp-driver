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

#include "loop_test.hpp"

#include "connection_pool_manager_initializer.hpp"
#include "connector.hpp"
#include "constants.hpp"
#include "control_connector.hpp"
#include "event_response.hpp"
#include "metrics.hpp"
#include "register_request.hpp"
#include "request_callback.hpp"
#include "timer.hpp"

using namespace datastax::internal;
using namespace datastax::internal::core;

// Coverage for GRACEFUL_DISCONNECT (CEP-59): capability negotiation against
// each connection's own SUPPORTED response, connection draining, pool
// draining, control connection processing, and metrics.
class GracefulDisconnectUnitTest : public LoopTest {
public:
  // A port that isn't used by the default mockssandra clusters so that these
  // tests can run alongside a locally running server.
  static const int TEST_PORT = 9142;

  // GRACEFUL_DISCONNECT (CEP-59) is only registered on protocol v5+.
  static const int GD_PROTOCOL_VERSION = CASS_PROTOCOL_VERSION_V5;

  static Address test_address() { return Address("127.0.0.1", TEST_PORT); }

  static Map<String, Vector<String> > options_with_graceful_disconnect(const String& value = "") {
    Map<String, Vector<String> > options;
    Vector<String> values;
    if (!value.empty()) {
      values.push_back(value);
    }
    options["GRACEFUL_DISCONNECT"] = values;
    return options;
  }

  class CountingRequestCallback : public SimpleRequestCallback {
  public:
    CountingRequestCallback(int* success_count, int* error_count)
        : SimpleRequestCallback("SELECT * FROM test")
        , success_count_(success_count)
        , error_count_(error_count) {}

    virtual void on_internal_set(ResponseMessage* response) {
      if (response->response_body()->opcode() == CQL_OPCODE_RESULT) {
        ++*success_count_;
      } else {
        ++*error_count_;
      }
    }

    virtual void on_internal_error(CassError code, const String& message) { ++*error_count_; }

    virtual void on_internal_timeout() { ++*error_count_; }

  private:
    int* success_count_;
    int* error_count_;
  };

  struct DrainState : public ConnectionListener {
    DrainState()
        : event_count(0)
        , was_draining_on_event(false)
        , write_result_during_drain(0)
        , request_success_count(0)
        , request_error_count(0)
        , is_closed(false)
        , write_during_drain(false) {}

    virtual void on_event(const EventResponse::Ptr& response) {
      if (response->event_type() == CASS_EVENT_GRACEFUL_DISCONNECT) {
        event_count++;
        was_draining_on_event = connection->is_draining();
        if (write_during_drain) {
          // New writes must be refused while draining.
          write_result_during_drain = connection->write(RequestCallback::Ptr(
              new CountingRequestCallback(&request_success_count, &request_error_count)));
        }
      }
    }

    virtual void on_close(Connection* connection_ptr) {
      is_closed = true;
      connection.reset();
    }

    Connection::Ptr connection;
    int event_count;
    bool was_draining_on_event;
    int32_t write_result_during_drain;
    int request_success_count;
    int request_error_count;
    bool is_closed;
    bool write_during_drain;
  };

  static void on_connection_connected(Connector* connector, DrainState* state) {
    ASSERT_TRUE(connector->is_ok());
    state->connection = connector->release_connection();
    state->connection->set_listener(state);
  }

  static void on_close_timer(Timer* timer, DrainState* state) {
    if (state->connection) {
      state->connection->close();
    }
  }
};

// Java: ProtocolInitHandlerGracefulDisconnectTest
//       .should_detect_capability_from_supported_options_map()
TEST_F(GracefulDisconnectUnitTest, DetectCapabilityFromSupportedOptions) {
  Map<String, Vector<String> > options;
  EXPECT_FALSE(Connector::supports_graceful_disconnect(options));

  Vector<String> cql_version;
  cql_version.push_back("3.4.7");
  options["CQL_VERSION"] = cql_version;
  EXPECT_FALSE(Connector::supports_graceful_disconnect(options));

  options["GRACEFUL_DISCONNECT"] = Vector<String>();
  EXPECT_TRUE(Connector::supports_graceful_disconnect(options));

  Vector<String> values;
  values.push_back("true");
  options["GRACEFUL_DISCONNECT"] = values;
  EXPECT_TRUE(Connector::supports_graceful_disconnect(options));

  values.clear();
  values.push_back("false");
  options["GRACEFUL_DISCONNECT"] = values;
  EXPECT_FALSE(Connector::supports_graceful_disconnect(options));

  values.clear();
  values.push_back("FALSE");
  options["GRACEFUL_DISCONNECT"] = values;
  EXPECT_FALSE(Connector::supports_graceful_disconnect(options));
}

// Java: GracefulDisconnectWireCompatTest.should_decode_v4_envelope()
TEST_F(GracefulDisconnectUnitTest, DecodeEventResponse) {
  const char* event_type = "GRACEFUL_DISCONNECT";
  String body;
  body.push_back(0x00);
  body.push_back(static_cast<char>(strlen(event_type)));
  body.append(event_type);

  EventResponse response;
  Decoder decoder(body.data(), body.size(),
                  ProtocolVersion(CASS_PROTOCOL_VERSION_V4));
  ASSERT_TRUE(response.decode(decoder));
  EXPECT_EQ(response.event_type(), CASS_EVENT_GRACEFUL_DISCONNECT);
}

// Java: GracefulDisconnectWireCompatTest.should_reject_unknown_event_type()
TEST_F(GracefulDisconnectUnitTest, RejectUnknownEventType) {
  const char* event_type = "GRACEFUL_DISCONNECT_V2";
  String body;
  body.push_back(0x00);
  body.push_back(static_cast<char>(strlen(event_type)));
  body.append(event_type);

  EventResponse response;
  Decoder decoder(body.data(), body.size(),
                  ProtocolVersion(CASS_PROTOCOL_VERSION_V4));
  EXPECT_FALSE(response.decode(decoder));
}

// Java: ProtocolInitHandlerGracefulDisconnectTest
//       .should_register_graceful_disconnect_when_advertised_in_supported()
//       (REGISTER encoding part)
TEST_F(GracefulDisconnectUnitTest, EncodeRegisterRequest) {
  RegisterRequest register_request(CASS_EVENT_SCHEMA_CHANGE | CASS_EVENT_GRACEFUL_DISCONNECT);
  const Request& request = register_request;

  BufferVec bufs;
  int size = request.encode(ProtocolVersion(CASS_PROTOCOL_VERSION_V4), NULL, &bufs);
  ASSERT_GT(size, 0);
  ASSERT_EQ(bufs.size(), 1u);

  String body(bufs[0].data(), bufs[0].size());
  EXPECT_NE(body.find("SCHEMA_CHANGE"), String::npos);
  EXPECT_NE(body.find("GRACEFUL_DISCONNECT"), String::npos);
}

// Java: InFlightHandlerTest.should_close_immediately_on_graceful_disconnect_if_no_pending()
TEST_F(GracefulDisconnectUnitTest, CloseImmediatelyWhenIdle) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  DrainState state;
  Connector::Ptr connector(new Connector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                                         bind_callback(on_connection_connected, &state)));
  connector->with_event_types(CASS_EVENT_GRACEFUL_DISCONNECT)->connect(loop());
  uv_run(loop(), UV_RUN_ONCE);
  while (!state.connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.connection);

  cluster.event(mockssandra::GracefulDisconnectEvent::create());
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(state.event_count, 1);
  EXPECT_TRUE(state.was_draining_on_event);
  EXPECT_TRUE(state.is_closed);
}

// Java: InFlightHandlerTest.should_initiate_graceful_drain_on_graceful_disconnect_event()
TEST_F(GracefulDisconnectUnitTest, DrainInFlightRequestsBeforeClosing) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  builder.on(mockssandra::OPCODE_QUERY).wait(300).void_result();
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  DrainState state;
  state.write_during_drain = true;
  Connector::Ptr connector(new Connector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                                         bind_callback(on_connection_connected, &state)));
  connector->with_event_types(CASS_EVENT_GRACEFUL_DISCONNECT)->connect(loop());
  while (!state.connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.connection);

  // Write a request that's pending while the event arrives (the server delays
  // the response).
  ASSERT_GT(state.connection->write_and_flush(
                RequestCallback::Ptr(new CountingRequestCallback(
                    &state.request_success_count, &state.request_error_count))),
            0);
  cluster.event(mockssandra::GracefulDisconnectEvent::create());
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(state.event_count, 1);
  EXPECT_TRUE(state.was_draining_on_event);
  // The write attempted during the drain must have been refused.
  EXPECT_EQ(state.write_result_during_drain, Request::REQUEST_ERROR_CONNECTION_DRAINING);
  // The in-flight request completed successfully; the shutdown is invisible
  // to the application.
  EXPECT_EQ(state.request_success_count, 1);
  EXPECT_EQ(state.request_error_count, 0);
  // The connection closed once the drain completed.
  EXPECT_TRUE(state.is_closed);
}

// Java: ProtocolInitHandlerGracefulDisconnectTest
//       .should_skip_register_when_graceful_disconnect_was_the_only_event_type()
TEST_F(GracefulDisconnectUnitTest, NoRegistrationWhenNotAdvertised) {
  mockssandra::SimpleCluster cluster(simple(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  DrainState state;
  Connector::Ptr connector(new Connector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                                         bind_callback(on_connection_connected, &state)));
  connector->with_event_types(CASS_EVENT_GRACEFUL_DISCONNECT)->connect(loop());
  while (!state.connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.connection);

  // The server never advertised the capability so the driver must not have
  // registered; the event is not delivered and the connection stays open.
  cluster.event(mockssandra::GracefulDisconnectEvent::create());

  Timer timer;
  timer.start(loop(), 200, bind_callback(on_close_timer, &state));
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(state.event_count, 0);
}

// Java: ProtocolInitHandlerGracefulDisconnectTest
//       .should_not_register_graceful_disconnect_when_advertised_as_false()
TEST_F(GracefulDisconnectUnitTest, NoRegistrationWhenAdvertisedFalse) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("false"));
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  DrainState state;
  Connector::Ptr connector(new Connector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                                         bind_callback(on_connection_connected, &state)));
  connector->with_event_types(CASS_EVENT_GRACEFUL_DISCONNECT)->connect(loop());
  while (!state.connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.connection);

  cluster.event(mockssandra::GracefulDisconnectEvent::create());

  Timer timer;
  timer.start(loop(), 200, bind_callback(on_close_timer, &state));
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(state.event_count, 0);
}

// Pool tests. Java: ChannelPoolGracefulDisconnectTest
class GracefulDisconnectPoolUnitTest : public GracefulDisconnectUnitTest {
public:
  struct PoolState {
    PoolState(uv_loop_t* loop)
        : loop(loop)
        , request_success_count(0)
        , request_error_count(0)
        , connection_was_closed(false)
        , sent_event(false) {}

    ~PoolState() {
      ConnectionPoolManager::Ptr temp(manager);
      if (temp) temp->close();
      uv_run(loop, UV_RUN_DEFAULT); // Allow the loop to cleanup
    }

    uv_loop_t* loop;
    ConnectionPoolManager::Ptr manager;
    PooledConnection::Ptr connection;
    mockssandra::SimpleCluster* cluster;
    int request_success_count;
    int request_error_count;
    bool connection_was_closed;
    bool sent_event;
    Timer timer;
  };

  class PoolRequestCallback : public SimpleRequestCallback {
  public:
    PoolRequestCallback(PoolState* state)
        : SimpleRequestCallback("SELECT * FROM test")
        , state_(state) {}

    virtual void on_internal_set(ResponseMessage* response) {
      if (response->response_body()->opcode() == CQL_OPCODE_RESULT) {
        state_->request_success_count++;
      } else {
        state_->request_error_count++;
      }
    }

    virtual void on_internal_error(CassError code, const String& message) {
      state_->request_error_count++;
    }

    virtual void on_internal_timeout() { state_->request_error_count++; }

  private:
    PoolState* state_;
  };

  static void on_pool_connected(ConnectionPoolManagerInitializer* initializer, PoolState* state) {
    state->manager = initializer->release_manager();
    state->connection = state->manager->find_least_busy(test_address());
    ASSERT_TRUE(state->connection);

    // Write a request that's pending while the event arrives (the server
    // delays the response).
    ASSERT_GT(state->connection->write(new PoolRequestCallback(state)), 0);
    state->manager->flush();

    state->cluster->event(mockssandra::GracefulDisconnectEvent::create());
    state->sent_event = true;

    // Give the drain time to complete, then finish the test.
    state->timer.start(state->loop, 500, bind_callback(on_done_timer, state));
  }

  static void on_done_timer(Timer* timer, PoolState* state) {
    state->connection_was_closed = state->connection && state->connection->is_closing();
    ConnectionPoolManager::Ptr temp(state->manager);
    state->manager.reset();
    state->connection.reset();
    if (temp) temp->close();
  }
};

// Java: ChannelPoolGracefulDisconnectTest
//       .should_drain_and_increment_metrics_when_event_received_on_query_connection()
//       and .should_close_all_channels_when_graceful_disconnect_event_for_node()
TEST_F(GracefulDisconnectPoolUnitTest, DrainPooledConnectionsAndIncrementMetrics) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  builder.on(mockssandra::OPCODE_QUERY).wait(300).void_result();
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  Metrics metrics(1, CASS_DEFAULT_HISTOGRAM_REFRESH_INTERVAL_NO_REFRESH);

  PoolState state(loop());
  state.cluster = &cluster;

  HostMap hosts;
  Host::Ptr host(new Host(test_address()));
  hosts[host->address()] = host;

  ConnectionPoolSettings settings; // graceful disconnect is enabled by default
  settings.num_connections_per_host = 1;

  ConnectionPoolManagerInitializer::Ptr initializer(new ConnectionPoolManagerInitializer(
      GD_PROTOCOL_VERSION, bind_callback(on_pool_connected, &state)));
  initializer->with_settings(settings)->with_metrics(&metrics)->initialize(loop(), hosts);
  uv_run(loop(), UV_RUN_DEFAULT);

  ASSERT_TRUE(state.sent_event);
  // The in-flight request completed successfully; the shutdown is invisible
  // to the application.
  EXPECT_EQ(state.request_success_count, 1);
  EXPECT_EQ(state.request_error_count, 0);
  // The pooled connection was drained and closed.
  EXPECT_TRUE(state.connection_was_closed);
  // The metric was incremented for the received event.
  EXPECT_GE(metrics.graceful_disconnects.sum(), 1);
}

// Java: ChannelPoolGracefulDisconnectTest
//       .should_not_request_graceful_disconnect_events_when_disabled()
TEST_F(GracefulDisconnectPoolUnitTest, IgnoreEventsWhenDisabled) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  builder.on(mockssandra::OPCODE_QUERY).wait(300).void_result();
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  Metrics metrics(1, CASS_DEFAULT_HISTOGRAM_REFRESH_INTERVAL_NO_REFRESH);

  PoolState state(loop());
  state.cluster = &cluster;

  HostMap hosts;
  Host::Ptr host(new Host(test_address()));
  hosts[host->address()] = host;

  ConnectionPoolSettings settings;
  settings.num_connections_per_host = 1;
  settings.graceful_disconnect = false;

  ConnectionPoolManagerInitializer::Ptr initializer(new ConnectionPoolManagerInitializer(
      GD_PROTOCOL_VERSION, bind_callback(on_pool_connected, &state)));
  initializer->with_settings(settings)->with_metrics(&metrics)->initialize(loop(), hosts);
  uv_run(loop(), UV_RUN_DEFAULT);

  ASSERT_TRUE(state.sent_event);
  EXPECT_EQ(state.request_success_count, 1);
  EXPECT_EQ(state.request_error_count, 0);
  // The pool did not register for the event so the connection is not drained.
  EXPECT_FALSE(state.connection_was_closed);
  EXPECT_EQ(metrics.graceful_disconnects.sum(), 0);
}

// Control connection tests. Java: ControlConnectionEventsTest
class GracefulDisconnectControlConnectionUnitTest : public GracefulDisconnectUnitTest {
public:
  struct ControlState : public ControlConnectionListener {
    ControlState()
        : is_closed(false)
        , was_closed_by_event(false) {}

    virtual void on_up(const Address& address) {}
    virtual void on_down(const Address& address) {}
    virtual void on_add(const Host::Ptr& host) {}
    virtual void on_remove(const Address& address) {}
    virtual void on_update_schema(SchemaType type, const ResultResponse::Ptr& result,
                                  const String& keyspace_name, const String& target_name) {}
    virtual void on_drop_schema(SchemaType type, const String& keyspace_name,
                                const String& target_name) {}

    virtual void on_close(ControlConnection* connection) {
      is_closed = true;
      control_connection.reset();
    }

    ControlConnection::Ptr control_connection;
    bool is_closed;
    bool was_closed_by_event;
  };

  static void on_control_connected(ControlConnector* connector, ControlState* state) {
    ASSERT_TRUE(connector->is_ok());
    state->control_connection = connector->release_connection();
    state->control_connection->set_listener(state);
  }

  static void on_control_close_timer(Timer* timer, ControlState* state) {
    // Record whether the event already closed the control connection, then
    // close it to end the test.
    state->was_closed_by_event = state->is_closed;
    if (state->control_connection) {
      state->control_connection->close();
    }
  }
};

// Java: ControlConnectionEventsTest.should_process_graceful_disconnect_event()
TEST_F(GracefulDisconnectControlConnectionUnitTest, DrainControlConnectionAndIncrementMetrics) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  Metrics metrics(1, CASS_DEFAULT_HISTOGRAM_REFRESH_INTERVAL_NO_REFRESH);

  ControlState state;
  ControlConnector::Ptr connector(
      new ControlConnector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                           bind_callback(on_control_connected, &state)));
  connector->with_metrics(&metrics)->connect(loop());
  while (!state.control_connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.control_connection);

  // The control connection registered for GRACEFUL_DISCONNECT (enabled by
  // default and advertised by the server); the event drains its connection
  // and the control connection closes.
  cluster.event(mockssandra::GracefulDisconnectEvent::create());
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(state.is_closed);
  EXPECT_EQ(metrics.graceful_disconnects.sum(), 1);
}

// Java: ControlConnectionEventsTest.should_not_register_for_graceful_disconnect_when_disabled()
TEST_F(GracefulDisconnectControlConnectionUnitTest, NoRegistrationWhenDisabled) {
  mockssandra::SimpleRequestHandlerBuilder builder;
  builder.on(mockssandra::OPCODE_OPTIONS).supported(options_with_graceful_disconnect("true"));
  mockssandra::SimpleCluster cluster(builder.build(), 1, 0, TEST_PORT);
  ASSERT_EQ(cluster.start_all(), 0);

  ControlState state;
  ControlConnector::Ptr connector(
      new ControlConnector(Host::Ptr(new Host(test_address())), GD_PROTOCOL_VERSION,
                           bind_callback(on_control_connected, &state)));
  ControlConnectionSettings settings;
  settings.graceful_disconnect = false;
  connector->with_settings(settings)->connect(loop());
  while (!state.control_connection && !state.is_closed) {
    uv_run(loop(), UV_RUN_ONCE);
  }
  ASSERT_TRUE(state.control_connection);

  cluster.event(mockssandra::GracefulDisconnectEvent::create());

  // The event is delivered (the control connection registers for other event
  // types), but GRACEFUL_DISCONNECT was not registered so the connection must
  // stay open.
  Timer timer;
  timer.start(loop(), 200, bind_callback(on_control_close_timer, &state));
  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_FALSE(state.was_closed_by_event);
}
