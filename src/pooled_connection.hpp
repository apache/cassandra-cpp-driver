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

#ifndef DATASTAX_INTERNAL_POOLED_CONNECTION_HPP
#define DATASTAX_INTERNAL_POOLED_CONNECTION_HPP

#include "atomic.hpp"
#include "connection.hpp"
#include "ref_counted.hpp"
#include "vector.hpp"

namespace datastax { namespace internal { namespace core {

class ConnectionPool;
class EventLoop;
class RequestCallback;

/**
 * A connection wrapper that handles connection pool functionality.
 */
class PooledConnection
    : public RefCounted<PooledConnection>
    , public ConnectionListener {
public:
  typedef SharedRefPtr<PooledConnection> Ptr;
  typedef Vector<Ptr> Vec;

  /**
   * Constructor. Don't use directly.
   *
   * @param pool The connection pool of the connection.
   * @param connection The wrapped connection.
   */
  PooledConnection(ConnectionPool* pool, const Connection::Ptr& connection);

  /**
   * Writes a request to a connection, but it's not written to the socket until
   * the connection pool manager flushes the request.
   *
   * @param callback A request callback that handles the request.
   * @return The number of bytes written, or negative if an error occurred.
   */
  int32_t write(RequestCallback* callback);

  /**
   * Flush pending writes.
   */
  void flush();

  /**
   * Closes the wrapped connection.
   */
  void close();

  /**
   * Start a graceful drain of the wrapped connection (CEP-59): in-flight
   * requests are allowed to complete before the connection is closed.
   */
  void start_graceful_drain();

  /**
   * Get the number of outstanding requests.
   *
   * @return The number of outstanding requests.
   */
  int inflight_request_count() const;

  /**
   * Determine if the connection is closing.
   *
   * @return Returns true if closing.
   */
  bool is_closing() const;

public:
  const String& keyspace() const { return connection_->keyspace(); } // Test only

private:
  virtual void on_event(const EventResponse::Ptr& response);
  virtual void on_read();
  virtual void on_write();
  virtual void on_close(Connection* connection);

private:
  const Connection::Ptr connection_;
  ConnectionPool* const pool_;
  EventLoop* const event_loop_;
};

}}} // namespace datastax::internal::core

#endif
