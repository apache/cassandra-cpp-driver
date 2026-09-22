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

#include "request_handler.hpp"

#include "batch_request.hpp"
#include "connection.hpp"
#include "connection_pool_manager.hpp"
#include "constants.hpp"
#include "error_response.hpp"
#include "execute_request.hpp"
#include "metrics.hpp"
#include "prepare_request.hpp"
#include "protocol.hpp"
#include "response.hpp"
#include "result_response.hpp"
#include "row.hpp"
#include "session.hpp"

#include <uv.h>

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

static String to_hex(const String& byte_id) {
  static const char half_byte_to_hex[] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };
  OStringStream ss;

  const char* data = byte_id.data();
  for (size_t i = 0; i < byte_id.length(); ++i) {
    uint8_t byte = static_cast<uint8_t>(data[i]);
    ss << half_byte_to_hex[(byte >> 4) & 0x0F];
    ss << half_byte_to_hex[byte & 0x0F];
  }
  return ss.str();
}

class SingleHostQueryPlan : public QueryPlan {
public:
  SingleHostQueryPlan(const Address& address)
      : host_(new Host(address)) {}

  virtual Host::Ptr compute_next() {
    Host::Ptr temp = host_;
    host_.reset(); // Only return the host once
    return temp;
  }

private:
  Host::Ptr host_;
};

class PrepareCallback : public SimpleRequestCallback {
public:
  PrepareCallback(const String& query, const String& id, RequestExecution* request_execution);

private:
  class PrepareRequest : public core::PrepareRequest {
  public:
    PrepareRequest(const String& query, const String& keyspace, uint64_t request_timeout_ms)
        : core::PrepareRequest(query) {
      set_keyspace(keyspace);
      set_request_timeout_ms(request_timeout_ms);
    }
  };

private:
  virtual void on_internal_set(ResponseMessage* response);
  virtual void on_internal_error(CassError code, const String& message);
  virtual void on_internal_timeout();

private:
  RequestExecution::Ptr request_execution_;
  String id_;
};

PrepareCallback::PrepareCallback(const String& query, const String& id,
                                 RequestExecution* request_execution)
    : SimpleRequestCallback(
          Request::ConstPtr(new PrepareRequest(query, request_execution->request()->keyspace(),
                                               request_execution->request_timeout_ms())))
    , request_execution_(request_execution)
    , id_(id) {}

void PrepareCallback::on_internal_set(ResponseMessage* response) {
  switch (response->opcode()) {
    case CQL_OPCODE_RESULT: {
      ResultResponse* result = static_cast<ResultResponse*>(response->response_body().get());
      if (result->kind() == CASS_RESULT_KIND_PREPARED) {
        String result_id = result->prepared_id().to_string();
        if (id_ != result_id) {
          request_execution_->notify_prepared_id_mismatch(id_, result_id);
        } else {
          request_execution_->notify_result_metadata_changed(request(), result);
          request_execution_->on_retry_current_host();
        }
      } else {
        request_execution_->on_retry_next_host();
      }
    } break;
    case CQL_OPCODE_ERROR:
      request_execution_->on_retry_next_host();
      break;
    default:
      break;
  }
}

void PrepareCallback::on_internal_error(CassError code, const String& message) {
  request_execution_->on_retry_next_host();
}

void PrepareCallback::on_internal_timeout() { request_execution_->on_retry_next_host(); }

class NopRequestListener : public RequestListener {
public:
  virtual void on_prepared_metadata_changed(const String& id,
                                            const PreparedMetadata::Entry::Ptr& entry) {}

  virtual void on_keyspace_changed(const String& keyspace, KeyspaceChangedResponse response) {}

  virtual bool on_wait_for_tracing_data(const RequestHandler::Ptr& request_handler,
                                        const Host::Ptr& current_host,
                                        const Response::Ptr& response) {
    return false;
  }

  virtual bool on_wait_for_schema_agreement(const RequestHandler::Ptr& request_handler,
                                            const Host::Ptr& current_host,
                                            const Response::Ptr& response) {
    return false;
  }

