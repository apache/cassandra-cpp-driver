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

#include "ssl.hpp"

#include "logger.hpp"
#include "utils.hpp"

#include "third_party/curl/hostcheck.hpp"

#include <openssl/crypto.h>
#include <openssl/engine.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/tls1.h>
#include <openssl/x509v3.h>
#include <string.h>

#define DEBUG_SSL 0

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
#define ASN1_STRING_get0_data ASN1_STRING_data
#else
#define SSL_F_SSL_CTX_USE_CERTIFICATE_CHAIN_FILE SSL_F_USE_CERTIFICATE_CHAIN_FILE
#endif

#if defined(OPENSSL_VERSION_NUMBER) && \
    !defined(LIBRESSL_VERSION_NUMBER) // Required as OPENSSL_VERSION_NUMBER for LibreSSL is defined
                                      // as 2.0.0
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
#define SSL_CAN_SET_MIN_VERSION
#define SSL_CLIENT_METHOD TLS_client_method
#else
#define SSL_CLIENT_METHOD SSLv23_client_method
#endif
#else
#if (LIBRESSL_VERSION_NUMBER >= 0x20302000L)
#define SSL_CAN_SET_MIN_VERSION
#define SSL_CLIENT_METHOD TLS_client_method
#else
#define SSL_CLIENT_METHOD SSLv23_client_method
#endif
#endif

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

#if DEBUG_SSL
#define SSL_PRINT_INFO(ssl, w, flag, msg)                                                        \
  do {                                                                                           \
    if (w & flag) {                                                                              \
      fprintf(stderr, "%s - %s - %s\n", msg, SSL_state_string(ssl), SSL_state_string_long(ssl)); \
    }                                                                                            \
  } while (0);

