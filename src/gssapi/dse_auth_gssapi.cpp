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

#include "dse_auth_gssapi.hpp"
#include "logger.hpp"
#include "scoped_ptr.hpp"
#include "string_ref.hpp"

#include <string.h>

#include <gssapi/gssapi.h>
#include <gssapi/gssapi_krb5.h>

#define DSE_AUTHENTICATOR "com.datastax.bdp.cassandra.auth.DseAuthenticator"

#define GSSAPI_AUTH_MECHANISM "GSSAPI"
#define GSSAPI_AUTH_SERVER_INITIAL_CHALLENGE "GSSAPI-START"

#define PLAINTEXT_AUTH_MECHANISM "PLAIN"
#define PLAINTEXT_AUTH_SERVER_INITIAL_CHALLENGE "PLAIN-START"

using namespace datastax;
using namespace datastax::internal::core;
using namespace datastax::internal::enterprise;

static void dse_gssapi_authenticator_nop_lock(void* data) {}
static void dse_gssapi_authenticator_nop_unlock(void* data) {}

struct GssapiBuffer {
public:
  gss_buffer_desc buffer;

public:
  GssapiBuffer() {
    buffer.value = NULL;
    buffer.length = 0;
  }

  ~GssapiBuffer() { release(); }

  const unsigned char* value() const { return static_cast<const unsigned char*>(buffer.value); }

  const char* data() const { return static_cast<const char*>(buffer.value); }

  size_t length() const { return buffer.length; }

  bool is_empty() const { return buffer.length == 0; }

  void release() {
    if (buffer.value) {
      OM_uint32 min_stat;

      DseGssapiAuthenticator::lock();
      gss_release_buffer(&min_stat, &buffer);
      DseGssapiAuthenticator::unlock();
    }
  }
};

class GssapiName {
public:
  gss_name_t name;

public:
  GssapiName()
      : name(GSS_C_NO_NAME) {}

  ~GssapiName() { release(); }

  void release() {
    if (name != GSS_C_NO_NAME) {
      OM_uint32 min_stat;

      DseGssapiAuthenticator::lock();
      gss_release_name(&min_stat, &name);
      DseGssapiAuthenticator::unlock();
    }
  }
};

namespace datastax { namespace internal { namespace enterprise {

class GssapiAuthenticatorImpl : public Allocated {
public:
  enum State { NEGOTIATION, AUTHENTICATION, AUTHENTICATED };

  enum Result { RESULT_ERROR, RESULT_CONTINUE, RESULT_COMPLETE };

  enum Type { AUTH_NONE = 1, AUTH_INTEGRITY = 2, AUTH_CONFIDENTIALITY = 3 };

  GssapiAuthenticatorImpl(const String& authorization_id);
  ~GssapiAuthenticatorImpl();

  const String& response() const { return response_; }
  const String& error() const { return error_; }

  Result init(const String& service, const String& principal);
  Result process(const String& token);

private:
  Result negotiate(gss_buffer_t challenge_token);
  Result authenticate(gss_buffer_t challenge_token);

  static String display_status(OM_uint32 maj, OM_uint32 min);

private:
  gss_ctx_id_t context_;
  gss_name_t server_name_;
  OM_uint32 gss_flags_;
  gss_cred_id_t client_creds_;
  String username_;
  String response_;
  String error_;
  State state_;
  String authorization_id_;
};

}}} // namespace datastax::internal::enterprise

GssapiAuthenticatorImpl::GssapiAuthenticatorImpl(const String& authorization_id)
    : context_(GSS_C_NO_CONTEXT)
    , server_name_(GSS_C_NO_NAME)
    , gss_flags_(GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG)
    , client_creds_(GSS_C_NO_CREDENTIAL)
    , state_(NEGOTIATION)
    , authorization_id_(authorization_id) {}

GssapiAuthenticatorImpl::~GssapiAuthenticatorImpl() {
  OM_uint32 min_stat;

  if (context_ != GSS_C_NO_CONTEXT) {
    DseGssapiAuthenticator::lock();
    gss_delete_sec_context(&min_stat, &context_, GSS_C_NO_BUFFER);
    DseGssapiAuthenticator::unlock();
  }

  if (server_name_ != GSS_C_NO_NAME) {
    DseGssapiAuthenticator::lock();
    gss_release_name(&min_stat, &server_name_);
    DseGssapiAuthenticator::unlock();
  }

  if (client_creds_ != GSS_C_NO_CREDENTIAL) {
    DseGssapiAuthenticator::lock();
    gss_release_cred(&min_stat, &client_creds_);
    DseGssapiAuthenticator::unlock();
  }
}

