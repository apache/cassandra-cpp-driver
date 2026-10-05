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

#include "statement.hpp"

#include "collection.hpp"
#include "execute_request.hpp"
#include "external.hpp"
#include "macros.hpp"
#include "prepared.hpp"
#include "protocol.hpp"
#include "query_request.hpp"
#include "request_callback.hpp"
#include "scoped_ptr.hpp"
#include "string_ref.hpp"
#include "tuple.hpp"
#include "user_type_value.hpp"
#include "utils.hpp"

#include <uv.h>

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

extern "C" {

CassStatement* cass_statement_new(const char* query, size_t parameter_count) {
  return cass_statement_new_n(query, SAFE_STRLEN(query), parameter_count);
}

CassStatement* cass_statement_new_n(const char* query, size_t query_length,
                                    size_t parameter_count) {
  QueryRequest* query_request = new QueryRequest(query, query_length, parameter_count);
  query_request->inc_ref();
  return CassStatement::to(query_request);
}

CassError cass_statement_reset_parameters(CassStatement* statement, size_t count) {
  statement->reset(count);
  return CASS_OK;
}

CassError cass_statement_add_key_index(CassStatement* statement, size_t index) {
  if (statement->kind() != CASS_BATCH_KIND_QUERY) return CASS_ERROR_LIB_BAD_PARAMS;
  if (index >= statement->elements().size()) return CASS_ERROR_LIB_BAD_PARAMS;
  statement->add_key_index(index);
  return CASS_OK;
}

CassError cass_statement_set_keyspace(CassStatement* statement, const char* keyspace) {
  return cass_statement_set_keyspace_n(statement, keyspace, SAFE_STRLEN(keyspace));
}

CassError cass_statement_set_keyspace_n(CassStatement* statement, const char* keyspace,
                                        size_t keyspace_length) {
  // The keyspace is set by the prepared metadata
  if (statement->opcode() == CQL_OPCODE_EXECUTE) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  statement->set_keyspace(String(keyspace, keyspace_length));
  return CASS_OK;
}

void cass_statement_free(CassStatement* statement) { statement->dec_ref(); }

CassError cass_statement_set_consistency(CassStatement* statement, CassConsistency consistency) {
  statement->set_consistency(consistency);
  return CASS_OK;
}

CassError cass_statement_set_serial_consistency(CassStatement* statement,
                                                CassConsistency serial_consistency) {
  statement->set_serial_consistency(serial_consistency);
  return CASS_OK;
}

CassError cass_statement_set_paging_size(CassStatement* statement, int page_size) {
  statement->set_page_size(page_size);
  return CASS_OK;
}

CassError cass_statement_set_paging_state(CassStatement* statement, const CassResult* result) {
  statement->set_paging_state(result->paging_state().to_string());
  return CASS_OK;
}

CassError cass_statement_set_paging_state_token(CassStatement* statement, const char* paging_state,
                                                size_t paging_state_size) {
  statement->set_paging_state(String(paging_state, paging_state_size));
  return CASS_OK;
}

CassError cass_statement_set_retry_policy(CassStatement* statement, CassRetryPolicy* retry_policy) {
  statement->set_retry_policy(retry_policy);
  return CASS_OK;
}

CassError cass_statement_set_timestamp(CassStatement* statement, cass_int64_t timestamp) {
  statement->set_timestamp(timestamp);
  return CASS_OK;
}

CassError cass_statement_set_request_timeout(CassStatement* statement, cass_uint64_t timeout_ms) {
  statement->set_request_timeout_ms(timeout_ms);
  return CASS_OK;
}

CassError cass_statement_set_is_idempotent(CassStatement* statement, cass_bool_t is_idempotent) {
  statement->set_is_idempotent(is_idempotent == cass_true);
  return CASS_OK;
}

CassError cass_statement_set_custom_payload(CassStatement* statement,
                                            const CassCustomPayload* payload) {
  statement->set_custom_payload(payload);
  return CASS_OK;
}

CassError cass_statement_set_execution_profile(CassStatement* statement, const char* name) {
  return cass_statement_set_execution_profile_n(statement, name, SAFE_STRLEN(name));
}

CassError cass_statement_set_execution_profile_n(CassStatement* statement, const char* name,
                                                 size_t name_length) {
  if (name_length > 0) {
    statement->set_execution_profile_name(String(name, name_length));
  } else {
    statement->set_execution_profile_name(String());
  }
  return CASS_OK;
}

CassError cass_statement_set_tracing(CassStatement* statement, cass_bool_t enabled) {
  statement->set_tracing(enabled == cass_true);
  return CASS_OK;
}

CassError cass_statement_set_host(CassStatement* statement, const char* host, int port) {
  return cass_statement_set_host_n(statement, host, SAFE_STRLEN(host), port);
}

CassError cass_statement_set_host_n(CassStatement* statement, const char* host, size_t host_length,
                                    int port) {
  Address address(String(host, host_length), port);
  if (!address.is_valid_and_resolved()) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  statement->set_host(address);
  return CASS_OK;
}

CassError cass_statement_set_host_inet(CassStatement* statement, const CassInet* host, int port) {
  Address address(host->address, host->address_length, port);
  if (!address.is_valid_and_resolved()) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  statement->set_host(address);
  return CASS_OK;
}

CassError cass_statement_set_node(CassStatement* statement, const CassNode* node) {
  if (node == NULL) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  statement->set_host(*node->from());
  return CASS_OK;
}

#define CASS_STATEMENT_BIND(Name, Params, Value)                                               \
  CassError cass_statement_bind_##Name(CassStatement* statement, size_t index Params) {        \
    return statement->set(index, Value);                                                       \
  }                                                                                            \
  CassError cass_statement_bind_##Name##_by_name(CassStatement* statement,                     \
                                                 const char* name Params) {                    \
    return statement->set(StringRef(name), Value);                                             \
  }                                                                                            \
  CassError cass_statement_bind_##Name##_by_name_n(CassStatement* statement, const char* name, \
                                                   size_t name_length Params) {                \
    return statement->set(StringRef(name, name_length), Value);                                \
  }