static void ssl_info_callback(const SSL* ssl, int where, int ret) {
  if (ret == 0) {
    fprintf(stderr, "ssl_info_callback, error occurred.\n");
    return;
  }
  SSL_PRINT_INFO(ssl, where, SSL_CB_LOOP, "LOOP");
  SSL_PRINT_INFO(ssl, where, SSL_CB_EXIT, "EXIT");
  SSL_PRINT_INFO(ssl, where, SSL_CB_READ, "READ");
  SSL_PRINT_INFO(ssl, where, SSL_CB_WRITE, "WRITE");
  SSL_PRINT_INFO(ssl, where, SSL_CB_ALERT, "ALERT");
  SSL_PRINT_INFO(ssl, where, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
}

#undef SSL_PRINT_INFO
#endif

static int ssl_no_verify_callback(int ok, X509_STORE_CTX* store) {
  // Verification happens after in SslSession::verify()
  // via SSL_get_verify_result().
  return 1;
}

static void ssl_log_errors(const char* context) {
  const char* data;
  int flags;
  int err;
  while ((err =
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
              ERR_get_error_all(NULL, NULL, NULL, &data, &flags)
#else
              ERR_get_error_line_data(NULL, NULL, &data, &flags)
#endif
              ) != 0) {
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    LOG_ERROR("%s: %s:%s", context, buf, (flags & ERR_TXT_STRING) ? data : "");
  }
  ERR_print_errors_fp(stderr);
}

static String ssl_error_string() {
  const char* data;
  int flags;
  int err;
  String error;
  while ((err =
#if (OPENSSL_VERSION_NUMBER >= 0x30000000L)
              ERR_get_error_all(NULL, NULL, NULL, &data, &flags)
#else
              ERR_get_error_line_data(NULL, NULL, &data, &flags)
#endif
              ) != 0) {
    char buf[256];
    ERR_error_string_n(err, buf, sizeof(buf));
    if (!error.empty()) error.push_back(',');
    error.append(buf);
    if (flags & ERR_TXT_STRING) {
      error.push_back(':');
      error.append(data);
    }
  }
  return error;
}

static int pem_password_callback(char* buf, int size, int rwflag, void* u) {
  if (u == NULL) return 0;

  int len = strlen(static_cast<const char*>(u));
  if (len == 0) return 0;

  int to_copy = size;
  if (len < to_copy) {
    to_copy = len;
  }
  memcpy(buf, u, to_copy);

  return len;
}

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
static uv_rwlock_t* crypto_locks = NULL;

static void crypto_locking_callback(int mode, int n, const char* file, int line) {
  if (mode & CRYPTO_LOCK) {
    if (mode & CRYPTO_READ) {
      uv_rwlock_rdlock(crypto_locks + n);
    } else {
      uv_rwlock_wrlock(crypto_locks + n);
    }
  } else {
    if (mode & CRYPTO_READ) {
      uv_rwlock_rdunlock(crypto_locks + n);
    } else {
      uv_rwlock_wrunlock(crypto_locks + n);
    }
  }
}

static unsigned long crypto_id_callback() {
#if defined(WIN32) || defined(_WIN32)
  return static_cast<unsigned long>(GetCurrentThreadId());
#else
  return copy_cast<uv_thread_t, unsigned long>(uv_thread_self());
#endif
}
#endif

// Implementation taken from OpenSSL's SSL_CTX_use_certificate_chain_file()
// (https://github.com/openssl/openssl/blob/OpenSSL_0_9_8-stable/ssl/ssl_rsa.c#L705).
// Modified to be used for in-memory certificate chains and formatting.
static int SSL_CTX_use_certificate_chain_bio(SSL_CTX* ctx, BIO* in) {
  int ret = 0;
  X509* x = NULL;

  x = PEM_read_bio_X509_AUX(in, NULL, pem_password_callback, NULL);
  if (x == NULL) {
    SSLerr(SSL_F_SSL_CTX_USE_CERTIFICATE_CHAIN_FILE, ERR_R_PEM_LIB);
    goto end;
  }

  ret = SSL_CTX_use_certificate(ctx, x);

  if (ERR_peek_error() != 0) {
    // Key/certificate mismatch doesn't imply ret==0 ...
    ret = 0;
  }

  if (ret) {
    // If we could set up our certificate, now proceed to
    // the CA certificates.

    X509* ca;
    int r;
    unsigned long err;

#if OPENSSL_VERSION_NUMBER < 0x10100000L || \
    (defined(LIBRESSL_VERSION_NUMBER) && LIBRESSL_VERSION_NUMBER < 0x2090100fL)
    if (ctx->extra_certs != NULL) {
      sk_X509_pop_free(ctx->extra_certs, X509_free);
      ctx->extra_certs = NULL;
    }
#else
    SSL_CTX_clear_chain_certs(ctx);
#endif

    while ((ca = PEM_read_bio_X509(in, NULL, pem_password_callback, NULL)) != NULL) {
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
      r = SSL_CTX_add_extra_chain_cert(ctx, ca);
#else
      r = SSL_CTX_add0_chain_cert(ctx, ca);
#endif
      if (!r) {
        X509_free(ca);
        ret = 0;
        goto end;
      }
      // Note that we must not free r if it was successfully
      // added to the chain (while we must free the main
      // certificate, since its reference count is increased
      // by SSL_CTX_use_certificate).
    }
    // When the while loop ends, it's usually just EOF.
    err = ERR_peek_last_error();
    if (ERR_GET_LIB(err) == ERR_LIB_PEM && ERR_GET_REASON(err) == PEM_R_NO_START_LINE) {
      ERR_clear_error();
    } else {
      // Some real error
      ret = 0;
    }
  }

end:
  if (x != NULL) X509_free(x);
  return ret;
}

static EVP_PKEY* load_key(const char* key, size_t key_size, const char* password) {
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(key), key_size);
  if (bio == NULL) {
    return NULL;
  }

  EVP_PKEY* pkey =
      PEM_read_bio_PrivateKey(bio, NULL, pem_password_callback, const_cast<char*>(password));
  if (pkey == NULL) {
    ssl_log_errors("Unable to load private key");
  }

  BIO_free_all(bio);

  return pkey;
}

class OpenSslVerifyIdentity {
public:
  enum Result { INVALID_CERT, MATCH, NO_MATCH, NO_SAN_PRESENT };