GssapiAuthenticatorImpl::Result GssapiAuthenticatorImpl::init(const String& service,
                                                              const String& principal) {
  OM_uint32 maj_stat;
  OM_uint32 min_stat;
  gss_buffer_desc name_token = GSS_C_EMPTY_BUFFER;

  name_token.value = const_cast<void*>(static_cast<const void*>(service.c_str()));
  name_token.length = service.size();

  DseGssapiAuthenticator::lock();
  maj_stat = gss_import_name(&min_stat, &name_token, GSS_C_NT_HOSTBASED_SERVICE, &server_name_);
  DseGssapiAuthenticator::unlock();

  if (GSS_ERROR(maj_stat)) {
    error_.assign("Failed to import server name (gss_import_name()): " +
                  display_status(maj_stat, min_stat));
    return RESULT_ERROR;
  }

  GssapiName principal_name; // Initialized to GSS_C_NO_NAME

  if (!principal.empty()) {
    gss_buffer_desc principal_token = GSS_C_EMPTY_BUFFER;

    principal_token.value = const_cast<void*>(static_cast<const void*>(principal.c_str()));
    principal_token.length = principal.size();

    DseGssapiAuthenticator::lock();
    maj_stat =
        gss_import_name(&min_stat, &principal_token, GSS_C_NT_USER_NAME, &principal_name.name);
    DseGssapiAuthenticator::unlock();

    if (GSS_ERROR(maj_stat)) {
      error_.assign("Failed to import principal name (gss_import_name()): " +
                    display_status(maj_stat, min_stat));
      return RESULT_ERROR;
    }
  }

  DseGssapiAuthenticator::lock();
  maj_stat = gss_acquire_cred(&min_stat, principal_name.name, GSS_C_INDEFINITE, GSS_C_NO_OID_SET,
                              GSS_C_INITIATE, &client_creds_, NULL, NULL);
  DseGssapiAuthenticator::unlock();

  if (GSS_ERROR(maj_stat)) {
    error_.assign("Failed to acquire principal credentials (gss_acquire_cred()): " +
                  display_status(maj_stat, min_stat));
    return RESULT_ERROR;
  }

  return RESULT_COMPLETE;
}

GssapiAuthenticatorImpl::Result GssapiAuthenticatorImpl::negotiate(gss_buffer_t challenge_token) {
  OM_uint32 maj_stat;
  OM_uint32 min_stat;
  GssapiBuffer output_token;
  Result result = RESULT_ERROR;

  DseGssapiAuthenticator::lock();
  maj_stat = gss_init_sec_context(&min_stat, client_creds_, &context_, server_name_, GSS_C_NO_OID,
                                  gss_flags_, 0, GSS_C_NO_CHANNEL_BINDINGS, challenge_token, NULL,
                                  &output_token.buffer, NULL, NULL);
  DseGssapiAuthenticator::unlock();

  if (maj_stat != GSS_S_COMPLETE && maj_stat != GSS_S_CONTINUE_NEEDED) {
    error_.assign("Failed to initalize security context (gss_init_sec_context()): " +
                  display_status(maj_stat, min_stat));
    return RESULT_ERROR;
  }

  result = (maj_stat == GSS_S_COMPLETE) ? RESULT_COMPLETE : RESULT_CONTINUE;

  if (!output_token.is_empty()) {
    response_.assign(output_token.data(), output_token.length());
  }

  if (result == RESULT_COMPLETE) {
    GssapiName user;

    DseGssapiAuthenticator::lock();
    maj_stat =
        gss_inquire_context(&min_stat, context_, &user.name, NULL, NULL, NULL, NULL, NULL, NULL);
    DseGssapiAuthenticator::unlock();

    if (GSS_ERROR(maj_stat)) {
      error_.assign(
          "Failed to inquire security context for user principal (gss_inquire_context()): " +
          display_status(maj_stat, min_stat));
      return RESULT_ERROR;
    }

    GssapiBuffer user_token;

    DseGssapiAuthenticator::lock();
    maj_stat = gss_display_name(&min_stat, user.name, &user_token.buffer, NULL);
    DseGssapiAuthenticator::unlock();

    if (GSS_ERROR(maj_stat)) {
      error_.assign("Failed to get display name for user principal (gss_inquire_context()): " +
                    display_status(maj_stat, min_stat));
      return RESULT_ERROR;
    } else {
      username_.assign(user_token.data(), user_token.length());
      state_ = AUTHENTICATION;
    }
  }

  return result;
}