CASS_STATEMENT_BIND(null, ZERO_PARAMS_(), CassNull())
CASS_STATEMENT_BIND(int8, ONE_PARAM_(cass_int8_t value), value)
CASS_STATEMENT_BIND(int16, ONE_PARAM_(cass_int16_t value), value)
CASS_STATEMENT_BIND(int32, ONE_PARAM_(cass_int32_t value), value)
CASS_STATEMENT_BIND(uint32, ONE_PARAM_(cass_uint32_t value), value)
CASS_STATEMENT_BIND(int64, ONE_PARAM_(cass_int64_t value), value)
CASS_STATEMENT_BIND(float, ONE_PARAM_(cass_float_t value), value)
CASS_STATEMENT_BIND(double, ONE_PARAM_(cass_double_t value), value)
CASS_STATEMENT_BIND(bool, ONE_PARAM_(cass_bool_t value), value)
CASS_STATEMENT_BIND(uuid, ONE_PARAM_(CassUuid value), value)
CASS_STATEMENT_BIND(inet, ONE_PARAM_(CassInet value), value)
CASS_STATEMENT_BIND(collection, ONE_PARAM_(const CassCollection* value), value->from())
CASS_STATEMENT_BIND(tuple, ONE_PARAM_(const CassTuple* value), value->from())
CASS_STATEMENT_BIND(user_type, ONE_PARAM_(const CassUserType* value), value->from())
CASS_STATEMENT_BIND(bytes, TWO_PARAMS_(const cass_byte_t* value, size_t value_size),
                    CassBytes(value, value_size))
CASS_STATEMENT_BIND(decimal,
                    THREE_PARAMS_(const cass_byte_t* varint, size_t varint_size, int scale),
                    CassDecimal(varint, varint_size, scale))
CASS_STATEMENT_BIND(duration,
                    THREE_PARAMS_(cass_int32_t months, cass_int32_t days, cass_int64_t nanos),
                    CassDuration(months, days, nanos))

#undef CASS_STATEMENT_BIND

CassError cass_statement_bind_string(CassStatement* statement, size_t index, const char* value) {
  return cass_statement_bind_string_n(statement, index, value, SAFE_STRLEN(value));
}

