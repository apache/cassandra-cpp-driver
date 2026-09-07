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

#include "socket_connector.hpp"

#include "config.hpp"
#include "logger.hpp"

#define SSL_HANDSHAKE_MAX_BUFFER_SIZE (16 * 1024 + 5)

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

namespace datastax { namespace internal { namespace core {

namespace {

// Used for debugging resolved addresses.
String to_string(const AddressVec& addresses) {
  String result;
  for (AddressVec::const_iterator it = addresses.begin(), end = addresses.end(); it != end; ++it) {
    if (!result.empty()) result.append(", ");
    result.append(it->to_string());
  }
  return result;
}

} // namespace

/**
 * A socket handler that handles the SSL handshake process.
 */
class SslHandshakeHandler : public SocketHandler {
public:
  SslHandshakeHandler(SocketConnector* connector)
      : connector_(connector) {}

  virtual void alloc_buffer(size_t suggested_size, uv_buf_t* buf) {
    buf->base = connector_->ssl_session_->incoming().peek_writable(&suggested_size);
    buf->len = suggested_size;
  }

  virtual void on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
      connector_->ssl_session_->incoming().commit(nread);
      connector_->ssl_handshake();
    }
  }

  virtual void on_write(Socket* socket, int status, SocketRequest* request) {
    delete request;
    if (status != 0) {
      connector_->on_error(SocketConnector::SOCKET_ERROR_WRITE, "Write error");
      return;
    }
    // TLS 1.3 can finish the handshake while this write was still queued.
    if (connector_->ssl_session_->is_handshake_done()) {
      connector_->ssl_handshake_finish();
    }
  }

  virtual void on_close() {
    if (connector_->is_canceled()) {
      connector_->finish();
    } else {
      connector_->on_error(SocketConnector::SOCKET_ERROR_CLOSE, "Socket closed prematurely");
    }
  }

private:
  SocketConnector* connector_;
};

}}} // namespace datastax::internal::core

SocketSettings::SocketSettings()
    : hostname_resolution_enabled(CASS_DEFAULT_HOSTNAME_RESOLUTION_ENABLED)
    , resolve_timeout_ms(CASS_DEFAULT_RESOLVE_TIMEOUT_MS)
    , tcp_nodelay_enabled(CASS_DEFAULT_TCP_NO_DELAY_ENABLED)
    , tcp_keepalive_enabled(CASS_DEFAULT_TCP_KEEPALIVE_ENABLED)
    , tcp_keepalive_delay_secs(CASS_DEFAULT_TCP_KEEPALIVE_DELAY_SECS)
    , max_reusable_write_objects(CASS_DEFAULT_MAX_REUSABLE_WRITE_OBJECTS) {}

SocketSettings::SocketSettings(const Config& config)
    : hostname_resolution_enabled(config.use_hostname_resolution())
    , resolve_timeout_ms(config.resolve_timeout_ms())
    , ssl_context(config.ssl_context())
    , tcp_nodelay_enabled(config.tcp_nodelay_enable())
    , tcp_keepalive_enabled(config.tcp_keepalive_enable())
    , tcp_keepalive_delay_secs(config.tcp_keepalive_delay_secs())
    , max_reusable_write_objects(config.max_reusable_write_objects())
    , local_address(config.local_address()) {}

Atomic<size_t> SocketConnector::resolved_address_offset_(0);

SocketConnector::SocketConnector(const Address& address, const Callback& callback)
    : address_(address)
    , callback_(callback)
    , error_code_(SOCKET_OK)
    , ssl_error_code_(CASS_OK) {}

SocketConnector* SocketConnector::with_settings(const SocketSettings& settings) {
  settings_ = settings;
  return this;
}

