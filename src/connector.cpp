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

#include "connector.hpp"

#include "config.hpp"

#include "auth_responses.hpp"
#include "connection.hpp"
#include "metrics.hpp"
#include "serialization.hpp"
#include "supported_response.hpp"

#include "auth_requests.hpp"
#include "options_request.hpp"
#include "query_request.hpp"
#include "register_request.hpp"
#include "request.hpp"
#include "startup_request.hpp"

#include "response.hpp"
#include "result_response.hpp"

#include <iomanip>

using namespace datastax;
using namespace datastax::internal::core;

namespace datastax { namespace internal { namespace core {

/**
 * A proxy request callback that handles the connection process.
 */
class StartupCallback : public SimpleRequestCallback {
public:
  StartupCallback(Connector* connector, const Request::ConstPtr& request);

private:
  virtual void on_internal_set(ResponseMessage* response);
  virtual void on_internal_error(CassError code, const String& message);
  virtual void on_internal_timeout();

  void on_result_response(ResponseMessage* response);

private:
  Connector* connector_;
};

}}} // namespace datastax::internal::core

StartupCallback::StartupCallback(Connector* connector, const Request::ConstPtr& request)
    : SimpleRequestCallback(request, connector->settings_.connect_timeout_ms)
    , connector_(connector) {}

void StartupCallback::on_internal_set(ResponseMessage* response) {
  switch (response->opcode()) {
    case CQL_OPCODE_SUPPORTED:
      connector_->on_supported(response);
      break;

    case CQL_OPCODE_ERROR: {
      ErrorResponse* error = static_cast<ErrorResponse*>(response->response_body().get());
      Connector::ConnectionError error_code = Connector::CONNECTION_ERROR_RESPONSE;
      if (error->code() == CQL_ERROR_PROTOCOL_ERROR &&
          error->message().find("Invalid or unsupported protocol version") != StringRef::npos) {
        error_code = Connector::CONNECTION_ERROR_INVALID_PROTOCOL;
      } else if (error->code() == CQL_ERROR_BAD_CREDENTIALS) {
        error_code = Connector::CONNECTION_ERROR_AUTH;
      } else if (error->code() == CQL_ERROR_INVALID_QUERY &&
                 error->message().find("Keyspace") == 0 &&
                 error->message().find("does not exist") != StringRef::npos) {
        error_code = Connector::CONNECTION_ERROR_KEYSPACE;
      }
      connector_->on_error(error_code, "Received error response " + error->error_message());
      break;
    }

    case CQL_OPCODE_AUTHENTICATE: {
      AuthenticateResponse* auth =
          static_cast<AuthenticateResponse*>(response->response_body().get());
      connector_->on_authenticate(auth->class_name());
      break;
    }

    case CQL_OPCODE_AUTH_CHALLENGE:
      connector_->on_auth_challenge(
          static_cast<const AuthResponseRequest*>(request()),
          static_cast<AuthChallengeResponse*>(response->response_body().get())->token());
      break;

    case CQL_OPCODE_AUTH_SUCCESS:
      connector_->on_auth_success(
          static_cast<const AuthResponseRequest*>(request()),
          static_cast<AuthSuccessResponse*>(response->response_body().get())->token());
      break;

    case CQL_OPCODE_READY:
      connector_->on_ready_or_register_for_events();
      break;

    case CQL_OPCODE_RESULT:
      on_result_response(response);
      break;

    default:
      connector_->on_error(Connector::CONNECTION_ERROR_INVALID_OPCODE, "Invalid opcode");
      break;
  }
}

void StartupCallback::on_internal_error(CassError code, const String& message) {
  // Ignore timeouts caused by the connection closing
  if (connector_->connection_->is_closing() && code == CASS_ERROR_LIB_REQUEST_TIMED_OUT) {
    return;
  }
  OStringStream ss;
  ss << "Error: '" << message << "' (0x" << std::hex << std::uppercase << std::setw(8)
     << std::setfill('0') << code << ")";
  connector_->on_error(Connector::CONNECTION_ERROR_INTERNAL, ss.str());
}

void StartupCallback::on_internal_timeout() {
  connector_->on_error(Connector::CONNECTION_ERROR_TIMEOUT, "Timed out");
}

void StartupCallback::on_result_response(ResponseMessage* response) {
  ResultResponse* result = static_cast<ResultResponse*>(response->response_body().get());
  switch (result->kind()) {
    case CASS_RESULT_KIND_SET_KEYSPACE:
      connector_->finish();
      break;
    default:
      connector_->on_error(Connector::CONNECTION_ERROR_KEYSPACE,
                           "Invalid result response. Expected set keyspace.");
      break;
  }
}