CassError cass_statement_bind_string_n(CassStatement* statement, size_t index, const char* value,
                                       size_t value_length) {
  return statement->set(index, CassString(value, value_length));
}

CassError cass_statement_bind_string_by_name(CassStatement* statement, const char* name,
                                             const char* value) {
  return statement->set(StringRef(name), CassString(value, SAFE_STRLEN(value)));
}

CassError cass_statement_bind_string_by_name_n(CassStatement* statement, const char* name,
                                               size_t name_length, const char* value,
                                               size_t value_length) {
  return statement->set(StringRef(name, name_length), CassString(value, value_length));
}

CassError cass_statement_bind_custom(CassStatement* statement, size_t index, const char* class_name,
                                     const cass_byte_t* value, size_t value_size) {
  return statement->set(index, CassCustom(StringRef(class_name), value, value_size));
}

CassError cass_statement_bind_custom_n(CassStatement* statement, size_t index,
                                       const char* class_name, size_t class_name_length,
                                       const cass_byte_t* value, size_t value_size) {
  return statement->set(index,
                        CassCustom(StringRef(class_name, class_name_length), value, value_size));
}

CassError cass_statement_bind_custom_by_name(CassStatement* statement, const char* name,
                                             const char* class_name, const cass_byte_t* value,
                                             size_t value_size) {
  return statement->set(StringRef(name), CassCustom(StringRef(class_name), value, value_size));
}

CassError cass_statement_bind_custom_by_name_n(CassStatement* statement, const char* name,
                                               size_t name_length, const char* class_name,
                                               size_t class_name_length, const cass_byte_t* value,
                                               size_t value_size) {
  return statement->set(StringRef(name, name_length),
                        CassCustom(StringRef(class_name, class_name_length), value, value_size));
}

} // extern "C"

Statement::Statement(const char* query, size_t query_length, size_t values_count)
    : RoutableRequest(CQL_OPCODE_QUERY)
    , AbstractData(values_count)
    , query_or_id_(sizeof(int32_t) + query_length)
    , flags_(0)
    , page_size_(-1) {
  // <query> [long string]
  query_or_id_.encode_long_string(0, query, query_length);
}

Statement::Statement(const Prepared* prepared)
    : RoutableRequest(CQL_OPCODE_EXECUTE)
    , AbstractData(prepared->result()->column_count())
    , query_or_id_(sizeof(uint16_t) + prepared->id().size())
    , flags_(0)
    , page_size_(-1) {
  // <id> [short bytes] (or [string])
  const String& id = prepared->id();
  query_or_id_.encode_string(0, id.data(), static_cast<uint16_t>(id.size()));
  // Inherit settings and keyspace from the prepared statement
  set_settings(prepared->request_settings());
  // If the keyspace wasn't explictly set then attempt to set it using the
  // prepared statement's result metadata.
  if (keyspace().empty()) {
    set_keyspace(prepared->result()->quoted_keyspace());
  }
}

String Statement::query() const {
  if (opcode() == CQL_OPCODE_QUERY) {
    return String(query_or_id_.data() + sizeof(int32_t), query_or_id_.size() - sizeof(int32_t));
  }
  return String();
}

// Format: <kind><string_or_id><n><value_1>...<value_n>
// where:
// <kind> is a [byte]
// <string_or_id> is a [long string] for <string> and a [short bytes] for <id>
// <n> is a [short]
// <value> is a [bytes]
int32_t Statement::encode_batch(ProtocolVersion version, RequestCallback* callback,
                                BufferVec* bufs) const {
  int32_t length = 0;

  { // <kind> [byte]
    bufs->push_back(Buffer(sizeof(uint8_t)));
    Buffer& buf = bufs->back();
    buf.encode_byte(0, kind());
    length += sizeof(uint8_t);
  }

  bufs->push_back(query_or_id_);
  length += query_or_id_.size();

  { // <n> [short]
    bufs->push_back(Buffer(sizeof(uint16_t)));
    Buffer& buf = bufs->back();
    buf.encode_uint16(0, static_cast<uint16_t>(elements().size()));
    length += sizeof(uint16_t);
  }

  if (elements().size() > 0) {
    int32_t result = encode_values(version, callback, bufs);
    if (result < 0) return result;
    length += result;
  }

  return length;
}

