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

#include "batch_request.hpp"

#include "constants.hpp"
#include "execute_request.hpp"
#include "external.hpp"
#include "protocol.hpp"
#include "request_callback.hpp"
#include "serialization.hpp"
#include "statement.hpp"
#include "utils.hpp"

using namespace datastax;
using namespace datastax::internal::core;

extern "C" {

CassBatch* cass_batch_new(CassBatchType type) {
  BatchRequest* batch = new BatchRequest(type);
  batch->inc_ref();
  return CassBatch::to(batch);
}

void cass_batch_free(CassBatch* batch) { batch->dec_ref(); }

CassError cass_batch_set_keyspace(CassBatch* batch, const char* keyspace) {
  return cass_batch_set_keyspace_n(batch, keyspace, SAFE_STRLEN(keyspace));
}

CassError cass_batch_set_keyspace_n(CassBatch* batch, const char* keyspace,
                                    size_t keyspace_length) {
  batch->set_keyspace(String(keyspace, keyspace_length));
  return CASS_OK;
}

CassError cass_batch_set_consistency(CassBatch* batch, CassConsistency consistency) {
  batch->set_consistency(consistency);
  return CASS_OK;
}

CassError cass_batch_set_serial_consistency(CassBatch* batch, CassConsistency serial_consistency) {
  batch->set_serial_consistency(serial_consistency);
  return CASS_OK;
}

CassError cass_batch_set_timestamp(CassBatch* batch, cass_int64_t timestamp) {
  batch->set_timestamp(timestamp);
  return CASS_OK;
}

CassError cass_batch_set_request_timeout(CassBatch* batch, cass_uint64_t timeout_ms) {
  batch->set_request_timeout_ms(timeout_ms);
  return CASS_OK;
}

CassError cass_batch_set_is_idempotent(CassBatch* batch, cass_bool_t is_idempotent) {
  batch->set_is_idempotent(is_idempotent == cass_true);
  return CASS_OK;
}

CassError cass_batch_set_retry_policy(CassBatch* batch, CassRetryPolicy* retry_policy) {
  batch->set_retry_policy(retry_policy);
  return CASS_OK;
}

CassError cass_batch_set_custom_payload(CassBatch* batch, const CassCustomPayload* payload) {
  batch->set_custom_payload(payload);
  return CASS_OK;
}

CassError cass_batch_set_tracing(CassBatch* batch, cass_bool_t enabled) {
  batch->set_tracing(enabled == cass_true);
  return CASS_OK;
}

CassError cass_batch_add_statement(CassBatch* batch, CassStatement* statement) {
  batch->add_statement(statement);
  return CASS_OK;
}

CassError cass_batch_set_execution_profile(CassBatch* batch, const char* name) {
  return cass_batch_set_execution_profile_n(batch, name, SAFE_STRLEN(name));
}

CassError cass_batch_set_execution_profile_n(CassBatch* batch, const char* name,
                                             size_t name_length) {
  if (name_length > 0) {
    batch->set_execution_profile_name(String(name, name_length));
  } else {
    batch->set_execution_profile_name(String());
  }
  return CASS_OK;
}

} // extern "C"

