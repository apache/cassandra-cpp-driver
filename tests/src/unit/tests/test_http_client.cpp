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

#include "http_test.hpp"

#include "driver_info.hpp"
#include "http_client.hpp"

using namespace datastax::internal;
using datastax::internal::core::HttpClient;
using datastax::internal::core::SocketSettings;
using datastax::internal::core::SslContext;
using datastax::internal::core::SslContextFactory;
using mockssandra::Ssl;

class HttpClientUnitTest : public HttpTest {
public:
  static void on_success_response(HttpClient* client, bool* flag) {
    *flag = true;
    EXPECT_TRUE(client->is_ok()) << "Failed to connect: " << client->error_message();
    EXPECT_EQ("text/plain", client->content_type());
    EXPECT_EQ(echo_response(), client->response_body());
  }

  static void on_failed_response(HttpClient* client, bool* flag) {
    *flag = true;
    EXPECT_FALSE(client->is_ok());
  }

  static void on_canceled(HttpClient* client, bool* flag) {
    if (client->is_canceled()) {
      *flag = true;
    }
  }

private:
  static String echo_response() {
    OStringStream ss;

    ss << "GET / HTTP/1.0\r\n"
       << "Host: " HTTP_MOCK_SERVER_IP "\r\n"
       << "User-Agent: cpp-driver/" << driver_version() << "\r\nAccept: */*\r\n\r\n";

    return ss.str();
  }
};

TEST_F(HttpClientUnitTest, Simple) {
  start_http_server();

  bool is_success = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_success_response, &is_success)));
  client->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_success);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, Cancel) {
  start_http_server();

  Vector<HttpClient::Ptr> clients;

  bool is_canceled = false;
  for (size_t i = 0; i < 10; ++i) {
    HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                          bind_callback(on_canceled, &is_canceled)));
    client->request(loop());
    clients.push_back(client);
  }

  Vector<HttpClient::Ptr>::iterator it = clients.begin();
  while (it != clients.end()) {
    (*it)->cancel();
    uv_run(loop(), UV_RUN_NOWAIT);
    it++;
  }

  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_canceled);
}

TEST_F(HttpClientUnitTest, CancelTimeout) {
  set_close_connnection_after_request(false);
  start_http_server();

  Vector<HttpClient::Ptr> clients;

  bool is_canceled = false;
  for (size_t i = 0; i < 10; ++i) {
    HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT),
                                          "/invalid", bind_callback(on_canceled, &is_canceled)));
    client
        ->with_request_timeout_ms(200) // Timeout quickly
        ->request(loop());
    clients.push_back(client);
  }

  Vector<HttpClient::Ptr>::iterator it = clients.begin();
  while (it != clients.end()) {
    (*it)->cancel();
    uv_run(loop(), UV_RUN_NOWAIT);
    it++;
  }

  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_canceled);

  for (Vector<HttpClient::Ptr>::const_iterator it = clients.begin(), end = clients.end(); it != end;
       ++it) {
    const HttpClient::Ptr& client(*it);
    if (!client->is_canceled()) {
      EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_TIMEOUT);
      EXPECT_EQ(client->status_code(), 404u);
    }
  }
}

TEST_F(HttpClientUnitTest, InvalidHttpServer) {
  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));
  client->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_SOCKET);
}

TEST_F(HttpClientUnitTest, InvalidHttpServerResponse) {
  enable_valid_response(false);
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));
  client->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_PARSING);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, InvalidPath) {
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT),
                                        "/invalid", bind_callback(on_failed_response, &is_failed)));
  client->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_HTTP_STATUS);
  EXPECT_EQ(client->status_code(), 404u);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, Timeout) {
  set_close_connnection_after_request(false);
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT),
                                        "/invalid", bind_callback(on_failed_response, &is_failed)));
  client
      ->with_request_timeout_ms(200) // Timeout quickly
      ->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_TIMEOUT);
  EXPECT_EQ(client->status_code(), 404u);

  stop_http_server();
}

#ifdef HAVE_OPENSSL
TEST_F(HttpClientUnitTest, Ssl) {
  SocketSettings settings = use_ssl();
  start_http_server();

  bool is_success = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_success_response, &is_success)));
  client->with_settings(settings)->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_success);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, NoClientCertProvidedSsl) {
  String ca_key = mockssandra::Ssl::generate_key();
  String ca_cert = mockssandra::Ssl::generate_cert(ca_key, "CA");

  /* CASSCPP-16

     Disable TLS 1.3 here so that we're only testing against TLS 1.2.  Since client cert compares
     don't happen until the first bit of client data is read this test would need to be restructured
     to work with TLS 1.3.  We can pursue this in later work but for now we just constrain this
     test at the earlier TLS version. */
  use_ssl(ca_key, ca_cert, HTTP_MOCK_HOSTNAME, TLS1_2_VERSION);
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));

  SslContext::Ptr ssl_context(SslContextFactory::create());

  // No client certificate provided

  ssl_context->add_trusted_cert(ca_cert.c_str(), ca_cert.size());

  SocketSettings settings;
  settings.ssl_context = ssl_context;
  client->with_settings(settings)->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_SOCKET);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, InvalidClientCertSsl) {
  String ca_key = mockssandra::Ssl::generate_key();
  String ca_cert = mockssandra::Ssl::generate_cert(ca_key, "CA");

  String client_key = mockssandra::Ssl::generate_key();
  String client_cert = mockssandra::Ssl::generate_cert(client_key, ""); // Self-signed

  /* CASSCPP-16

     Disable TLS 1.3 here so that we're only testing against TLS 1.2.  Since client cert compares
     don't happen until the first bit of client data is read this test would need to be restructured
     to work with TLS 1.3.  We can pursue this in later work but for now we just constrain this
     test at the earlier TLS version. */
  use_ssl(ca_key, ca_cert, HTTP_MOCK_HOSTNAME, TLS1_2_VERSION);
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));

  SslContext::Ptr ssl_context(SslContextFactory::create());

  ssl_context->set_cert(client_cert.c_str(), client_cert.size());
  ssl_context->set_private_key(client_key.c_str(), client_key.size(), "",
                               0); // No password expected for the private key
  ssl_context->add_trusted_cert(ca_cert.c_str(), ca_cert.size());

  SocketSettings settings;
  settings.ssl_context = ssl_context;

  client->with_settings(settings)->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_SOCKET);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, InvalidClientSslNotConfigured) {
  use_ssl();
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));
  client->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_CLOSED);

  stop_http_server();
}

TEST_F(HttpClientUnitTest, InvalidServerSslNotConfigured) {
  SocketSettings settings = use_ssl("127.0.0.1", false);
  start_http_server();

  bool is_failed = false;
  HttpClient::Ptr client(new HttpClient(Address(HTTP_MOCK_SERVER_IP, HTTP_MOCK_SERVER_PORT), "/",
                                        bind_callback(on_failed_response, &is_failed)));
  client->with_settings(settings)->request(loop());
  uv_run(loop(), UV_RUN_DEFAULT);
  EXPECT_TRUE(is_failed);
  EXPECT_EQ(client->error_code(), HttpClient::HTTP_CLIENT_ERROR_SOCKET);

  stop_http_server();
}
#endif