bool Statement::with_keyspace(ProtocolVersion version) const {
  return version.supports_set_keyspace() &&
         // Execute requests (bound statements) use the keyspace
         // from the time of prepare.
         opcode() != CQL_OPCODE_EXECUTE && !keyspace().empty();
}

// For query statements the format is:
// <query><consistency><flags><n>
// where:
// <query> has the format [long string]
// <consistency> is a [short]
// <flags> is a [byte] (or [int] for protocol v5)
// <n> is a [short]
//
// For execute statements the format is:
// <id><consistency><flags><n>
// where:
// <id> has the format [short bytes] (or [string])
// <consistency> is a [short]
// <flags> is a [byte] (or [int] for protocol v5)
// <n> is a [short]

int32_t Statement::encode_query_or_id(BufferVec* bufs) const {
  bufs->push_back(query_or_id_);
  return query_or_id_.size();
}

int32_t Statement::encode_begin(ProtocolVersion version, uint16_t element_count,
                                RequestCallback* callback, BufferVec* bufs) const {
  int32_t length = 0;
  size_t query_params_buf_size = 0;
  int32_t flags = flags_;

  if (callback->skip_metadata()) {
    flags |= CASS_QUERY_FLAG_SKIP_METADATA;
  }

  query_params_buf_size += sizeof(uint16_t); // <consistency> [short]

  query_params_buf_size += version.query_flags_size(); // <flags> [int] (v5+) or [byte]

  if (element_count > 0) {
    query_params_buf_size += sizeof(uint16_t); // <n> [short]
    flags |= CASS_QUERY_FLAG_VALUES;
  }

  if (page_size() > 0) {
    flags |= CASS_QUERY_FLAG_PAGE_SIZE;
  }

  if (!paging_state().empty()) {
    flags |= CASS_QUERY_FLAG_PAGING_STATE;
  }

  if (callback->serial_consistency() != 0) {
    flags |= CASS_QUERY_FLAG_SERIAL_CONSISTENCY;
  }

  if (callback->timestamp() != CASS_INT64_MIN) {
    flags |= CASS_QUERY_FLAG_DEFAULT_TIMESTAMP;
  }

  if (version.supports_now_in_seconds() && callback->now_in_seconds() != CASS_INT32_MIN) {
    flags |= CASS_QUERY_FLAG_NOW_IN_SECONDS;
  }

  if (with_keyspace(version)) {
    flags |= CASS_QUERY_FLAG_WITH_KEYSPACE;
  }

  bufs->push_back(Buffer(query_params_buf_size));
  length += query_params_buf_size;

  Buffer& buf = bufs->back();
  size_t pos = buf.encode_uint16(0, callback->consistency());

  if (version.query_flags_size() == sizeof(int32_t)) {
    // v5 widened <flags> to [int] to make room for With_now_in_seconds.
    pos = buf.encode_int32(pos, flags);
  } else {
    pos = buf.encode_byte(pos, static_cast<uint8_t>(flags));
  }

  if (element_count > 0) {
    buf.encode_uint16(pos, element_count);
  }

  return length;
}

// Format: [<value_1>...<value_n>]
// where:
// <value> is a [bytes]
int32_t Statement::encode_values(ProtocolVersion version, RequestCallback* callback,
                                 BufferVec* bufs) const {
  int32_t length = 0;
  for (size_t i = 0; i < elements().size(); ++i) {
    const Element& element = elements()[i];
    if (!element.is_unset()) {
      bufs->push_back(element.get_buffer());
    } else {
      if (version >= CASS_PROTOCOL_VERSION_V4) {
        bufs->push_back(core::encode_with_length(CassUnset()));
      } else {
        OStringStream ss;
        ss << "Query parameter at index " << i << " was not set";
        callback->on_error(CASS_ERROR_LIB_PARAMETER_UNSET, ss.str());
        return Request::REQUEST_ERROR_PARAMETER_UNSET;
      }
    }
    length += bufs->back().size();
  }
  return length;
}