// Format: <type><n><query_1>...<query_n><consistency><flags>[<serial_consistency>][<timestamp>]
// where:
// <type> is a [byte]
// <n> is a [short]
// <query> has the format <kind><string_or_id><n>[<name_1>]<value_1>...[<name_n>]<value_n>
// <consistency> is a [short]
// Only protocol v3 and higher for the following:
// <flags> is a [byte] (or [int] for protocol v5)
// <serial_consistency> is a [short]
// <timestamp> is a [long]
int BatchRequest::encode(ProtocolVersion version, RequestCallback* callback,
                         BufferVec* bufs) const {
  int length = 0;
  uint32_t flags = 0;

  {
    // <type> [byte] + <n> [short]
    size_t buf_size = sizeof(uint8_t) + sizeof(uint16_t);

    Buffer buf(buf_size);

    size_t pos = buf.encode_byte(0, type_);
    buf.encode_uint16(pos, static_cast<uint16_t>(statements().size()));

    bufs->push_back(buf);
    length += buf_size;
  }

  for (BatchRequest::StatementVec::const_iterator i = statements_.begin(), end = statements_.end();
       i != end; ++i) {
    const Statement::Ptr& statement(*i);
    if (statement->has_names_for_values()) {
      callback->on_error(CASS_ERROR_LIB_BAD_PARAMS,
                         "Batches cannot contain queries with named values");
      return REQUEST_ERROR_BATCH_WITH_NAMED_VALUES;
    }
    int32_t result = statement->encode_batch(version, callback, bufs);
    if (result < 0) {
      return result;
    }
    length += result;
  }

  {
    // <consistency> [short]
    size_t buf_size = sizeof(uint16_t);

    // <flags>[<serial_consistency><timestamp><keyspace><now_in_seconds>]
    buf_size += version.query_flags_size(); // [int] (v5+) or [byte]

    if (callback->serial_consistency() != 0) {
      buf_size += sizeof(uint16_t); // [short]
      flags |= CASS_QUERY_FLAG_SERIAL_CONSISTENCY;
    }

    if (callback->timestamp() != CASS_INT64_MIN) {
      buf_size += sizeof(int64_t); // [long]
      flags |= CASS_QUERY_FLAG_DEFAULT_TIMESTAMP;
    }

    if (version.supports_now_in_seconds() && callback->now_in_seconds() != CASS_INT32_MIN) {
      buf_size += sizeof(int32_t); // <now_in_seconds> [int]
      flags |= CASS_QUERY_FLAG_NOW_IN_SECONDS;
    }

    // The v5 keyspace field holds the keyspace name itself rather than a CQL
    // identifier, so remove any quoting the caller supplied.
    String wire_keyspace = version.supports_set_keyspace() ? unescape_id(keyspace()) : String();

    if (!wire_keyspace.empty()) {
      buf_size += sizeof(uint16_t) + wire_keyspace.size();
      flags |= CASS_QUERY_FLAG_WITH_KEYSPACE;
    }

    Buffer buf(buf_size);

    size_t pos = buf.encode_uint16(0, callback->consistency());
    if (version.query_flags_size() == sizeof(int32_t)) {
      // v5 widened <flags> to [int] to make room for With_now_in_seconds.
      pos = buf.encode_int32(pos, flags);
    } else {
      pos = buf.encode_byte(pos, static_cast<uint8_t>(flags));
    }

    if (callback->serial_consistency() != 0) {
      pos = buf.encode_uint16(pos, callback->serial_consistency());
    }

    if (callback->timestamp() != CASS_INT64_MIN) {
      pos = buf.encode_int64(pos, callback->timestamp());
    }

    if (!wire_keyspace.empty()) {
      pos = buf.encode_string(pos, wire_keyspace.data(), static_cast<uint16_t>(wire_keyspace.size()));
    }

    if (version.supports_now_in_seconds() && callback->now_in_seconds() != CASS_INT32_MIN) {
      pos = buf.encode_int32(pos, callback->now_in_seconds());
    }

    bufs->push_back(buf);
    length += buf_size;
  }

  return length;
}

void BatchRequest::add_statement(Statement* statement) {
  // If the keyspace is not set then inherit the keyspace of the first
  // statement with a non-empty keyspace.
  if (keyspace().empty()) {
    set_keyspace(statement->keyspace());
  }
  statements_.push_back(Statement::Ptr(statement));
}

bool BatchRequest::find_prepared_query(const String& id, String* query) const {
  for (StatementVec::const_iterator it = statements_.begin(), end = statements_.end(); it != end;
       ++it) {
    const Statement::Ptr& statement(*it);
    if (statement->kind() == CASS_BATCH_KIND_PREPARED) {
      ExecuteRequest* execute_request = static_cast<ExecuteRequest*>(statement.get());
      if (execute_request->prepared()->id() == id) {
        *query = execute_request->prepared()->query();
        return true;
      }
    }
  }
  return false;
}

bool BatchRequest::get_routing_key(String* routing_key) const {
  for (BatchRequest::StatementVec::const_iterator i = statements_.begin(); i != statements_.end();
       ++i) {
    if ((*i)->get_routing_key(routing_key)) {
      return true;
    }
  }
  return false;
}