  virtual bool on_prepare_all(const RequestHandler::Ptr& request_handler,
                              const Host::Ptr& current_host, const Response::Ptr& response) {
    return false;
  }

  virtual void on_done() {}
};

static NopRequestListener nop_request_listener__;

RequestHandler::RequestHandler(const Request::ConstPtr& request, const ResponseFuture::Ptr& future,
                               Metrics* metrics)
    : wrapper_(request)
    , future_(future)
    , is_done_(false)
    , running_executions_(0)
    , start_time_ns_(uv_hrtime())
    , listener_(&nop_request_listener__)
    , manager_(NULL)
    , metrics_(metrics) {}

RequestHandler::~RequestHandler() {
  if (Logger::log_level() >= CASS_LOG_TRACE) {
    OStringStream ss;
    for (RequestTryVec::const_iterator it = request_tries_.begin(), end = request_tries_.end();
         it != end; ++it) {
      if (it != request_tries_.begin()) ss << ", ";
      ss << "(" << it->address << ", ";
      if (it->error != CASS_OK) {
        ss << cass_error_desc(it->error);
      } else {
        ss << it->latency;
      }
      ss << ")";
    }
    LOG_TRACE("Speculative execution attempts: [%s]", ss.str().c_str());
  }
}

void RequestHandler::set_prepared_metadata(const PreparedMetadata::Entry::Ptr& entry) {
  wrapper_.set_prepared_metadata(entry);
}

void RequestHandler::init(const ExecutionProfile& profile, ConnectionPoolManager* manager,
                          const TokenMap* token_map, TimestampGenerator* timestamp_generator,
                          RequestListener* listener) {
  manager_ = manager;
  listener_ = listener ? listener : &nop_request_listener__;
  wrapper_.init(profile, timestamp_generator);

  // Attempt to use the statement's keyspace first then if not set then use the session's keyspace
  const String& keyspace(!request()->keyspace().empty() ? request()->keyspace()
                                                        : manager_->keyspace());

  // If a specific host is set then bypass the load balancing policy and use a
  // specialized single host query plan.
  if (request()->host()) {
    query_plan_.reset(new SingleHostQueryPlan(*request()->host()));
  } else {
    query_plan_.reset(profile.load_balancing_policy()->new_query_plan(keyspace, this, token_map));
  }

  execution_plan_.reset(
      profile.speculative_execution_policy()->new_plan(keyspace, wrapper_.request().get()));
}

void RequestHandler::execute() {
  RequestExecution::Ptr request_execution(new RequestExecution(this));
  running_executions_++;
  internal_retry(request_execution.get());
}

void RequestHandler::retry(RequestExecution* request_execution, Protected) {
  internal_retry(request_execution);
}

void RequestHandler::start_request(uv_loop_t* loop, Protected) {
  if (!timer_.is_running()) {
    uint64_t request_timeout_ms = wrapper_.request_timeout_ms();
    if (request_timeout_ms > 0) { // 0 means no timeout
      timer_.start(loop, request_timeout_ms, bind_callback(&RequestHandler::on_timeout, this));
    }
  }
}

Host::Ptr RequestHandler::next_host(Protected) { return query_plan_->compute_next(); }

int64_t RequestHandler::next_execution(const Host::Ptr& current_host, Protected) {
  return execution_plan_->next_execution(current_host);
}

void RequestHandler::add_attempted_address(const Address& address, Protected) {
  future_->add_attempted_address(address);
}

void RequestHandler::notify_result_metadata_changed(const String& prepared_id, const String& query,
                                                    const String& keyspace,
                                                    const String& result_metadata_id,
                                                    const ResultResponse::ConstPtr& result_response,
                                                    Protected) {
  PreparedMetadata::Entry::Ptr entry(
      new PreparedMetadata::Entry(query, keyspace, result_metadata_id, result_response));
  listener_->on_prepared_metadata_changed(prepared_id, entry);
}

