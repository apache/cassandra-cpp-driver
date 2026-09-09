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

#include "loop_test.hpp"
#
#include "connector.hpp"
#include "socket_connector.hpp"
#include "ssl.hpp"

#define DNS_HOSTNAME "cpp-driver.hostname."
#define DNS_IP_ADDRESS "127.254.254.254"

using mockssandra::internal::ClientConnection;
using mockssandra::internal::ClientConnectionFactory;
using mockssandra::internal::ServerConnection;

class CloseConnection : public ClientConnection {
public:
  CloseConnection(ServerConnection* server)
      : ClientConnection(server) {}

  virtual int on_accept() {
    int rc = accept();
    if (rc != 0) {
      return rc;
    }
    close();
    return rc;
  }
};

class CloseConnectionFactory : public ClientConnectionFactory {
public:
  virtual ClientConnection* create(ServerConnection* server) const {
    return new CloseConnection(server);
  }
};

class SniServerNameConnection : public ClientConnection {
public:
  SniServerNameConnection(ServerConnection* server)
      : ClientConnection(server) {}

  virtual void on_read(const char* data, size_t len) {
    const char* server_name = sni_server_name();
    if (server_name) {
      write(String(server_name) + " - Closed");
    } else {
      write("<unknown> - Closed");
    }
  }
};

class SniServerNameConnectionFactory : public ClientConnectionFactory {
public:
  virtual ClientConnection* create(ServerConnection* server) const {
    return new SniServerNameConnection(server);
  }
};

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

class TestSocketHandler : public SocketHandler {
public:
  TestSocketHandler(String* result)
      : result_(result) {}

  virtual void on_read(Socket* socket, ssize_t nread, const uv_buf_t* buf) {
    if (nread > 0) {
      result_->append(buf->base, nread);
    }
    free_buffer(buf);
    if (result_->find("Closed") != std::string::npos) {
      socket->close();
    }
  }

  virtual void on_write(Socket* socket, int status, SocketRequest* request) { delete request; }

  virtual void on_close() {}

private:
  String* result_;
};

class SslTestSocketHandler : public SslSocketHandler {
public:
  SslTestSocketHandler(SslSession* ssl_session, String* result)
      : SslSocketHandler(ssl_session)
      , result_(result) {}

  virtual void on_ssl_read(Socket* socket, char* buf, size_t size) {
    result_->append(buf, size);
    if (result_->find("Closed") != std::string::npos) {
      socket->close();
    }
  }

  virtual void on_write(Socket* socket, int status, SocketRequest* request) { delete request; }

  virtual void on_close() {}

private:
  String* result_;
};

class SocketUnitTest : public LoopTest {
public:
  SocketSettings use_ssl(const String& cn = "") {
    SslContext::Ptr ssl_context(SslContextFactory::create());

    String cert = server_.use_ssl(cn);
    EXPECT_FALSE(cert.empty()) << "Unable to enable SSL";
    EXPECT_EQ(ssl_context->add_trusted_cert(cert.data(), cert.size()), CASS_OK);

    SocketSettings settings;
    settings.ssl_context = ssl_context;
    settings.hostname_resolution_enabled = true;

    return settings;
  }

  void weaken_ssl() { server_.weaken_ssl(); }

  void listen(const Address& address = Address("127.0.0.1", 8888)) {
    ASSERT_EQ(server_.listen(address), 0);
  }

  void close() { server_.close(); }

  void use_close_immediately() { server_.use_connection_factory(new CloseConnectionFactory()); }
  void use_sni_server_name() {
    server_.use_connection_factory(new SniServerNameConnectionFactory());
  }

  virtual void TearDown() {
    LoopTest::TearDown();
    close();
  }

  bool verify_dns() {
    verify_dns_check(); // Verify address can be resolved
    return !HasFailure();
  }