  static Result match(X509* cert, const Address& address) {
    Result result = match_subject_alt_names_ipadd(cert, address);
    if (result == NO_SAN_PRESENT) {
      result = match_common_name_ipaddr(cert, address.hostname_or_address());
    }
    return result;
  }

  static Result match_dns(X509* cert, const String& hostname) {
    Result result = match_subject_alt_names_dns(cert, hostname);
    if (result == NO_SAN_PRESENT) {
      result = match_common_name_dns(cert, hostname);
    }
    return result;
  }

private:
  static Result match_common_name_ipaddr(X509* cert, const String& address) {
    const X509_NAME* name = X509_get_subject_name(cert);
    if (name == NULL) {
      return INVALID_CERT;
    }

    int i = -1;
    while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) > 0) {
      const X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
      if (name_entry == NULL) {
        return INVALID_CERT;
      }

      const ASN1_STRING* str = X509_NAME_ENTRY_get_data(name_entry);
      if (str == NULL) {
        return INVALID_CERT;
      }

      const char* common_name = reinterpret_cast<const char*>(ASN1_STRING_get0_data(str));
      if (strlen(common_name) != static_cast<size_t>(ASN1_STRING_length(str))) {
        return INVALID_CERT;
      }

      if (address == common_name) {
        return MATCH;
      }
    }

    return NO_MATCH;
  }

  static Result match_common_name_dns(X509* cert, const String& hostname) {
    const X509_NAME* name = X509_get_subject_name(cert);
    if (name == NULL) {
      return INVALID_CERT;
    }

    int i = -1;
    while ((i = X509_NAME_get_index_by_NID(name, NID_commonName, i)) >= 0) {
      const X509_NAME_ENTRY* name_entry = X509_NAME_get_entry(name, i);
      if (name_entry == NULL) {
        return INVALID_CERT;
      }

      const ASN1_STRING* str = X509_NAME_ENTRY_get_data(name_entry);
      if (str == NULL) {
        return INVALID_CERT;
      }

      const char* common_name = reinterpret_cast<const char*>(ASN1_STRING_get0_data(str));
      if (strlen(common_name) != static_cast<size_t>(ASN1_STRING_length(str))) {
        return INVALID_CERT;
      }

      // Using Curl's hostcheck because this could be error prone to rewrite
      if (Curl_cert_hostcheck(common_name, hostname.c_str())) {
        return MATCH;
      }
    }

    return NO_MATCH;
  }

  static Result match_subject_alt_names_ipadd(X509* cert, const Address& addr) {
    uint8_t addr_buf[16];
    size_t addr_buf_size;

    addr_buf_size = addr.to_inet(addr_buf);
    if (addr_buf_size == 0) {
      return NO_MATCH;
    }

    STACK_OF(GENERAL_NAME)* names = static_cast<STACK_OF(GENERAL_NAME)*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL));
    if (names == NULL) {
      return NO_SAN_PRESENT;
    }

    Result result = NO_MATCH;
    for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
      GENERAL_NAME* name = sk_GENERAL_NAME_value(names, i);

      if (name->type == GEN_IPADD) {
        ASN1_STRING* str = name->d.iPAddress;
        if (str == NULL) {
          result = INVALID_CERT;
          break;
        }

        const unsigned char* ip = ASN1_STRING_get0_data(str);
        int ip_len = ASN1_STRING_length(str);
        if (ip_len != 4 && ip_len != 16) {
          result = INVALID_CERT;
          break;
        }

        if (static_cast<size_t>(ip_len) == addr_buf_size &&
            memcmp(ip, addr_buf, addr_buf_size) == 0) {
          result = MATCH;
          break;
        }
      }
    }
    sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

    return result;
  }

  static Result match_subject_alt_names_dns(X509* cert, const String& hostname) {
    STACK_OF(GENERAL_NAME)* names = static_cast<STACK_OF(GENERAL_NAME)*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL));
    if (names == NULL) {
      return NO_SAN_PRESENT;
    }

    Result result = NO_MATCH;
    for (int i = 0; i < sk_GENERAL_NAME_num(names); ++i) {
      GENERAL_NAME* name = sk_GENERAL_NAME_value(names, i);

      if (name->type == GEN_DNS) {
        ASN1_STRING* str = name->d.dNSName;
        if (str == NULL) {
          result = INVALID_CERT;
          break;
        }

        const char* common_name = reinterpret_cast<const char*>(ASN1_STRING_get0_data(str));
        if (strlen(common_name) != static_cast<size_t>(ASN1_STRING_length(str))) {
          result = INVALID_CERT;
          break;
        }

        // Using Curl's hostcheck because this could be error prone to rewrite
        if (Curl_cert_hostcheck(common_name, hostname.c_str())) {
          result = MATCH;
          break;
        }
      }
    }
    sk_GENERAL_NAME_pop_free(names, GENERAL_NAME_free);

    return result;
  }
};