void SocketConnector::connect(uv_loop_t* loop) {
  inc_ref(); // For the event loop

  if (!address_.is_resolved()) { // Address not resolved
    hostname_ = address_.hostname_or_address();

    resolver_.reset(new Resolver(hostname_, address_.port(),
                                 bind_callback(&SocketConnector::on_resolve, this)));
    resolver_->resolve(loop, settings_.resolve_timeout_ms);
  } else {
    resolved_address_ = address_;

    if (settings_.hostname_resolution_enabled) { // Run hostname resolution then connect.
      name_resolver_.reset(
          new NameResolver(address_, bind_callback(&SocketConnector::on_name_resolve, this)));
      name_resolver_->resolve(loop, settings_.resolve_timeout_ms);
    } else {
      // Postpone the connection process until after this method ends because it
      // can call the callback (via on_error() when when the socket fails to
      // init/bind) and destroy its parent.
      no_resolve_timer_.start(loop,
                              0, // Run connect immediately after.
                              bind_callback(&SocketConnector::on_no_resolve, this));
    }
  }
}

void SocketConnector::cancel() {
  error_code_ = SOCKET_CANCELED;
  if (resolver_) resolver_->cancel();
  if (name_resolver_) name_resolver_->cancel();
  if (connector_) connector_->cancel();
  if (socket_) socket_->close();
}

Socket::Ptr SocketConnector::release_socket() {
  Socket::Ptr temp(socket_);
  socket_.reset();
  return temp;
}

void SocketConnector::internal_connect(uv_loop_t* loop) {
  Socket::Ptr socket(new Socket(resolved_address_, settings_.max_reusable_write_objects));

  if (uv_tcp_init(loop, socket->handle()) != 0) {
    on_error(SOCKET_ERROR_INIT, "Unable to initialize TCP object");
    return;
  }

  socket_ = socket;
  socket_->inc_ref(); // For the event loop

  // This needs to be done after setting the socket to properly cleanup.
  const Address& local_address = settings_.local_address;
  if (local_address.is_valid()) {
    Address::SocketStorage storage;
    int rc = uv_tcp_bind(socket->handle(), local_address.to_sockaddr(&storage), 0);
    if (rc != 0) {
      on_error(SOCKET_ERROR_BIND, "Unable to bind local address: " + String(uv_strerror(rc)));

      return;
    }
  }

  if (uv_tcp_nodelay(socket_->handle(), settings_.tcp_nodelay_enabled ? 1 : 0) != 0) {
    LOG_WARN("Unable to set tcp nodelay");
  }

  if (uv_tcp_keepalive(socket_->handle(), settings_.tcp_keepalive_enabled ? 1 : 0,
                       settings_.tcp_keepalive_delay_secs) != 0) {
    LOG_WARN("Unable to set tcp keepalive");
  }

  if (settings_.ssl_context) {
    ssl_session_.reset(settings_.ssl_context->create_session(resolved_address_, hostname_,
                                                             address_.server_name()));
  }

  connector_.reset(new TcpConnector(resolved_address_));
  connector_->connect(socket_->handle(), bind_callback(&SocketConnector::on_connect, this));
}

void SocketConnector::ssl_handshake() {
  // Run the handshake process if not done which might create outgoing data
  // which is handled below.
  if (!ssl_session_->is_handshake_done()) {
    ssl_session_->do_handshake();
    if (ssl_session_->has_error()) {
      on_error(SOCKET_ERROR_SSL_HANDSHAKE,
               "Error during SSL handshake: " + ssl_session_->error_message());
      return;
    }
  }

  // Write any outgoing data created by the handshake process. Finishing is
  // deferred to on_write() if the handshake is already done (e.g. TLS 1.3).
  char buf[SSL_HANDSHAKE_MAX_BUFFER_SIZE];
  size_t size = ssl_session_->outgoing().read(buf, SSL_HANDSHAKE_MAX_BUFFER_SIZE);
  if (size > 0) {
    socket_->write_and_flush(new BufferSocketRequest(Buffer(buf, size)));
  } else if (ssl_session_->is_handshake_done()) {
    ssl_handshake_finish();
  }
}

void SocketConnector::ssl_handshake_finish() {
  // If the handshake process is done then verify the certificate and finish.
  ssl_session_->verify();
  if (ssl_session_->has_error()) {
    on_error(SOCKET_ERROR_SSL_VERIFY,
             "Error verifying peer certificate: " + ssl_session_->error_message());
    return;
  }
  finish();
}