  static void on_socket_connected(SocketConnector* connector, String* result) {
    Socket::Ptr socket = connector->release_socket();
    if (connector->error_code() == SocketConnector::SOCKET_OK) {
      if (connector->ssl_session()) {
        socket->set_handler(new SslTestSocketHandler(connector->ssl_session().release(), result));
      } else {
        socket->set_handler(new TestSocketHandler(result));
      }
      const char* data = "The socket is successfully connected and wrote data - ";
      socket->write(new BufferSocketRequest(Buffer(data, strlen(data))));
      socket->write(new BufferSocketRequest(Buffer("Closed", sizeof("Closed") - 1)));
      socket->flush();
    } else {
      ASSERT_TRUE(false) << "Failed to connect: " << connector->error_message();
    }
  }

  static void on_socket_refused(SocketConnector* connector, bool* is_refused) {
    if (connector->error_code() == SocketConnector::SOCKET_ERROR_CONNECT) {
      *is_refused = true;
    }
  }

  static void on_socket_closed(SocketConnector* connector, bool* is_closed) {
    if (connector->error_code() == SocketConnector::SOCKET_ERROR_CLOSE) {
      *is_closed = true;
    }
  }

  /* SSL handshake failures have different error codes on different versions of
   * OpenSSL - this accounts for both of them
   */
  static void on_socket_ssl_error(SocketConnector* connector, bool* is_error) {
    SocketConnector::SocketError err = connector->error_code();
    if ((err == SocketConnector::SOCKET_ERROR_CLOSE) ||
        (err == SocketConnector::SOCKET_ERROR_SSL_HANDSHAKE)) {
      *is_error = true;
    }
  }

  static void on_socket_canceled(SocketConnector* connector, bool* is_canceled) {
    if (connector->is_canceled()) {
      *is_canceled = true;
    }
  }

  static void on_request(uv_getaddrinfo_t* handle, int status, struct addrinfo* res) {
    if (status) {
      FAIL() << "Unable to Execute Test: "
             << "Add /etc/hosts entry " << DNS_IP_ADDRESS << "\t" << DNS_HOSTNAME;
    } else {
      bool match = false;
      do {
        Address address(res->ai_addr);
        if (address.is_valid_and_resolved() && address == Address(DNS_IP_ADDRESS, 8888)) {
          match = true;
          break;
        }
        res = res->ai_next;
      } while (res);
      ASSERT_TRUE(match) << "Invalid /etc/hosts entry for: '" << DNS_HOSTNAME << "' != '"
                         << DNS_IP_ADDRESS << "'";
    }
    uv_freeaddrinfo(res);
  }

private:
  void verify_dns_check() {
    uv_getaddrinfo_t request;
    ASSERT_EQ(0, uv_getaddrinfo(loop(), &request, on_request, DNS_HOSTNAME, "8888", NULL));
    uv_run(loop(), UV_RUN_DEFAULT);
  }

private:
  mockssandra::SimpleEchoServer server_;
};

TEST_F(SocketUnitTest, Simple) {
  listen();

  String result;
  SocketConnector::Ptr connector(
      new SocketConnector(Address("127.0.0.1", 8888), bind_callback(on_socket_connected, &result)));

  connector->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "The socket is successfully connected and wrote data - Closed");
}

TEST_F(SocketUnitTest, SimpleDns) {
  if (!verify_dns()) return;

  listen(Address(DNS_IP_ADDRESS, 8888));

  String result;
  SocketConnector::Ptr connector(new SocketConnector(Address(DNS_HOSTNAME, 8888),
                                                     bind_callback(on_socket_connected, &result)));

  connector->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "The socket is successfully connected and wrote data - Closed");
}

TEST_F(SocketUnitTest, Ssl) {
  SocketSettings settings(use_ssl());

  listen();

  String result;
  SocketConnector::Ptr connector(
      new SocketConnector(Address("127.0.0.1", 8888), bind_callback(on_socket_connected, &result)));

  connector->with_settings(settings)->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "The socket is successfully connected and wrote data - Closed");
}

TEST_F(SocketUnitTest, SslSniServerName) {
  SocketSettings settings(use_ssl());

  use_sni_server_name();
  listen();

  String result;
  SocketConnector::Ptr connector(
      new SocketConnector(Address("127.0.0.1", 8888, "TestSniServerName"),
                          bind_callback(on_socket_connected, &result)));

  connector->with_settings(settings)->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "TestSniServerName - Closed");
}