OpenSslSession::OpenSslSession(const Address& address, const String& hostname,
                               const String& sni_server_name, int flags, SSL_CTX* ssl_ctx)
    : SslSession(address, hostname, sni_server_name, flags)
    , ssl_(SSL_new(ssl_ctx))
    , incoming_state_(&incoming_)
    , outgoing_state_(&outgoing_)
    , incoming_bio_(rb::RingBufferBio::create(&incoming_state_))
    , outgoing_bio_(rb::RingBufferBio::create(&outgoing_state_)) {
  SSL_set_bio(ssl_, incoming_bio_, outgoing_bio_);
  SSL_set_connect_state(ssl_);

  if (!sni_server_name_.empty()) {
    SSL_set_tlsext_host_name(ssl_, const_cast<char*>(sni_server_name_.c_str()));
  }
}

OpenSslSession::~OpenSslSession() { SSL_free(ssl_); }

void OpenSslSession::do_handshake() {
  int rc = SSL_connect(ssl_);
  if (rc <= 0) check_error(rc);
}

void OpenSslSession::verify() {
  if (!verify_flags_) return;

  X509* peer_cert = SSL_get_peer_certificate(ssl_);
  if (peer_cert == NULL) {
    error_code_ = CASS_ERROR_SSL_NO_PEER_CERT;
    error_message_ = "No peer certificate found";
    return;
  }

  if (verify_flags_ & CASS_SSL_VERIFY_PEER_CERT) {
    int rc = SSL_get_verify_result(ssl_);
    if (rc != X509_V_OK) {
      error_code_ = CASS_ERROR_SSL_INVALID_PEER_CERT;
      error_message_ = X509_verify_cert_error_string(rc);
      X509_free(peer_cert);
      return;
    }
  }

  if (verify_flags_ & CASS_SSL_VERIFY_PEER_IDENTITY) { // Match using IP addresses
    switch (OpenSslVerifyIdentity::match(peer_cert, address_)) {
      case OpenSslVerifyIdentity::MATCH:
        // Success
        break;

      case OpenSslVerifyIdentity::INVALID_CERT:
        error_code_ = CASS_ERROR_SSL_INVALID_PEER_CERT;
        error_message_ = "Peer certificate has malformed name field(s)";
        X509_free(peer_cert);
        return;

      default:
        error_code_ = CASS_ERROR_SSL_IDENTITY_MISMATCH;
        error_message_ = "Peer certificate subject name does not match";
        X509_free(peer_cert);
        return;
    }
  } else if (verify_flags_ &
             CASS_SSL_VERIFY_PEER_IDENTITY_DNS) { // Match using hostnames (including wildcards)
    switch (OpenSslVerifyIdentity::match_dns(peer_cert, hostname_)) {
      case OpenSslVerifyIdentity::MATCH:
        // Success
        break;

      case OpenSslVerifyIdentity::INVALID_CERT:
        error_code_ = CASS_ERROR_SSL_INVALID_PEER_CERT;
        error_message_ = "Peer certificate has malformed name field(s)";
        X509_free(peer_cert);
        return;

      default:
        error_code_ = CASS_ERROR_SSL_IDENTITY_MISMATCH;
        error_message_ = "Peer certificate subject name does not match";
        X509_free(peer_cert);
        return;
    }
  }

  X509_free(peer_cert);
}

int OpenSslSession::encrypt(const char* buf, size_t size) {
  int rc = SSL_write(ssl_, buf, size);
  if (rc <= 0) check_error(rc);
  return rc;
}

