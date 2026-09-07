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

using namespace datastax;
using namespace datastax::internal::core;

SocketSettings HttpTest::use_ssl(const String& cn, bool is_server_using_ssl /*= true*/) {
  SocketSettings settings;

#ifdef HAVE_OPENSSL
  String ca_key = mockssandra::Ssl::generate_key();
  ca_cert_ = mockssandra::Ssl::generate_cert(ca_key, "CA");

  key_ = mockssandra::Ssl::generate_key();
  cert_ = mockssandra::Ssl::generate_cert(key_, cn, ca_cert_, ca_key);

  String client_key = mockssandra::Ssl::generate_key();
  String client_cert = mockssandra::Ssl::generate_cert(client_key, cn, ca_cert_, ca_key);

  SslContext::Ptr ssl_context(SslContextFactory::create());

  ssl_context->set_cert(client_cert.c_str(), client_cert.size());
  ssl_context->set_private_key(client_key.c_str(), client_key.size(), "",
                               0); // No password expected for the private key

  ssl_context->add_trusted_cert(ca_cert_.c_str(), ca_cert_.size());

  settings.ssl_context = ssl_context;

  if (is_server_using_ssl) {
    server_.use_ssl(key_, cert_, ca_cert_, true);
  }
#endif

  return settings;
}

void HttpTest::use_ssl(const String& ca_key, const String& ca_cert, const String& cn, int max_tls_version /* = 0 */) {
#ifdef HAVE_OPENSSL
  key_ = mockssandra::Ssl::generate_key();
  cert_ = mockssandra::Ssl::generate_cert(key_, cn, ca_cert, ca_key);
  ca_cert_ = ca_cert;

  server_.use_ssl(key_, cert_, ca_cert_, true, max_tls_version);
#endif
}
