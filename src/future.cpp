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

#include "future.hpp"

#include "external.hpp"
#include "prepared.hpp"
#include "request_handler.hpp"
#include "result_response.hpp"
#include "scoped_ptr.hpp"

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

extern "C" {

void cass_future_free(CassFuture* future) {
  // Futures can be deleted without being waited on
  // because they'll be cleaned up by the notifying thread
  future->dec_ref();
}

CassError cass_future_set_callback(CassFuture* future, CassFutureCallback callback, void* data) {
  if (!future->set_callback(callback, data)) {
    return CASS_ERROR_LIB_CALLBACK_ALREADY_SET;
  }
  return CASS_OK;
}

cass_bool_t cass_future_ready(CassFuture* future) {
  return static_cast<cass_bool_t>(future->ready());
}

void cass_future_wait(CassFuture* future) { future->wait(); }

cass_bool_t cass_future_wait_timed(CassFuture* future, cass_duration_t wait_us) {
  return static_cast<cass_bool_t>(future->wait_for(wait_us));
}

const CassResult* cass_future_get_result(CassFuture* future) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return NULL;
  }

  Response::Ptr response(static_cast<ResponseFuture*>(future->from())->response());
  if (!response || response->opcode() == CQL_OPCODE_ERROR) {
    return NULL;
  }

  response->inc_ref();
  return CassResult::to(static_cast<ResultResponse*>(response.get()));
}

const CassPrepared* cass_future_get_prepared(CassFuture* future) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return NULL;
  }
  ResponseFuture* response_future = static_cast<ResponseFuture*>(future->from());

  SharedRefPtr<ResultResponse> result(response_future->response());
  if (!result || result->kind() != CASS_RESULT_KIND_PREPARED) {
    return NULL;
  }

  Prepared* prepared =
      new Prepared(result, response_future->prepare_request, *response_future->schema_metadata);
  prepared->inc_ref();
  return CassPrepared::to(prepared);
}

const CassErrorResult* cass_future_get_error_result(CassFuture* future) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return NULL;
  }

  Response::Ptr response(static_cast<ResponseFuture*>(future->from())->response());
  if (!response || response->opcode() != CQL_OPCODE_ERROR) {
    return NULL;
  }

  response->inc_ref();
  return CassErrorResult::to(static_cast<ErrorResponse*>(response.get()));
}

CassError cass_future_error_code(CassFuture* future) {
  const Future::Error* error = future->error();
  if (error != NULL) {
    return error->code;
  } else {
    return CASS_OK;
  }
}

void cass_future_error_message(CassFuture* future, const char** message, size_t* message_length) {
  const Future::Error* error = future->error();
  if (error != NULL) {
    const String& m = error->message;
    *message = m.data();
    *message_length = m.length();
  } else {
    *message = "";
    *message_length = 0;
  }
}

CassError cass_future_tracing_id(CassFuture* future, CassUuid* tracing_id) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return CASS_ERROR_LIB_INVALID_FUTURE_TYPE;
  }

  Response::Ptr response(static_cast<ResponseFuture*>(future->from())->response());
  if (!response || !response->has_tracing_id()) {
    return CASS_ERROR_LIB_NO_TRACING_ID;
  }

  *tracing_id = response->tracing_id();

  return CASS_OK;
}

size_t cass_future_custom_payload_item_count(CassFuture* future) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return 0;
  }
  Response::Ptr response(static_cast<ResponseFuture*>(future->from())->response());
  if (!response) return 0;
  return response->custom_payload().size();
}

CassError cass_future_custom_payload_item(CassFuture* future, size_t index, const char** name,
                                          size_t* name_length, const cass_byte_t** value,
                                          size_t* value_size) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return CASS_ERROR_LIB_INVALID_FUTURE_TYPE;
  }
  Response::Ptr response(static_cast<ResponseFuture*>(future->from())->response());
  if (!response) return CASS_ERROR_LIB_NO_CUSTOM_PAYLOAD;

  const CustomPayloadVec& custom_payload = response->custom_payload();
  if (index >= custom_payload.size()) {
    return CASS_ERROR_LIB_INDEX_OUT_OF_BOUNDS;
  }

  const CustomPayloadItem& item = custom_payload[index];
  *name = item.name.data();
  *name_length = item.name.size();
  *value = reinterpret_cast<const cass_byte_t*>(item.value.data());
  *value_size = item.value.size();
  return CASS_OK;
}

const CassNode* cass_future_coordinator(CassFuture* future) {
  if (future->type() != Future::FUTURE_TYPE_RESPONSE) {
    return NULL;
  }
  const Address& node = static_cast<ResponseFuture*>(future->from())->address();
  return node.is_valid() ? CassNode::to(&node) : NULL;
}

} // extern "C"

bool Future::set_callback(Future::Callback callback, void* data) {
  ScopedMutex lock(&mutex_);
  if (callback_) {
    return false; // Callback is already set
  }
  callback_ = callback;
  data_ = data;
  if (is_set_) {
    // Run the callback if the future is already set
    lock.unlock();
    callback(CassFuture::to(this), data);
  }
  return true;
}

void Future::internal_set(ScopedMutex& lock) {

  /* CASSCPP-6 Identify the thread performing set so that wait() calls form this thread won't block */
  set_caller_ = uv_thread_self();

  if (callback_) {
    Callback callback = callback_;
    void* data = data_;
    lock.unlock();
    callback(CassFuture::to(this), data);
    lock.lock();
  }

  /* CPP-987

     Don't mark future as set until _after_ callbacks have run */
  is_set_ = true;

  set_caller_ = 0;

  // Broadcast after we've run the callback so that threads waiting
  // on this future see the side effects of the callback.
  uv_cond_broadcast(&cond_);
}