void RequestHandler::notify_keyspace_changed(const String& keyspace, const Host::Ptr& current_host,
                                             const Response::Ptr& response) {
  listener_->on_keyspace_changed(
      keyspace, KeyspaceChangedResponse(RequestHandler::Ptr(this), current_host, response));
}

bool RequestHandler::wait_for_tracing_data(const Host::Ptr& current_host,
                                           const Response::Ptr& response) {
  return listener_->on_wait_for_tracing_data(Ptr(this), current_host, response);
}

bool RequestHandler::wait_for_schema_agreement(const Host::Ptr& current_host,
                                               const Response::Ptr& response) {
  return listener_->on_wait_for_schema_agreement(Ptr(this), current_host, response);
}

bool RequestHandler::prepare_all(const Host::Ptr& current_host, const Response::Ptr& response) {
  return listener_->on_prepare_all(Ptr(this), current_host, response);
}

void RequestHandler::set_response(const Host::Ptr& host, const Response::Ptr& response) {
  stop_request();
  running_executions_--;

  if (future_->set_response(host->address(), response)) {
    if (metrics_) {
      metrics_->record_request(uv_hrtime() - start_time_ns_);
    }
  } else {
    // This request is a speculative execution for whom we already processed
    // a response (another speculative execution). So consider this one an
    // aborted speculative execution.
    if (metrics_) {
      metrics_->record_speculative_request(uv_hrtime() - start_time_ns_);
    }
  }

  if (Logger::log_level() >= CASS_LOG_TRACE) {
    request_tries_.push_back(RequestTry(host->address(), uv_hrtime() - start_time_ns_));
  }
}

void RequestHandler::set_error(CassError code, const String& message) {
  stop_request();
  bool skip = (code == CASS_ERROR_LIB_NO_HOSTS_AVAILABLE && --running_executions_ > 0);
  if (!skip) {
    future_->set_error(code, message);
  }
}

void RequestHandler::set_error(const Host::Ptr& host, CassError code, const String& message) {
  stop_request();
  bool skip = (code == CASS_ERROR_LIB_NO_HOSTS_AVAILABLE && --running_executions_ > 0);
  if (!skip) {
    if (host) {
      future_->set_error_with_address(host->address(), code, message);
    } else {
      set_error(code, message);
    }
  }
  if (Logger::log_level() >= CASS_LOG_TRACE) {
    request_tries_.push_back(RequestTry(host->address(), code));
  }
}

void RequestHandler::set_error_with_error_response(const Host::Ptr& host,
                                                   const Response::Ptr& error, CassError code,
                                                   const String& message) {
  stop_request();
  running_executions_--;
  future_->set_error_with_response(host->address(), error, code, message);
  if (Logger::log_level() >= CASS_LOG_TRACE) {
    request_tries_.push_back(RequestTry(host->address(), code));
  }
}

void RequestHandler::stop_timer() { timer_.stop(); }

void RequestHandler::on_timeout(Timer* timer) {
  if (metrics_) {
    metrics_->request_timeouts.inc();
  }
  set_error(CASS_ERROR_LIB_REQUEST_TIMED_OUT, "Request timed out");
  LOG_DEBUG("Request timed out");
}

void RequestHandler::stop_request() {
  if (!is_done_) {
    listener_->on_done();
    is_done_ = true;
  }
  timer_.stop();
}

