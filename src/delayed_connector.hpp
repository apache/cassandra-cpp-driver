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

#ifndef DATASTAX_INTERNAL_DELAYED_CONNECTOR_HPP
#define DATASTAX_INTERNAL_DELAYED_CONNECTOR_HPP

#include "callback.hpp"
#include "connector.hpp"
#include "host.hpp"
#include "ref_counted.hpp"
#include "string.hpp"
#include "vector.hpp"

namespace datastax { namespace internal { namespace core {

class ConnectionPool;
class EventLoop;

/**
 * A connector that starts the connection process after some delay.
 */
class DelayedConnector : public RefCounted<DelayedConnector> {
public:
  typedef SharedRefPtr<DelayedConnector> Ptr;
  typedef Vector<Ptr> Vec;

  typedef internal::Callback<void, DelayedConnector*> Callback;

  /**
   * Constructor
   *
   * @param host The host to connect to.
   * @param protocol_version The protocol version to use for the connection.
   * @param callback A callback that is called when the connection is connected or
   * if an error occurred.
   */
  DelayedConnector(const Host::Ptr& host, ProtocolVersion protocol_version,
                   const Callback& callback);

  /**
   * Same as Connector::with_keyspace()
   *
   * @param keyspace
   * @return
   */
  DelayedConnector* with_keyspace(const String& keyspace);

  /**
   * Same as Connector::with_metrics()
   *
   * @param metrics
   * @return
   */
  DelayedConnector* with_metrics(Metrics* metrics);

  /**
   * Same as Connector::with_event_types()
   *
   * @param event_types A bit set of event types to register.
   * @return The connector to chain calls.
   */
  DelayedConnector* with_event_types(int event_types);

  /**
   * Same as Connector::with_settings()
   *
   * @param settings
   * @return
   */
  DelayedConnector* with_settings(const ConnectionSettings& settings);

  /**
   * Connect to a host after a delay.
   *
   * @param loop The event loop to run the timer and connection process.
   * @param wait_time_ms The amount of time to delay.
   */
  void delayed_connect(uv_loop_t* loop, uint64_t wait_time_ms);

  /**
   * Attempt immediate connection if the connector is currently waiting to
   * connect. If the connection process is canceled or already in-progress this
   * has no effect.
   */
  void attempt_immediate_connect();

  /**
   * Cancel the connection process.
   */
  void cancel();

  /**
   * Release the connection from the connector. If not released in the callback
   * the connection automatically be closed.
   *
   * @return The connection object for this connector. This returns a null object
   * if the connection is not connected or an error occurred.
   */
  Connection::Ptr release_connection();

public:
  bool is_canceled() const;
  bool is_ok() const;
  bool is_critical_error() const;
  bool is_keyspace_error() const;

  Connector::ConnectionError error_code() const;
  const String& error_message() const { return connector_->error_message(); }

private:
  void internal_connect(uv_loop_t* loop);

  void on_connect(Connector* connector);
  void on_delayed_connect(Timer* timer);

private:
  Connector::Ptr connector_;

  Callback callback_;

  Timer delayed_connect_timer_;
  bool is_canceled_;
};

}}} // namespace datastax::internal::core

#endif