int OpenSslSession::decrypt(char* buf, size_t size) {
  int rc = SSL_read(ssl_, buf, size);
  if (rc <= 0) check_error(rc);
  return rc;
}

void OpenSslSession::check_error(int rc) {
  int err = SSL_get_error(ssl_, rc);
  if (err == SSL_ERROR_ZERO_RETURN) {
    error_code_ = CASS_ERROR_SSL_CLOSED;
  } else if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_NONE) {
    error_code_ = CASS_ERROR_SSL_PROTOCOL_ERROR;
    error_message_ = ssl_error_string();
  }
}

OpenSslContext::OpenSslContext()
    : ssl_ctx_(SSL_CTX_new(SSL_CLIENT_METHOD()))
    , trusted_store_(X509_STORE_new()) {
  SSL_CTX_set_cert_store(ssl_ctx_, trusted_store_);
  SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, ssl_no_verify_callback);
#if (OPENSSL_VERSION_NUMBER >= 0x10100000L)
  // Allow up to TLS 1.3.
  SSL_CTX_set_max_proto_version(ssl_ctx_, TLS1_3_VERSION);
#endif
#if DEBUG_SSL
  SSL_CTX_set_info_callback(ssl_ctx_, ssl_info_callback);
#endif
}

OpenSslContext::~OpenSslContext() { SSL_CTX_free(ssl_ctx_); }

SslSession* OpenSslContext::create_session(const Address& address, const String& hostname,
                                           const String& sni_server_name) {
  return new OpenSslSession(address, hostname, sni_server_name, verify_flags_, ssl_ctx_);
}

CassError OpenSslContext::add_trusted_cert(const char* cert, size_t cert_length) {
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(cert), cert_length);
  if (bio == NULL) {
    return CASS_ERROR_SSL_INVALID_CERT;
  }

  int num_certs = 0;

  // Iterate over the bio, reading out as many certificates as possible.
  for (X509* cert = PEM_read_bio_X509(bio, NULL, pem_password_callback, NULL); cert != NULL;
       cert = PEM_read_bio_X509(bio, NULL, pem_password_callback, NULL)) {
    X509_STORE_add_cert(trusted_store_, cert);
    X509_free(cert);
    num_certs++;
  }

  // Retrieve and discard the error tht terminated the loop,
  // so it doesn't cause the next PEM operation to fail mysteriously.
  ERR_get_error();

  BIO_free_all(bio);

  // If no certificates were read from the bio, that is an error.
  if (num_certs == 0) {
    ssl_log_errors("Unable to load certificate(s)");
    return CASS_ERROR_SSL_INVALID_CERT;
  }

  return CASS_OK;
}

CassError OpenSslContext::set_cert(const char* cert, size_t cert_length) {
  BIO* bio = BIO_new_mem_buf(const_cast<char*>(cert), cert_length);
  if (bio == NULL) {
    return CASS_ERROR_SSL_INVALID_CERT;
  }

  int rc = SSL_CTX_use_certificate_chain_bio(ssl_ctx_, bio);

  BIO_free_all(bio);

  if (!rc) {
    ssl_log_errors("Unable to load certificate chain");
    return CASS_ERROR_SSL_INVALID_CERT;
  }

  return CASS_OK;
}

CassError OpenSslContext::set_private_key(const char* key, size_t key_length, const char* password,
                                          size_t password_length) {
  // TODO: Password buffer
  EVP_PKEY* pkey = load_key(key, key_length, password);
  if (pkey == NULL) {
    return CASS_ERROR_SSL_INVALID_PRIVATE_KEY;
  }

  SSL_CTX_use_PrivateKey(ssl_ctx_, pkey);
  EVP_PKEY_free(pkey);

  return CASS_OK;
}