ConnectionSettings::ConnectionSettings()
    : connect_timeout_ms(CASS_DEFAULT_CONNECT_TIMEOUT_MS)
    , auth_provider(new AuthProvider())
    , idle_timeout_secs(CASS_DEFAULT_IDLE_TIMEOUT_SECS)
    , heartbeat_interval_secs(CASS_DEFAULT_HEARTBEAT_INTERVAL_SECS)
    , no_compact(CASS_DEFAULT_NO_COMPACT) {}

ConnectionSettings::ConnectionSettings(const Config& config)
    : socket_settings(config)
    , connect_timeout_ms(config.connect_timeout_ms())
    , auth_provider(config.auth_provider())
    , idle_timeout_secs(config.connection_idle_timeout_secs())
    , heartbeat_interval_secs(config.connection_heartbeat_interval_secs())
    , no_compact(config.no_compact())
    , application_name(config.application_name())
    , application_version(config.application_version()) {}

Connector::Connector(const Host::Ptr& host, ProtocolVersion protocol_version,
                     const Callback& callback)
    : callback_(callback)
    , loop_(NULL)
    , host_(host)
    , socket_connector_(
          new SocketConnector(host->address(), bind_callback(&Connector::on_connect, this)))
    , error_code_(CONNECTION_OK)
    , protocol_version_(protocol_version)
    , event_types_(0)
    , listener_(NULL)
    , metrics_(NULL) {}

Connector* Connector::with_keyspace(const String& keyspace) {
  keyspace_ = keyspace;
  return this;
}

Connector* Connector::with_event_types(int event_types) {
  event_types_ = event_types;
  return this;
}

Connector* Connector::with_listener(ConnectionListener* listener) {
  listener_ = listener;
  return this;
}

Connector* Connector::with_metrics(Metrics* metrics) {
  metrics_ = metrics;
  return this;
}

Connector* Connector::with_settings(const ConnectionSettings& settings) {
  settings_ = settings;
  // Only use hostname resolution if actually required for SSL or
  // authentication.
  settings_.socket_settings.hostname_resolution_enabled =
      settings.socket_settings.hostname_resolution_enabled &&
      (settings.auth_provider || settings.socket_settings.ssl_context);
  return this;
}

void Connector::connect(uv_loop_t* loop) {
  inc_ref(); // For the event loop
  loop_ = loop;
  socket_connector_->with_settings(settings_.socket_settings)->connect(loop);
  if (settings_.connect_timeout_ms > 0) {
    timer_.start(loop, settings_.connect_timeout_ms, bind_callback(&Connector::on_timeout, this));
  }
}

void Connector::cancel() {
  error_code_ = CONNECTION_CANCELED;
  socket_connector_->cancel();
  if (connection_) connection_->close();
}

Connection::Ptr Connector::release_connection() {
  Connection::Ptr temp(connection_);
  connection_.reset();
  return temp;
}

void Connector::finish() {
  timer_.stop();
  if (connection_) {
    connection_->set_listener(is_ok() ? listener_ : NULL);
  }
  callback_(this);
  if (connection_) {
    // If the callback doesn't take possession of the connection then we should
    // also clear the listener.
    connection_->set_listener();
    connection_->close();
  }
  dec_ref();
}

void Connector::on_error(ConnectionError code, const String& message) {
  assert(code != CONNECTION_OK && "Notified error without an error");
  LOG_DEBUG("Unable to connect to host %s because of the following error: %s",
            address().to_string().c_str(), message.c_str());
  if (error_code_ == CONNECTION_OK) { // Only perform this once
    error_message_ = message;
    error_code_ = code;
    if (connection_) connection_->defunct();
    finish();
  }
}

void Connector::on_ready_or_set_keyspace() {
  if (keyspace_.empty()) {
    finish();
  } else {
    connection_->write_and_flush(RequestCallback::Ptr(
        new StartupCallback(this, Request::ConstPtr(new QueryRequest("USE " + keyspace_)))));
  }
}

bool Connector::supports_graceful_disconnect(const StringMultimap& supported_options) {
  StringMultimap::const_iterator it = supported_options.find("GRACEFUL_DISCONNECT");
  if (it == supported_options.end()) {
    return false;
  }
  const Vector<String>& values = it->second;
  for (Vector<String>::const_iterator vit = values.begin(), end = values.end(); vit != end;
       ++vit) {
    if (iequals(StringRef(*vit), StringRef("false"))) {
      return false;
    }
  }
  return true;
}