GssapiAuthenticatorImpl::Result
GssapiAuthenticatorImpl::authenticate(gss_buffer_t challenge_token) {
  OM_uint32 maj_stat;
  OM_uint32 min_stat;
  OM_uint32 req_output_size;
  OM_uint32 max_input_size;
  unsigned char qop;
  String input;
  gss_buffer_desc input_token = GSS_C_EMPTY_BUFFER;
  GssapiBuffer output_token;

  DseGssapiAuthenticator::lock();
  maj_stat = gss_unwrap(&min_stat, context_, challenge_token, &output_token.buffer, NULL, NULL);
  DseGssapiAuthenticator::unlock();

  if (GSS_ERROR(maj_stat)) {
    error_.assign("Failed to get unwrap challenge token (gss_unwrap()): " +
                  display_status(maj_stat, min_stat));
    return RESULT_ERROR;
  }

  if (output_token.length() != 4) {
    return RESULT_ERROR;
  }

  qop = output_token.value()[0];
  if (qop & AUTH_CONFIDENTIALITY) {
    qop = AUTH_CONFIDENTIALITY;
  } else if (qop & AUTH_INTEGRITY) {
    qop = AUTH_INTEGRITY;
  } else {
    qop = AUTH_NONE;
  }

  req_output_size = ((output_token.value())[1] << 16) | ((output_token.value())[2] << 8) |
                    ((output_token.value())[3] << 0);

  req_output_size = req_output_size & 0xFFFFFF;

  DseGssapiAuthenticator::lock();
  maj_stat = gss_wrap_size_limit(&min_stat, context_, 1, GSS_C_QOP_DEFAULT, req_output_size,
                                 &max_input_size);
  DseGssapiAuthenticator::unlock();

  if (max_input_size < req_output_size) {
    req_output_size = max_input_size;
  }

  input.push_back(qop);
  input.push_back((req_output_size >> 16) & 0xFF);
  input.push_back((req_output_size >> 8) & 0xFF);
  input.push_back((req_output_size >> 0) & 0xFF);

  // Send the authorization_id if present (proxy login), otherwise the username.
  const String& authorization_id = authorization_id_.empty() ? username_ : authorization_id_;
  input.append(authorization_id);

  input_token.length = 4 + authorization_id.size();
  input_token.value = const_cast<void*>(static_cast<const void*>(input.c_str()));

  output_token.release();

  DseGssapiAuthenticator::lock();
  maj_stat =
      gss_wrap(&min_stat, context_, 0, GSS_C_QOP_DEFAULT, &input_token, NULL, &output_token.buffer);
  DseGssapiAuthenticator::unlock();

  if (GSS_ERROR(maj_stat)) {
    error_.assign("Failed to get wrape response token (gss_wrap()): " +
                  display_status(maj_stat, min_stat));
    return RESULT_ERROR;
  }

  if (!output_token.is_empty()) {
    response_.assign(output_token.data(), output_token.length());
  }

  state_ = AUTHENTICATED;

  return RESULT_COMPLETE;
}

String GssapiAuthenticatorImpl::display_status(OM_uint32 maj, OM_uint32 min) {
  String error;
  OM_uint32 message_context;

  message_context = 0;

  do {
    GssapiBuffer message;
    OM_uint32 maj_stat, min_stat;

    DseGssapiAuthenticator::lock();
    maj_stat = gss_display_status(&min_stat, maj, GSS_C_GSS_CODE, GSS_C_NO_OID, &message_context,
                                  &message.buffer);
    DseGssapiAuthenticator::unlock();

    if (GSS_ERROR(maj_stat)) {
      error.append("GSSAPI error: (unable to get major error)");
      break;
    }

    error.append(message.data(), message.length());
  } while (message_context != 0);

  message_context = 0;

  error.append(" (");
  do {
    GssapiBuffer message;
    OM_uint32 maj_stat, min_stat;

    DseGssapiAuthenticator::lock();
    maj_stat = gss_display_status(&min_stat, min, GSS_C_MECH_CODE, GSS_C_NO_OID, &message_context,
                                  &message.buffer);
    DseGssapiAuthenticator::unlock();

    if (GSS_ERROR(maj_stat)) {
      error.append("GSSAPI error: (unable to get minor error)");
      break;
    }

    error.append(message.data(), message.length());
  } while (message_context != 0);
  error.append(" )");

  return error;
}

