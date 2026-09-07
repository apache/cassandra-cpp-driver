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

#ifndef HTTP_SERVER_TEST_HPP
#define HTTP_SERVER_TEST_HPP

#include "http_server.hpp"
#include "loop_test.hpp"
#include "socket_connector.hpp"

class HttpTest : public LoopTest {
public:
  ~HttpTest() { server_.close(); }

  const datastax::String& ca_cert() const { return ca_cert_; }
  const datastax::String& cert() const { return cert_; }
  const datastax::String& key() const { return key_; }

  void set_path(const datastax::String& path) { server_.set_path(path); }

  void set_content_type(const datastax::String& content_type) {
    server_.set_content_type(content_type);
  }

  void set_response_body(const datastax::String& response_body) {
    server_.set_response_body(response_body);
  }

  void set_response_status_code(int status_code) { server_.set_response_status_code(status_code); }

  void enable_valid_response(bool enable) { server_.enable_valid_response(enable); }

  void set_close_connnection_after_request(bool enable) {
    server_.set_close_connnection_after_request(enable);
  }

  void start_http_server() { server_.listen(); }
  void stop_http_server() { server_.close(); }

  datastax::internal::core::SocketSettings use_ssl(const String& cn = HTTP_MOCK_HOSTNAME,
                                                   bool is_server_using_ssl = true);

  void use_ssl(const String& ca_cert, const String& ca_key, const String& cn, int max_tls_version = 0);

private:
  datastax::String ca_cert_;
  datastax::String cert_;
  datastax::String key_;

  mockssandra::http::Server server_;
};

#endif