// Format: [<result_page_size>][<paging_state>][<serial_consistency>][<timestamp>]
// where:
// <result_page_size> is a [int]
// <paging_state> is a [bytes]
// <serial_consistency> is a [short]
// <timestamp> is a [long]
// <keyspace> is a [string]
int32_t Statement::encode_end(ProtocolVersion version, RequestCallback* callback,
                              BufferVec* bufs) const {
  int32_t length = 0;
  size_t paging_buf_size = 0;

  bool with_keyspace = this->with_keyspace(version);
  // The v5 keyspace field holds the keyspace name itself rather than a CQL
  // identifier, so remove any quoting the caller supplied.
  String keyspace = with_keyspace ? unescape_id(this->keyspace()) : String();

  if (page_size() > 0) {
    paging_buf_size += sizeof(int32_t); // [int]
  }

  if (!paging_state().empty()) {
    paging_buf_size += sizeof(int32_t) + paging_state().size(); // [bytes]
  }

  if (callback->serial_consistency() != 0) {
    paging_buf_size += sizeof(uint16_t); // [short]
  }

  if (callback->timestamp() != CASS_INT64_MIN) {
    paging_buf_size += sizeof(int64_t); // [long]
  }

  if (version.supports_now_in_seconds() && callback->now_in_seconds() != CASS_INT32_MIN) {
    paging_buf_size += sizeof(int32_t); // <now_in_seconds> [int]
  }

  if (!keyspace.empty()) {
    paging_buf_size += sizeof(uint16_t) + keyspace.size();
  }

  if (paging_buf_size > 0) {
    bufs->push_back(Buffer(paging_buf_size));
    length += paging_buf_size;

    Buffer& buf = bufs->back();
    size_t pos = 0;

    if (page_size() >= 0) {
      pos = buf.encode_int32(pos, page_size());
    }

    if (!paging_state().empty()) {
      pos = buf.encode_bytes(pos, paging_state().data(), paging_state().size());
    }

    if (callback->serial_consistency() != 0) {
      pos = buf.encode_uint16(pos, callback->serial_consistency());
    }

    if (callback->timestamp() != CASS_INT64_MIN) {
      pos = buf.encode_int64(pos, callback->timestamp());
    }

    if (!keyspace.empty()) {
      pos = buf.encode_string(pos, keyspace.data(), static_cast<uint16_t>(keyspace.size()));
    }

    // <now_in_seconds> is the last field of <query_parameters>.
    if (version.supports_now_in_seconds() && callback->now_in_seconds() != CASS_INT32_MIN) {
      pos = buf.encode_int32(pos, callback->now_in_seconds());
    }
  }

  return length;
}

bool Statement::calculate_routing_key(const Vector<size_t>& key_indices,
                                      String* routing_key) const {
  if (key_indices.empty()) return false;

  if (key_indices.size() == 1) {
    assert(key_indices.front() < elements().size());
    const AbstractData::Element& element(elements()[key_indices.front()]);
    if (element.is_unset() || element.is_null()) {
      return false;
    }
    Buffer buf(element.get_buffer());
    routing_key->assign(buf.data() + sizeof(int32_t), buf.size() - sizeof(int32_t));
  } else {
    size_t length = 0;

    for (Vector<size_t>::const_iterator i = key_indices.begin(); i != key_indices.end(); ++i) {
      assert(*i < elements().size());
      const AbstractData::Element& element(elements()[*i]);
      if (element.is_unset() || element.is_null()) {
        return false;
      }
      size_t size = element.get_size() - sizeof(int32_t);
      length += sizeof(uint16_t) + size + 1;
    }

    routing_key->clear();
    routing_key->reserve(length);

    for (Vector<size_t>::const_iterator i = key_indices.begin(); i != key_indices.end(); ++i) {
      const AbstractData::Element& element(elements()[*i]);
      Buffer buf(element.get_buffer());
      size_t size = buf.size() - sizeof(int32_t);

      char size_buf[sizeof(uint16_t)];
      encode_uint16(size_buf, static_cast<uint16_t>(size));
      routing_key->append(size_buf, sizeof(uint16_t));
      routing_key->append(buf.data() + sizeof(int32_t), size);
      routing_key->push_back(0);
    }
  }

  return true;
}