CassError OpenSslContext::set_min_protocol_version(CassSslTlsVersion min_version) {
#ifdef SSL_CAN_SET_MIN_VERSION
  int method;
  switch (min_version) {
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1:
      method = TLS1_VERSION;
      break;
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1_1:
      method = TLS1_1_VERSION;
      break;
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1_2:
      method = TLS1_2_VERSION;
      break;
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1_3:
      method = TLS1_3_VERSION;
      break;
    default:
      // unsupported version
      return CASS_ERROR_LIB_BAD_PARAMS;
  }
  SSL_CTX_set_min_proto_version(ssl_ctx_, method);
  return CASS_OK;
#else
  // If we don't have the `set_min_proto_version` function then we do this via
  // the (deprecated in later versions) options function.
  int options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
  switch (min_version) {
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1:
      break;
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1_1:
      options |= SSL_OP_NO_TLSv1;
      break;
    case CassSslTlsVersion::CASS_SSL_VERSION_TLS1_2:
      options |= SSL_OP_NO_TLSv1;
      options |= SSL_OP_NO_TLSv1_1;
      break;
    default:
      // unsupported version
      return CASS_ERROR_LIB_BAD_PARAMS;
  }
  SSL_CTX_set_options(ssl_ctx_, options);
  return CASS_OK;
#endif
}

SslContext::Ptr OpenSslContextFactory::create() { return SslContext::Ptr(new OpenSslContext()); }

namespace openssl {

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
void* malloc(size_t size) { return Memory::malloc(size); }

void* realloc(void* ptr, size_t size) { return Memory::realloc(ptr, size); }

void free(void* ptr) { Memory::free(ptr); }
#else
void* malloc(size_t size, const char* file, int line) { return Memory::malloc(size); }

void* realloc(void* ptr, size_t size, const char* file, int line) {
  return Memory::realloc(ptr, size);
}

void free(void* ptr, const char* file, int line) { Memory::free(ptr); }
#endif

} // namespace openssl

void OpenSslContextFactory::internal_init() {
  CRYPTO_set_mem_functions(openssl::malloc, openssl::realloc, openssl::free);

#if OPENSSL_VERSION_NUMBER < 0x10100000L
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
  // We have to set the lock/id callbacks for use of OpenSSL thread safety.
  // It's not clear what's thread-safe in OpenSSL. Writing/Reading to
  // a single "SSL" object is NOT and we don't do that, but we do create multiple
  // "SSL" objects from a single "SSL_CTX" in different threads. That seems to be
  // okay with the following callbacks set.
  int num_locks = CRYPTO_num_locks();
  crypto_locks = reinterpret_cast<uv_rwlock_t*>(Memory::malloc(sizeof(uv_rwlock_t) * num_locks));
  for (int i = 0; i < num_locks; ++i) {
    if (uv_rwlock_init(crypto_locks + i)) {
      fprintf(stderr, "Unable to init read/write lock");
      abort();
    }
  }

  CRYPTO_set_locking_callback(crypto_locking_callback);
  CRYPTO_set_id_callback(crypto_id_callback);

#else
  rb::RingBufferBio::initialize();
#endif
}

void OpenSslContextFactory::internal_thread_cleanup() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
  ERR_remove_thread_state(NULL);
#endif
}

void OpenSslContextFactory::internal_cleanup() {
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  RAND_cleanup();
  ENGINE_cleanup();
#endif
  CONF_modules_unload(1);
#if OPENSSL_VERSION_NUMBER < 0x10100000L
  CONF_modules_free();
  EVP_cleanup();
  ERR_free_strings();
  CRYPTO_cleanup_all_ex_data();
  CRYPTO_set_locking_callback(NULL);
  CRYPTO_set_id_callback(NULL);
#endif
#if OPENSSL_VERSION_NUMBER < 0x10100000L && OPENSSL_VERSION_NUMBER > 0x10002000L
  SSL_COMP_free_compression_methods();
#endif

  thread_cleanup();

#if OPENSSL_VERSION_NUMBER < 0x10100000L || defined(LIBRESSL_VERSION_NUMBER)
  if (crypto_locks != NULL) {
    int num_locks = CRYPTO_num_locks();
    for (int i = 0; i < num_locks; ++i) {
      uv_rwlock_destroy(crypto_locks + i);
    }
    Memory::free(crypto_locks);
    crypto_locks = NULL;
  }
#else
  rb::RingBufferBio::cleanup();
#endif
}