GssapiAuthenticatorImpl::Result GssapiAuthenticatorImpl::process(const String& token) {
  Result result = RESULT_ERROR;
  gss_buffer_desc challenge_token = GSS_C_EMPTY_BUFFER;

  response_.clear();

  if (!token.empty()) {
    challenge_token.value = const_cast<char*>(token.c_str());
    challenge_token.length = token.length();
  }

  switch (state_) {
    case NEGOTIATION:
      result = negotiate(&challenge_token);
      break;

    case AUTHENTICATION:
      result = authenticate(&challenge_token);
      break;

    default:
      break;
  }

  return result;
}

DseGssapiAuthenticatorLockCallback DseGssapiAuthenticator::lock_callback_ =
    dse_gssapi_authenticator_nop_lock;
DseGssapiAuthenticatorUnlockCallback DseGssapiAuthenticator::unlock_callback_ =
    dse_gssapi_authenticator_nop_unlock;
void* DseGssapiAuthenticator::data_ = NULL;

CassError
DseGssapiAuthenticator::set_lock_callbacks(DseGssapiAuthenticatorLockCallback lock_callback,
                                           DseGssapiAuthenticatorUnlockCallback unlock_callback,
                                           void* data) {
  if (lock_callback == NULL || unlock_callback == NULL || data_ == NULL) {
    lock_callback_ = dse_gssapi_authenticator_nop_lock;
    unlock_callback_ = dse_gssapi_authenticator_nop_unlock;
    data_ = NULL;
    return CASS_ERROR_LIB_BAD_PARAMS;
  } else {
    lock_callback_ = lock_callback;
    unlock_callback_ = unlock_callback;
    data_ = data;
    return CASS_OK;
  }
}

DseGssapiAuthenticator::DseGssapiAuthenticator(const Address& address, const String& hostname,
                                               const String& class_name, const String& service,
                                               const String& principal,
                                               const String& authorization_id)
    : address_(address)
    , hostname_(hostname)
    , class_name_(class_name)
    , service_(service)
    , principal_(principal)
    , authorization_id_(authorization_id)
    , impl_(new GssapiAuthenticatorImpl(authorization_id)) {}

bool DseGssapiAuthenticator::initial_response(String* response) {

  String service;

  if (hostname_.empty()) {
    service.append(service_);
    service.append("@");
    service.append(address_.to_string());
  } else {
    service.append(service_);
    service.append("@");
    service.append(hostname_);
  }

  if (impl_->init(service, principal_) == GssapiAuthenticatorImpl::RESULT_ERROR) {
    set_error("Unable to initialize GSSAPI: " + impl_->error());
    return false;
  }

  if (class_name_ == DSE_AUTHENTICATOR) {
    response->assign(GSSAPI_AUTH_MECHANISM);
    return true;
  } else {
    return evaluate_challenge(GSSAPI_AUTH_SERVER_INITIAL_CHALLENGE, response);
  }
}

bool DseGssapiAuthenticator::evaluate_challenge(const String& token, String* response) {
  if (token == GSSAPI_AUTH_SERVER_INITIAL_CHALLENGE) {
    if (impl_->process(String()) == GssapiAuthenticatorImpl::RESULT_ERROR) {
      set_error("GSSAPI initial handshake failed: " + impl_->error());
      return false;
    }
  } else {
    if (impl_->process(token) == GssapiAuthenticatorImpl::RESULT_ERROR) {
      set_error("GSSAPI challenge handshake failed: " + impl_->error());
      return false;
    }
  }
  *response = impl_->response();
  return true;
}

bool DseGssapiAuthenticator::success(const String& token) {
  // no-op
  return true;
}
