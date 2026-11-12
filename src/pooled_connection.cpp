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

#include "pooled_connection.hpp"

#include "connection_pool_manager.hpp"
#include "event_loop.hpp"
#include "query_request.hpp"

using namespace datastax;
using namespace datastax::internal::core;

/**
 * A request callback that sets the keyspace then runs the original request
 * callback. This happens when the current keyspace wasn't set or has been
 * changed.
 */
class ChainedSetKeyspaceCallback : public SimpleRequestCallback {
public:
  ChainedSetKeyspaceCallback(Connection* connection, const String& keyspace,
                             const RequestCallback::Ptr& chained_callback);

private:
  class SetKeyspaceRequest : public QueryRequest {
  public:
    SetKeyspaceRequest(const String& keyspace, uint64_t request_timeout_ms)
        : QueryRequest("USE " + keyspace) {
      set_request_timeout_ms(request_timeout_ms);
    }
  };

private:
  virtual void on_internal_set(ResponseMessage* response);
  virtual void on_internal_error(CassError code, const String& message);
  virtual void on_internal_timeout();

  void on_result_response(ResponseMessage* response);

private:
  Connection* connection_;
  RequestCallback::Ptr chained_callback_;
};

ChainedSetKeyspaceCallback::ChainedSetKeyspaceCallback(Connection* connection,
                                                       const String& keyspace,
                                                       const RequestCallback::Ptr& chained_callback)
    : SimpleRequestCallback(Request::ConstPtr(
          new SetKeyspaceRequest(keyspace, chained_callback->request_timeout_ms())))
    , connection_(connection)
    , chained_callback_(chained_callback) {}

void ChainedSetKeyspaceCallback::on_internal_set(ResponseMessage* response) {
  switch (response->opcode()) {
    case CQL_OPCODE_RESULT:
      on_result_response(response);
      break;
    case CQL_OPCODE_ERROR:
      connection_->defunct();
      chained_callback_->on_error(CASS_ERROR_LIB_UNABLE_TO_SET_KEYSPACE, "Unable to set keyspace");
      break;
    default:
      connection_->defunct();
      chained_callback_->on_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE, "Unexpected response");
      break;
  }
}

void ChainedSetKeyspaceCallback::on_internal_error(CassError code, const String& message) {
  connection_->defunct();
  chained_callback_->on_error(CASS_ERROR_LIB_UNABLE_TO_SET_KEYSPACE, "Unable to set keyspace");
}

void ChainedSetKeyspaceCallback::on_internal_timeout() { chained_callback_->on_retry_next_host(); }

void ChainedSetKeyspaceCallback::on_result_response(ResponseMessage* response) {
  ResultResponse* result = static_cast<ResultResponse*>(response->response_body().get());
  if (result->kind() == CASS_RESULT_KIND_SET_KEYSPACE) {
    if (connection_->write_and_flush(chained_callback_) < 0) {
      // Try on the same host but a different connection
      chained_callback_->on_retry_current_host();
    }
  } else {
    connection_->defunct();
    chained_callback_->on_error(CASS_ERROR_LIB_UNABLE_TO_SET_KEYSPACE, "Unable to set keyspace");
  }
}

PooledConnection::PooledConnection(ConnectionPool* pool, const Connection::Ptr& connection)
    : connection_(connection)
    , pool_(pool)
    // If the user data is set then use it to start the I/O timer.
    , event_loop_(static_cast<EventLoop*>(pool->loop()->data)) {
  inc_ref(); // Reference for the connection's lifetime
  connection_->set_listener(this);
}

int32_t PooledConnection::write(RequestCallback* callback) {
  int32_t result;
  const String& keyspace(pool_->keyspace());
  if (keyspace != connection_->keyspace()) {
    LOG_DEBUG("Setting keyspace %s on connection(%p) pool(%p)", keyspace.c_str(),
              static_cast<void*>(connection_.get()), static_cast<void*>(pool_));
    result = connection_->write(RequestCallback::Ptr(new ChainedSetKeyspaceCallback(
        connection_.get(), keyspace, RequestCallback::Ptr(callback))));
  } else {
    result = connection_->write(RequestCallback::Ptr(callback));
  }

  if (result > 0) {
    pool_->requires_flush(this, ConnectionPool::Protected());
  }

  return result;
}

void PooledConnection::flush() {
  size_t bytes_flushed = connection_->flush();
#ifdef CASS_INTERNAL_DIAGNOSTICS
  if (bytes_flushed > 0) {
    pool_->manager()->flush_bytes().record_value(bytes_flushed);
  }
#else
  UNUSED_(bytes_flushed);
#endif
}

void PooledConnection::close() { connection_->close(); }

void PooledConnection::start_graceful_drain() { connection_->start_graceful_drain(); }

int PooledConnection::inflight_request_count() const {
  return connection_->inflight_request_count();
}

bool PooledConnection::is_closing() const {
  // A draining connection (CEP-59) does not accept new requests.
  return connection_->is_closing() || connection_->is_draining();
}

void PooledConnection::on_event(const EventResponse::Ptr& response) {
  if (response && response->event_type() == CASS_EVENT_GRACEFUL_DISCONNECT) {
    // The wrapped connection has already started draining itself; notify the
    // pool so that all connections to this host are drained (CEP-59).
    pool_->on_graceful_disconnect(ConnectionPool::Protected());
  }
}

void PooledConnection::on_read() {
  if (event_loop_) {
    event_loop_->maybe_start_io_time();
  }
}

void PooledConnection::on_write() {
  if (event_loop_) {
    event_loop_->maybe_start_io_time();
  }
}

void PooledConnection::on_close(Connection* connection) {
  pool_->close_connection(this, ConnectionPool::Protected());
  dec_ref();
}
