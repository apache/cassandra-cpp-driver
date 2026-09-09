/*
  Copyright (c) 2014-2017 DataStax

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

#ifndef DATASTAX_INTERNAL_SOCKET_CONNECTOR_HPP
#define DATASTAX_INTERNAL_SOCKET_CONNECTOR_HPP

#include "atomic.hpp"
#include "callback.hpp"
#include "name_resolver.hpp"
#include "resolver.hpp"
#include "socket.hpp"
#include "tcp_connector.hpp"

namespace datastax { namespace internal { namespace core {

class Config;

/**
 * Socket settings.
 */
struct SocketSettings {
  /**
   * Constructor. Initialize with default settings.
   */
  SocketSettings();

  /**
   * Constructor. Initialize socket settings from a config object.
   *
   * @param config The config object.
   */
  SocketSettings(const Config& config);

  bool hostname_resolution_enabled;
  uint64_t resolve_timeout_ms;
  SslContext::Ptr ssl_context;
  bool tcp_nodelay_enabled;
  bool tcp_keepalive_enabled;
  unsigned tcp_keepalive_delay_secs;
  unsigned max_reusable_write_objects;
  Address local_address;
};

/**
 * A socket connector. This contains all the initialization code to connect a
 * socket.
 */
class SocketConnector : public RefCounted<SocketConnector> {
  friend class SslHandshakeHandler;

public:
  typedef SharedRefPtr<SocketConnector> Ptr;

  typedef internal::Callback<void, SocketConnector*> Callback;

  enum SocketError {
    SOCKET_OK,
    SOCKET_CANCELED,
    SOCKET_ERROR_CLOSE,
    SOCKET_ERROR_CONNECT,
    SOCKET_ERROR_INIT,
    SOCKET_ERROR_BIND,
    SOCKET_ERROR_RESOLVE,
    SOCKET_ERROR_RESOLVE_TIMEOUT,
    SOCKET_ERROR_SSL_HANDSHAKE,
    SOCKET_ERROR_SSL_VERIFY,
    SOCKET_ERROR_WRITE
  };

public:
  /**
   * Constructor
   *
   * @param address The address to connect to.
   * @param callback A callback that is called when the socket is connected or
   * if an error occurred.
   */
  SocketConnector(const Address& address, const Callback& callback);

  /**
   * Set the socket settings.
   *
   * @param settings The settings to use when connecting the socket.
   * @return The socket connector so calls can be chained.
   */
  SocketConnector* with_settings(const SocketSettings& settings);

  /**
   * Connect the socket.
   * @param loop An event loop to use for connecting the socket.
   */
  void connect(uv_loop_t* loop);

  /**
   * Cancel the connection process.
   */
  void cancel();

  /**
   * Release the socket from the connector. If not released in the callback
   * the socket automatically be closed.
   *
   * @return The socket object for this connector. This returns a null object
   * if the socket is not connected or an error occurred.
   */
  Socket::Ptr release_socket();

public:
  const String& hostname() { return hostname_; }

  ScopedPtr<SslSession>& ssl_session() { return ssl_session_; }

  SocketError error_code() { return error_code_; }
  const String& error_message() { return error_message_; }
  CassError ssl_error_code() { return ssl_error_code_; }

  bool is_ok() const { return error_code_ == SOCKET_OK; }

  bool is_canceled() const { return error_code_ == SOCKET_CANCELED; }

  bool is_ssl_error() const {
    return error_code_ == SOCKET_ERROR_SSL_HANDSHAKE || error_code_ == SOCKET_ERROR_SSL_VERIFY;
  }

private:
  void internal_connect(uv_loop_t* loop);
  void ssl_handshake();
  void ssl_handshake_finish();
  void finish();

  void on_error(SocketError code, const String& message);
  void on_connect(TcpConnector* tcp_connecter);
  void on_resolve(Resolver* resolver);
  void on_name_resolve(NameResolver* resolver);
  void on_no_resolve(Timer* timer);

private:
  static Atomic<size_t> resolved_address_offset_;

private:
  Address address_;
  Address resolved_address_;
  String hostname_;
  Callback callback_;

  Socket::Ptr socket_;
  TcpConnector::Ptr connector_;
  Resolver::Ptr resolver_;
  NameResolver::Ptr name_resolver_;
  Timer no_resolve_timer_;

  SocketError error_code_;
  String error_message_;
  CassError ssl_error_code_;

  ScopedPtr<SslSession> ssl_session_;

  SocketSettings settings_;
};

}}} // namespace datastax::internal::core

#endif
