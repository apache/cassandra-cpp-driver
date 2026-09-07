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

#ifndef HTTP_MOCK_SERVER_HPP
#define HTTP_MOCK_SERVER_HPP

#define HTTP_MOCK_HOSTNAME "cpp-driver.hostname."
#define HTTP_MOCK_SERVER_IP "127.254.254.254"
#define HTTP_MOCK_SERVER_PORT 30443

#include "http_parser.h"
#include "mockssandra.hpp"
#include "string.hpp"

namespace mockssandra { namespace http {

/**
 * Mockssandra HTTP server.
 *
 * If no response body is set then the default response will the be original request; e.g. echo HTTP
 * server.
 */
class Server {
public:
  Server()
      : path_("/")
      , content_type_("text/plain")
      , response_status_code_(200)
      , enable_valid_response_(true)
      , close_connnection_after_request_(true)
      , event_loop_group_(1, "HTTP Server")
      , factory_(this)
      , server_connection_(new internal::ServerConnection(
            Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), factory_)) {}

  const String& path() const { return path_; }
  const String& content_type() const { return content_type_; }
  const String& response_body() const { return response_body_; }
  int response_status_code() const { return response_status_code_; }
  bool enable_valid_response() { return enable_valid_response_; }
  bool close_connnection_after_request() { return close_connnection_after_request_; }

  void set_path(const String& path) { path_ = path; }
  void set_content_type(const String& content_type) { content_type_ = content_type; }
  void set_response_body(const String& response_body) { response_body_ = response_body; }
  void set_response_status_code(int status_code) { response_status_code_ = status_code; }
  void enable_valid_response(bool enable) { enable_valid_response_ = enable; }
  void set_close_connnection_after_request(bool enable) {
    close_connnection_after_request_ = enable;
  }

  bool use_ssl(const String& key, const String& cert, const String& ca_cert = "",
               bool require_client_cert = false, int max_tls_version = 0);

  void listen();
  void close();

private:
  class ClientConnection : public internal::ClientConnection {
  public:
    ClientConnection(internal::ServerConnection* server_connection, Server* server);

    virtual void on_read(const char* data, size_t len);

  private:
    static int on_url(http_parser* parser, const char* buf, size_t len);
    void handle_url(const char* buf, size_t len);

  private:
    String path_;
    String content_type_;
    String response_body_;
    int response_status_code_;
    bool enable_valid_response_;
    bool close_connnection_after_request_;
    String request_;
    http_parser parser_;
    http_parser_settings parser_settings_;
  };

  class ClientConnectionFactory : public internal::ClientConnectionFactory {
  public:
    ClientConnectionFactory(Server* server)
        : server_(server) {}

    virtual internal::ClientConnection*
    create(internal::ServerConnection* server_connection) const {
      return new ClientConnection(server_connection, server_);
    }

  private:
    Server* const server_;
  };

private:
  String path_;
  String content_type_;
  String response_body_;
  int response_status_code_;
  bool enable_valid_response_;
  bool close_connnection_after_request_;
  SimpleEventLoopGroup event_loop_group_;
  ClientConnectionFactory factory_;
  internal::ServerConnection::Ptr server_connection_;
};

}} // namespace mockssandra::http

#endif
