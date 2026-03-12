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

#include "event_response.hpp"
#include "request_callback.hpp"
#include "socket.hpp"
#include "stream_manager.hpp"

#ifndef DATASTAX_INTERNAL_CONNECTION_HPP
#define DATASTAX_INTERNAL_CONNECTION_HPP

namespace datastax { namespace internal { namespace core {

class ResponseMessage;
class EventResponse;
class Connection;

/**
 * A proxy socket handler for the connection.
 */
class ConnectionHandler : public SocketHandler {
public:
  ConnectionHandler(Connection* connection)
      : connection_(connection) {}

  virtual void on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf);
  virtual void on_write(Socket* socket, int status, SocketRequest* request);
  virtual void on_close();

private:
  Connection* connection_;
};

/**
 * A proxy SSL socket handler for the connection.
 */
class SslConnectionHandler : public SslSocketHandler {
public:
  SslConnectionHandler(SslSession* ssl_session, Connection* connection)
      : SslSocketHandler(ssl_session)
      , connection_(connection) {}

  virtual void on_ssl_read(Socket* socket, char* buf, size_t size);
  virtual void on_write(Socket* socket, int status, SocketRequest* request);
  virtual void on_close();

private:
  Connection* connection_;
};

/**
 * A listener that handles events for the connection.
 */
class ConnectionListener {
public:
  virtual ~ConnectionListener() {}

  /**
   * A callback that's called when the connection receives an event. The
   * connection must register for events when connected.
   *
   * @param response The event response data sent from the server.
   */
  virtual void on_event(const EventResponse::Ptr& response) {}

  virtual void on_read() {}

  virtual void on_write() {}

  /**
   * A callback that's called when the connection closes.
   *
   * @param connection The closing connection.
   */
  virtual void on_close(Connection* connection) {}
};

/**
 * A listener that handles recording events to be processed later.
 */
class RecordingConnectionListener : public ConnectionListener {
public:
  const EventResponse::Vec& events() const { return events_; }

  virtual void on_event(const EventResponse::Ptr& response) { events_.push_back(response); }

  virtual void on_close(Connection* connection) = 0;

  /**
   * Process the recorded events through a connection listener.
   *
   * @param events The events to replay.
   * @param listener The listener that will receive the events.
   */
  static void process_events(const EventResponse::Vec& events, ConnectionListener* listener);

private:
  EventResponse::Vec events_;
};

/**
 * A connection. It's a socket wrapper that handles Cassandra/DSE specific
 * functionality such as decoding responses and heartbeats. It can not be
 * connected directly instead use a Connector object.
 *
 * @see Connector
 */
class Connection : public RefCounted<Connection> {
  friend class ConnectionConnector;
  friend class ConnectionHandler;
  friend class SslConnectionHandler;
  friend class HeartbeatCallback;

public:
  typedef SharedRefPtr<Connection> Ptr;
  typedef Vector<Ptr> Vec;

  /**
   * Constructor. Don't use directly.
   *
   * @param socket The wrapped socket.
   * @param host The host associated with the connection.
   * @param protocol_version The protocol version to use for the connection.
   * @param idle_timeout_secs The amount of time (in seconds) without a write or heartbeat
   * where the connection is considered idle and is terminated.
   * @param heartbeat_interval_secs The interval (in seconds) to send a heartbeat.
   */
  Connection(const Socket::Ptr& socket, const Host::Ptr& host, ProtocolVersion protocol_version,
             unsigned int idle_timeout_secs, unsigned int heartbeat_interval_secs);
  ~Connection();

  /**
   * Write a request to the connection and coalesce with outstanding requests. This
   * method doesn't flush.
   *
   * @param callback A request callback that will handle the request.
   * @return The number of bytes written, or negative if an error occurred.
   */
  int32_t write(const RequestCallback::Ptr& callback);

  /**
   * Write a request to the connection and flush immediately.
   *
   * @param callback The request callback that will handle the request.
   * @return The number of bytes written, or negative if an error occurred.
   */
  int32_t write_and_flush(const RequestCallback::Ptr& callback);

  /**
   * Flush all outstanding requests.
   */
  size_t flush();

  /**
   * Determine if the connection is closing.
   *
   * @return Returns true if closing.
   */
  bool is_closing() const { return socket_->is_closing(); }

  /**
   * Close the connection.
   */
  void close();

  /**
   * Determine if the connection is defunct.
   *
   * @return Returns true if defunct.
   */
  bool is_defunct() const { return socket_->is_defunct(); }

  /**
   * Mark as defunct and close the connection.
   */
  void defunct();

  /**
   * Set the listener that will handle events for the connection.
   *
   * @param listener The connection listener.
   */
  void set_listener(ConnectionListener* listener = NULL);

  /**
   * Start heartbeats to keep the connection alive and to detect a network or
   * server-side failure.
   */
  void start_heartbeats();

public:
  const Address& address() const { return host_->address(); }
  const String& address_string() const { return host_->address_string(); }
  const Address& resolved_address() const { return socket_->address(); }
  const Host::Ptr& host() const { return host_; }
  ProtocolVersion protocol_version() const { return protocol_version_; }
  const String& keyspace() { return keyspace_; }
  uv_loop_t* loop() { return socket_->loop(); }
  const uv_tcp_t* handle() const { return socket_->handle(); }

  int inflight_request_count() const { return inflight_request_count_.load(MEMORY_ORDER_RELAXED); }

private:
  void maybe_set_keyspace(ResponseMessage* response);

  void on_write(int status, RequestCallback* request);
  void on_read(const char* buf, size_t size);
  void on_close();

private:
  void restart_heartbeat_timer();
  void on_heartbeat(Timer* timer);

  void restart_terminate_timer();
  void on_terminate(Timer* timer);

private:
  Socket::Ptr socket_;
  const Host::Ptr host_;
  StreamManager<RequestCallback::Ptr> stream_manager_;
  Atomic<int> inflight_request_count_;

  List<SocketRequest> pending_reads_;
  ScopedPtr<ResponseMessage> response_;

  ConnectionListener* listener_;

  ProtocolVersion protocol_version_;
  String keyspace_;

  unsigned int idle_timeout_secs_;
  unsigned int heartbeat_interval_secs_;
  bool heartbeat_outstanding_;
  Timer heartbeat_timer_;
  Timer terminate_timer_;
};

}}} // namespace datastax::internal::core

#endif