void Connector::on_ready_or_register_for_events() {
  // GRACEFUL_DISCONNECT (CEP-59) support is negotiated per connection: only
  // register if this connection's SUPPORTED response advertised it, and only
  // on protocol version 5 or above. The server may advertise the capability
  // in SUPPORTED on older protocol versions but reject the REGISTER, so the
  // protocol version must be checked as well.
  if ((event_types_ & CASS_EVENT_GRACEFUL_DISCONNECT) != 0 &&
      (!supports_graceful_disconnect(supported_options_) ||
       protocol_version_ < ProtocolVersion(CASS_PROTOCOL_VERSION_V5))) {
    event_types_ &= ~CASS_EVENT_GRACEFUL_DISCONNECT;
  }
  if (event_types_ != 0) {
    connection_->write_and_flush(RequestCallback::Ptr(
        new StartupCallback(this, Request::ConstPtr(new RegisterRequest(event_types_)))));
    // REGISTER requests also returns a READY response so this needs to be reset
    // to prevent a loop.
    event_types_ = 0;
  } else {
    on_ready_or_set_keyspace();
  }
}

void Connector::on_supported(ResponseMessage* response) {
  SupportedResponse* supported = static_cast<SupportedResponse*>(response->response_body().get());
  supported_options_ = supported->supported_options();

  connection_->write_and_flush(RequestCallback::Ptr(new StartupCallback(
      this, Request::ConstPtr(new StartupRequest(settings_.application_name,
                                                 settings_.application_version, settings_.client_id,
                                                 settings_.no_compact)))));
}

void Connector::on_authenticate(const String& class_name) {
  Authenticator::Ptr auth(settings_.auth_provider->new_authenticator(
      host_->address(), socket_connector_->hostname(), class_name));
  if (!auth) {
    on_error(CONNECTION_ERROR_AUTH, "Authentication required but no auth provider set");
  } else {
    String response;
    if (!auth->initial_response(&response)) {
      on_error(CONNECTION_ERROR_AUTH, "Failed creating initial response token: " + auth->error());
      return;
    }
    connection_->write_and_flush(RequestCallback::Ptr(
        new StartupCallback(this, Request::ConstPtr(new AuthResponseRequest(response, auth)))));
  }
}

void Connector::on_auth_challenge(const AuthResponseRequest* request, const String& token) {
  String response;
  if (!request->auth()->evaluate_challenge(token, &response)) {
    on_error(CONNECTION_ERROR_AUTH,
             "Failed evaluating challenge token: " + request->auth()->error());
    return;
  }
  connection_->write_and_flush(RequestCallback::Ptr(new StartupCallback(
      this, Request::ConstPtr(new AuthResponseRequest(response, request->auth())))));
}

void Connector::on_auth_success(const AuthResponseRequest* request, const String& token) {
  if (!request->auth()->success(token)) {
    on_error(CONNECTION_ERROR_AUTH, "Failed evaluating success token: " + request->auth()->error());
    return;
  }
  on_ready_or_register_for_events();
}

void Connector::on_close(Connection* connection) {
  if (is_canceled() || is_timeout_error()) {
    finish();
  } else {
    on_error(CONNECTION_ERROR_CLOSE, "Connection closed prematurely");
  }
}

void Connector::on_connect(SocketConnector* socket_connector) {
  if (socket_connector->is_ok()) {
    Socket::Ptr socket(socket_connector->release_socket());

    connection_.reset(new Connection(socket, host_, protocol_version_, settings_.idle_timeout_secs,
                                     settings_.heartbeat_interval_secs));
    connection_->set_listener(this);

    if (socket_connector->ssl_session()) {
      socket->set_handler(
          new SslConnectionHandler(socket_connector->ssl_session().release(), connection_.get()));
    } else {
      socket->set_handler(new ConnectionHandler(connection_.get()));
    }

    connection_->write_and_flush(
        RequestCallback::Ptr(new StartupCallback(this, Request::ConstPtr(new OptionsRequest()))));

  } else if (socket_connector->is_canceled() || is_timeout_error()) {
    finish();
  } else if (socket_connector->error_code() == SocketConnector::SOCKET_ERROR_CONNECT) {
    on_error(CONNECTION_ERROR_CONNECT, socket_connector->error_message());
  } else if (socket_connector->error_code() == SocketConnector::SOCKET_ERROR_CLOSE) {
    on_error(CONNECTION_ERROR_CLOSE, socket_connector->error_message());
  } else if (socket_connector->error_code() == SocketConnector::SOCKET_ERROR_SSL_HANDSHAKE) {
    on_error(CONNECTION_ERROR_SSL_HANDSHAKE, socket_connector->error_message());
  } else if (socket_connector->error_code() == SocketConnector::SOCKET_ERROR_SSL_VERIFY) {
    on_error(CONNECTION_ERROR_SSL_VERIFY, socket_connector->error_message());
  } else {
    on_error(CONNECTION_ERROR_SOCKET,
             "Underlying socket error: " + socket_connector->error_message());
  }
}

void Connector::on_timeout(Timer* timer) {
  if (metrics_) {
    metrics_->connection_timeouts.inc();
  }
  error_code_ = CONNECTION_ERROR_TIMEOUT;
  error_message_ = "Connection timeout";
  socket_connector_->cancel();
  if (connection_) connection_->close();
}