void RequestHandler::internal_retry(RequestExecution* request_execution) {
  if (is_done_) {
    LOG_DEBUG("Canceling speculative execution (%p) for request (%p) on host %s",
              static_cast<void*>(request_execution), static_cast<void*>(this),
              request_execution->current_host()
                  ? request_execution->current_host()->address_string().c_str()
                  : "<no current host>");
    return;
  }

  bool is_done = false;
  while (!is_done && request_execution->current_host()) {
    PooledConnection::Ptr connection =
        manager_->find_least_busy(request_execution->current_host()->address());
    if (connection) {
      int32_t result = connection->write(request_execution);

      if (result > 0) {
        is_done = true;
      } else {
        switch (result) {
          case SocketRequest::SOCKET_REQUEST_ERROR_CLOSED:
            // This should never happen, but retry with next host if it does.
            request_execution->next_host();
            break;

          case SocketRequest::SOCKET_REQUEST_ERROR_NO_HANDLER:
            set_error(CASS_ERROR_LIB_WRITE_ERROR,
                      "Socket is not properly configured with a handler");
            is_done = true;
            break;

          case Request::REQUEST_ERROR_NO_AVAILABLE_STREAM_IDS:
            // Retry with next host
            request_execution->next_host();
            break;

          case Request::REQUEST_ERROR_CONNECTION_DRAINING:
            // The connection is draining because its host is shutting down
            // gracefully (CEP-59); retry with next host.
            request_execution->next_host();
            break;

          case Request::REQUEST_ERROR_BATCH_WITH_NAMED_VALUES:
          case Request::REQUEST_ERROR_PARAMETER_UNSET:
          case Request::REQUEST_ERROR_UNSUPPORTED_PROTOCOL:
          case Request::REQUEST_ERROR_NO_DATA_WRITTEN:
            // Already handled with a specific error.
            is_done = true;
            break;

          default:
            set_error(CASS_ERROR_LIB_WRITE_ERROR, "Unspecified write error occurred");
            is_done = true;
            break;
        }
      }
    } else {
      // No connection available on the current host, move to the next host.
      request_execution->next_host();
    }
  }

  if (!request_execution->current_host()) {
    set_error(CASS_ERROR_LIB_NO_HOSTS_AVAILABLE, "All hosts in current policy attempted "
                                                 "and were either unavailable or failed");
  }
}

RequestExecution::RequestExecution(RequestHandler* request_handler)
    : RequestCallback(request_handler->wrapper())
    , request_handler_(request_handler)
    , current_host_(request_handler->next_host(RequestHandler::Protected()))
    , num_retries_(0)
    , start_time_ns_(uv_hrtime()) {}

void RequestExecution::on_execute_next(Timer* timer) { request_handler_->execute(); }

void RequestExecution::on_retry_current_host() { retry_current_host(); }

void RequestExecution::on_retry_next_host() {
  if (current_host_) current_host_->decrement_inflight_requests();
  retry_next_host();
}

void RequestExecution::retry_current_host() {
  // Reset the request so it can be executed again
  set_state(REQUEST_STATE_NEW);

  request_handler_->retry(this, RequestHandler::Protected());
}

void RequestExecution::retry_next_host() {
  next_host();
  retry_current_host();
}

void RequestExecution::on_write(Connection* connection) {
  assert(current_host_ && "Tried to start on a non-existent host");
  current_host_->increment_inflight_requests();
  connection_ = connection;
  if (request()->record_attempted_addresses()) {
    request_handler_->add_attempted_address(current_host_->address(), RequestHandler::Protected());
  }
  request_handler_->start_request(connection->loop(), RequestHandler::Protected());
  if (request()->is_idempotent()) {
    int64_t timeout = request_handler_->next_execution(current_host_, RequestHandler::Protected());
    if (timeout == 0) {
      request_handler_->execute();
    } else if (timeout > 0) {
      schedule_timer_.start(connection->loop(), timeout,
                            bind_callback(&RequestExecution::on_execute_next, this));
    }
  }
}

void RequestExecution::on_set(ResponseMessage* response) {
  assert(connection_ != NULL);
  assert(current_host_ && "Tried to set on a non-existent host");

  current_host_->decrement_inflight_requests();
  Connection* connection = connection_;

  switch (response->opcode()) {
    case CQL_OPCODE_RESULT:
      on_result_response(connection, response);
      break;
    case CQL_OPCODE_ERROR:
      on_error_response(connection, response);
      break;
    default:
      connection->defunct();
      set_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE, "Unexpected response");
      break;
  }
}

void RequestExecution::on_error(CassError code, const String& message) {
  if (current_host_) current_host_->decrement_inflight_requests();
  set_error(code, message);
}

