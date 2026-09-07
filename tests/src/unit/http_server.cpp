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

#include "http_server.hpp"

using datastax::String;
using datastax::internal::Memory;
using datastax::internal::OStringStream;
using datastax::internal::ScopedMutex;
using datastax::internal::core::Address;
using datastax::internal::core::EventLoop;
using datastax::internal::core::Task;

String response(int status, const String& body = "", const String& content_type = "") {
  OStringStream ss;
  ss << "HTTP/1.0 " << status << " " << http_status_str(static_cast<http_status>(status)) << "\r\n";
  if (!body.empty()) {
    ss << "Content-Type: ";
    if (content_type.empty()) {
      ss << "text/plain";
    } else {
      ss << content_type;
    }
    ss << "\r\nContent-Length: " << body.size() << "\r\n\r\n" << body;
  } else {
    ss << "\r\n";
  }

  return ss.str();
}

using namespace mockssandra;
using namespace mockssandra::http;

void Server::listen() {
  server_connection_->listen(&event_loop_group_);
  server_connection_->wait_listen();
}

void Server::close() {
  if (server_connection_) {
    server_connection_->close();
    server_connection_->wait_close();
  }
}

bool Server::use_ssl(const String& key, const String& cert, const String& ca_cert /*= ""*/,
                     bool require_client_cert /*= false*/, int max_tls_version /* = 0 */) {
  return server_connection_->use_ssl(key, cert, ca_cert, require_client_cert, max_tls_version);
}

Server::ClientConnection::ClientConnection(internal::ServerConnection* server_connection,
                                           Server* server)
    : internal::ClientConnection(server_connection)
    , path_(server->path())
    , content_type_(server->content_type())
    , response_body_(server->response_body())
    , response_status_code_(server->response_status_code())
    , enable_valid_response_(server->enable_valid_response())
    , close_connnection_after_request_(server->close_connnection_after_request()) {
  http_parser_init(&parser_, HTTP_REQUEST);
  http_parser_settings_init(&parser_settings_);

  parser_.data = this;
  parser_settings_.on_url = on_url;
}

void Server::ClientConnection::on_read(const char* data, size_t len) {
  request_ = String(data, len);
  size_t parsed = http_parser_execute(&parser_, &parser_settings_, data, len);
  if (parsed < static_cast<size_t>(len)) {
    enum http_errno err = HTTP_PARSER_ERRNO(&parser_);
    fprintf(stderr, "%s: %s\n", http_errno_name(err), http_errno_description(err));
    close();
  }
}

int Server::ClientConnection::on_url(http_parser* parser, const char* buf, size_t len) {
  ClientConnection* self = static_cast<ClientConnection*>(parser->data);
  self->handle_url(buf, len);
  return 0;
}

void Server::ClientConnection::handle_url(const char* buf, size_t len) {
  String path(buf, len);
  if (path.substr(0, path.find("?")) == path_) { // Compare without query parameters
    if (enable_valid_response_) {
      if (response_body_.empty()) {
        write(response(response_status_code_, request_)); // Echo response
      } else {
        write(response(response_status_code_, response_body_, content_type_));
      }
    } else {
      write("Invalid HTTP server response");
    }
  } else {
    write(response(404));
  }
  // From the HTTP/1.0 protocol specification:
  //
  // > When an Entity-Body is included with a message, the length of that body may be determined in
  // > one of two ways. If a Content-Length header field is present, its value in bytes represents
  // > the length of the Entity-Body. Otherwise, the body length is determined by the closing of the
  // > connection by the server.
  if (close_connnection_after_request_) {
    close();
  }
}