void SocketConnector::finish() {
  if (socket_) socket_->set_handler(NULL);
  callback_(this);
  // If the socket hasn't been released then close it.
  if (socket_) socket_->close();
  no_resolve_timer_.stop();
  dec_ref();
}

void SocketConnector::on_error(SocketError code, const String& message) {
  assert(code != SOCKET_OK && "Notified error without an error");
  if (error_code_ == SOCKET_OK) { // Only call this once
    LOG_DEBUG("Lost connection to host %s with the following error: %s",
              address_.to_string().c_str(), message.c_str());
    error_message_ = message;
    error_code_ = code;
    if (is_ssl_error()) {
      ssl_error_code_ = ssl_session_->error_code();
    }
    if (socket_) socket_->defunct();
    finish();
  }
}

void SocketConnector::on_connect(TcpConnector* tcp_connector) {
  if (tcp_connector->is_success()) {
    LOG_DEBUG("Connected to host %s on socket(%p)", address_.to_string().c_str(),
              static_cast<void*>(this));

#ifdef HAVE_NOSIGPIPE
    uv_tcp_t* tcp = &socket_->tcp_;

    // This must be done after the socket is initialized, which is done in the
    // connection process, for the socket file descriptor to be valid.
    uv_os_fd_t fd = 0;
    int enabled = 1;
    if (uv_fileno(reinterpret_cast<uv_handle_t*>(tcp), &fd) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, (void*)&enabled, sizeof(int)) != 0) {
      LOG_WARN("Unable to set socket option SO_NOSIGPIPE for host %s",
               address_.to_string().c_str());
    }
#endif

    if (ssl_session_) {
      socket_->set_handler(new SslHandshakeHandler(this));
      ssl_handshake();
    } else {
      finish();
    }
  } else if (is_canceled() || tcp_connector->is_canceled()) {
    finish();
  } else {
    on_error(SOCKET_ERROR_CONNECT,
             "Connect error '" + String(uv_strerror(tcp_connector->uv_status())) + "'");
  }
}

void SocketConnector::on_resolve(Resolver* resolver) {
  if (resolver->is_success()) {
    const AddressVec& addresses(resolver->addresses());
    LOG_DEBUG("Resolved the addresses %s for hostname %s", to_string(addresses).c_str(),
              hostname_.c_str());

    size_t offset = resolved_address_offset_.fetch_add(1, MEMORY_ORDER_RELAXED);
    resolved_address_ = Address(addresses[offset % addresses.size()],
                                address_.server_name()); // Keep the server name for debugging
    internal_connect(resolver->loop());
  } else if (is_canceled() || resolver->is_canceled()) {
    finish();
  } else if (resolver->is_timed_out()) {
    on_error(SOCKET_ERROR_RESOLVE_TIMEOUT, "Timed out attempting to resolve hostname");
  } else {
    on_error(SOCKET_ERROR_RESOLVE,
             "Unable to resolve hostname '" + String(uv_strerror(resolver->uv_status())) + "'");
  }
}

void SocketConnector::on_name_resolve(NameResolver* resolver) {
  if (resolver->is_success()) {
    LOG_DEBUG("Resolved the hostname %s for address %s", resolver->hostname().c_str(),
              resolver->address().to_string().c_str());
    const String& hostname = resolver->hostname();
    if (!hostname.empty() && hostname[hostname.size() - 1] == '.') {
      // Strip off trailing dot for hostcheck comparison
      hostname_ = hostname.substr(0, hostname.size() - 1);
    } else {
      hostname_ = hostname;
    }
    internal_connect(resolver->loop());
  } else if (is_canceled() || resolver->is_canceled()) {
    finish();
  } else if (resolver->is_timed_out()) {
    on_error(SOCKET_ERROR_RESOLVE_TIMEOUT, "Timed out attempting to resolve hostname");
  } else {
    on_error(SOCKET_ERROR_RESOLVE,
             "Unable to resolve hostname '" + String(uv_strerror(resolver->uv_status())) + "'");
  }
}

void SocketConnector::on_no_resolve(Timer* timer) {
  if (is_canceled()) {
    finish();
  } else {
    internal_connect(timer->loop());
  }
}