void RequestExecution::notify_result_metadata_changed(const Request* request,
                                                      ResultResponse* result_response) {
  // Attempt to use the per-query keyspace first (v5+/DSEv2+ only) then
  // the keyspace in the result metadata.
  String keyspace;
  if (result_response->protocol_version().supports_set_keyspace() && !request->keyspace().empty()) {
    keyspace = request->keyspace();
  } else {
    keyspace = result_response->quoted_keyspace();
  }

  if (request->opcode() == CQL_OPCODE_EXECUTE && result_response->kind() == CASS_RESULT_KIND_ROWS) {
    const ExecuteRequest* execute = static_cast<const ExecuteRequest*>(request);
    request_handler_->notify_result_metadata_changed(
        execute->prepared()->id(), execute->prepared()->query(), keyspace,
        result_response->new_metadata_id().to_string(), ResultResponse::ConstPtr(result_response),
        RequestHandler::Protected());
  } else if (request->opcode() == CQL_OPCODE_PREPARE &&
             result_response->kind() == CASS_RESULT_KIND_PREPARED) {
    const PrepareRequest* prepare = static_cast<const PrepareRequest*>(request);
    request_handler_->notify_result_metadata_changed(
        result_response->prepared_id().to_string(), prepare->query(), keyspace,
        result_response->result_metadata_id().to_string(),
        ResultResponse::ConstPtr(result_response), RequestHandler::Protected());
  } else {
    assert(false && "Invalid response type for a result metadata change");
  }
}

void RequestExecution::notify_prepared_id_mismatch(const String& expected_id,
                                                   const String& received_id) {
  OStringStream ss;
  ss << "ID mismatch while trying to prepare query (expected ID " << to_hex(expected_id)
     << ", received ID " << to_hex(received_id)
     << "). This prepared statement won't work anymore. This usually happens when you run a "
        "'USE...' query after the statement was prepared.";
  String message = ss.str();
  request_handler_->set_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE, message);
}

void RequestExecution::on_result_response(Connection* connection, ResponseMessage* response) {
  ResultResponse* result = static_cast<ResultResponse*>(response->response_body().get());

  switch (result->kind()) {
    case CASS_RESULT_KIND_ROWS:
      current_host_->update_latency(uv_hrtime() - start_time_ns_);

      // Execute statements with no metadata get their metadata from
      // result_metadata() returned when the statement was prepared.
      if (request()->opcode() == CQL_OPCODE_EXECUTE) {
        if (result->no_metadata()) {
          if (!skip_metadata()) {
            // Caused by a race condition in C* 2.1.0
            on_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE,
                     "Expected metadata but no metadata in response (see CASSANDRA-8054)");
            return;
          }
          result->set_metadata(prepared_metadata_entry()->result()->result_metadata());
        } else if (result->metadata_changed()) {
          notify_result_metadata_changed(request(), result);
        }
      }

      if (!response->response_body()->has_tracing_id() ||
          !request_handler_->wait_for_tracing_data(current_host(), response->response_body())) {
        set_response(response->response_body());
      }
      break;

    case CASS_RESULT_KIND_SCHEMA_CHANGE: {
      if (!request_handler_->wait_for_schema_agreement(current_host(), response->response_body())) {
        set_response(response->response_body());
      }
      break;
    }

    case CASS_RESULT_KIND_SET_KEYSPACE:
      // The response is set after the keyspace is propagated to all threads.
      request_handler_->notify_keyspace_changed(result->quoted_keyspace(), current_host_,
                                                response->response_body());
      break;

    case CASS_RESULT_KIND_PREPARED:
      notify_result_metadata_changed(request(), result);
      if (!request_handler_->prepare_all(current_host(), response->response_body())) {
        set_response(response->response_body());
      }
      break;

    default:
      set_response(response->response_body());
      break;
  }
}