TEST_F(SocketUnitTest, Refused) {
  bool is_refused = false;
  SocketConnector::Ptr connector(new SocketConnector(
      Address("127.0.0.1", 8888), bind_callback(on_socket_refused, &is_refused)));
  connector->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(is_refused);
}

TEST_F(SocketUnitTest, SslClose) {
  SocketSettings settings(use_ssl());

  use_close_immediately();
  listen();

  Vector<SocketConnector::Ptr> connectors;

  bool is_closed = false;
  for (size_t i = 0; i < 10; ++i) {
    SocketConnector::Ptr connector(new SocketConnector(
        Address("127.0.0.1", 8888), bind_callback(on_socket_closed, &is_closed)));

    connector->with_settings(settings)->connect(loop());
    connectors.push_back(connector);
  }

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(is_closed);
}

TEST_F(SocketUnitTest, Cancel) {
  listen();

  Vector<SocketConnector::Ptr> connectors;

  bool is_canceled = false;
  for (size_t i = 0; i < 10; ++i) {
    SocketConnector::Ptr connector(new SocketConnector(
        Address("127.0.0.1", 8888), bind_callback(on_socket_canceled, &is_canceled)));
    connector->connect(loop());
    connectors.push_back(connector);
  }

  Vector<SocketConnector::Ptr>::iterator it = connectors.begin();
  while (it != connectors.end()) {
    (*it)->cancel();
    uv_run(loop(), UV_RUN_NOWAIT);
    it++;
  }

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(is_canceled);
}

TEST_F(SocketUnitTest, SslCancel) {
  SocketSettings settings(use_ssl());

  listen();

  Vector<SocketConnector::Ptr> connectors;

  bool is_canceled = false;
  for (size_t i = 0; i < 10; ++i) {
    SocketConnector::Ptr connector(new SocketConnector(
        Address("127.0.0.1", 8888), bind_callback(on_socket_canceled, &is_canceled)));
    connector->with_settings(settings)->connect(loop());
    connectors.push_back(connector);
  }

  Vector<SocketConnector::Ptr>::iterator it = connectors.begin();
  while (it != connectors.end()) {
    (*it)->cancel();
    uv_run(loop(), UV_RUN_NOWAIT);
    it++;
  }

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(is_canceled);
}

TEST_F(SocketUnitTest, SslVerifyIdentity) {
  SocketSettings settings(use_ssl("127.0.0.1"));

  listen();

  settings.ssl_context->set_verify_flags(CASS_SSL_VERIFY_PEER_IDENTITY);

  String result;
  SocketConnector::Ptr connector(
      new SocketConnector(Address("127.0.0.1", 8888), bind_callback(on_socket_connected, &result)));

  connector->with_settings(settings)->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "The socket is successfully connected and wrote data - Closed");
}

TEST_F(SocketUnitTest, SslVerifyIdentityDns) {
  if (!verify_dns()) return;

  SocketSettings settings(use_ssl(DNS_HOSTNAME));

  listen(Address(DNS_IP_ADDRESS, 8888));

  settings.ssl_context->set_verify_flags(CASS_SSL_VERIFY_PEER_IDENTITY_DNS);
  settings.resolve_timeout_ms = 12000;

  String result;
  SocketConnector::Ptr connector(new SocketConnector(Address(DNS_HOSTNAME, 8888),
                                                     bind_callback(on_socket_connected, &result)));

  connector->with_settings(settings)->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_EQ(result, "The socket is successfully connected and wrote data - Closed");
}

TEST_F(SocketUnitTest, SslEnforceTlsVersion) {
  SocketSettings settings(use_ssl("127.0.0.1"));
  weaken_ssl();

  listen();

  settings.ssl_context->set_min_protocol_version(CASS_SSL_VERSION_TLS1_3);

  bool is_error;
  SocketConnector::Ptr connector(new SocketConnector(
      Address("127.0.0.1", 8888), bind_callback(on_socket_ssl_error, &is_error)));

  connector->with_settings(settings)->connect(loop());

  uv_run(loop(), UV_RUN_DEFAULT);

  EXPECT_TRUE(is_error);
}