void RequestExecution::on_error_response(Connection* connection, ResponseMessage* response) {
  ErrorResponse* error = static_cast<ErrorResponse*>(response->response_body().get());

  RetryPolicy::RetryDecision decision = RetryPolicy::RetryDecision::return_error();

  switch (error->code()) {
    case CQL_ERROR_READ_TIMEOUT:
      if (retry_policy()) {
        decision = retry_policy()->on_read_timeout(request(), error->consistency(),
                                                   error->received(), error->required(),
                                                   error->data_present() > 0, num_retries_);
      }
      break;

    case CQL_ERROR_WRITE_TIMEOUT:
      if (retry_policy() && request()->is_idempotent()) {
        decision =
            retry_policy()->on_write_timeout(request(), error->consistency(), error->received(),
                                             error->required(), error->write_type(), num_retries_);
      }
      break;

    case CQL_ERROR_UNAVAILABLE:
      if (retry_policy()) {
        decision = retry_policy()->on_unavailable(
            request(), error->consistency(), error->required(), error->received(), num_retries_);
      }
      break;

    case CQL_ERROR_OVERLOADED:
      LOG_WARN("Host %s is overloaded.", connection->address_string().c_str());
      if (retry_policy() && request()->is_idempotent()) {
        decision = retry_policy()->on_request_error(request(), consistency(), error, num_retries_);
      }
      break;

    case CQL_ERROR_SERVER_ERROR:
      LOG_WARN("Received server error '%s' from host %s. Defuncting the connection...",
               error->message().to_string().c_str(), connection->address_string().c_str());
      connection->defunct();
      if (retry_policy() && request()->is_idempotent()) {
        decision = retry_policy()->on_request_error(request(), consistency(), error, num_retries_);
      }
      break;

    case CQL_ERROR_IS_BOOTSTRAPPING:
      LOG_ERROR("Query sent to bootstrapping host %s. Retrying on the next host...",
                connection->address_string().c_str());
      retry_next_host();
      return; // Done

    case CQL_ERROR_UNPREPARED:
      on_error_unprepared(connection, error);
      return; // Done

    default:
      // Return the error response
      break;
  }

  // Process retry decision
  switch (decision.type()) {
    case RetryPolicy::RetryDecision::RETURN_ERROR:
      set_error_with_error_response(
          response->response_body(),
          static_cast<CassError>(CASS_ERROR(CASS_ERROR_SOURCE_SERVER, error->code())),
          error->message().to_string());
      break;

    case RetryPolicy::RetryDecision::RETRY:
      set_retry_consistency(decision.retry_consistency());
      if (decision.retry_current_host()) {
        retry_current_host();
      } else {
        retry_next_host();
      }
      num_retries_++;
      break;

    case RetryPolicy::RetryDecision::IGNORE:
      set_response(Response::Ptr(new ResultResponse()));
      break;
  }
}

void RequestExecution::on_error_unprepared(Connection* connection, ErrorResponse* error) {
  LOG_DEBUG("Unprepared error response returned for request: %s",
            error->message().to_string().c_str());

  String query;
  String id = error->prepared_id().to_string();
  if (request()->opcode() == CQL_OPCODE_EXECUTE) {
    const ExecuteRequest* execute = static_cast<const ExecuteRequest*>(request());
    query = execute->prepared()->query();
  } else if (request()->opcode() == CQL_OPCODE_BATCH) {
    const BatchRequest* batch = static_cast<const BatchRequest*>(request());
    if (!batch->find_prepared_query(id, &query)) {
      set_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE,
                "Unable to find prepared statement in batch statement");
      return;
    }
  } else {
    connection->defunct();
    set_error(CASS_ERROR_LIB_UNEXPECTED_RESPONSE, "Received unprepared error for invalid "
                                                  "request type or invalid prepared id");
    return;
  }

  RequestCallback::Ptr callback(new PrepareCallback(query, id, this));
  if (connection->write_and_flush(callback) < 0) {
    // Try to prepare on the same host but on a different connection
    retry_current_host();
  }
}

void RequestExecution::set_response(const Response::Ptr& response) {
  request_handler_->set_response(current_host_, response);
}

void RequestExecution::set_error(CassError code, const String& message) {
  request_handler_->set_error(current_host_, code, message);
}

void RequestExecution::set_error_with_error_response(const Response::Ptr& error, CassError code,
                                                     const String& message) {
  request_handler_->set_error_with_error_response(current_host_, error, code, message);
}
