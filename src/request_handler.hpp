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

#ifndef DATASTAX_INTERNAL_REQUEST_HANDLER_HPP
#define DATASTAX_INTERNAL_REQUEST_HANDLER_HPP

#include "constants.hpp"
#include "error_response.hpp"
#include "future.hpp"
#include "host.hpp"
#include "load_balancing.hpp"
#include "metadata.hpp"
#include "prepare_request.hpp"
#include "request.hpp"
#include "request_callback.hpp"
#include "response.hpp"
#include "result_response.hpp"
#include "retry_policy.hpp"
#include "scoped_ptr.hpp"
#include "small_vector.hpp"
#include "speculative_execution.hpp"
#include "string.hpp"
#include "timestamp_generator.hpp"

#include <uv.h>

namespace datastax { namespace internal { namespace core {

class Config;
class Connection;
class ConnectionPoolManager;
class Pool;
class ExecutionProfile;
class Timer;
class TokenMap;

struct RequestTry {
  RequestTry()
      : error(CASS_OK)
      , latency(0) {}

  RequestTry(const Address& address, uint64_t latency)
      : address(address)
      , error(CASS_OK)
      , latency(latency / (1000 * 1000)) {} // To milliseconds

  RequestTry(const Address& address, CassError error)
      : address(address)
      , error(error)
      , latency(0) {}

  Address address;
  CassError error;
  uint64_t latency;
};

typedef SmallVector<RequestTry, 2> RequestTryVec;

class ResponseFuture : public Future {
public:
  typedef SharedRefPtr<ResponseFuture> Ptr;

  ResponseFuture()
      : Future(FUTURE_TYPE_RESPONSE) {}

  ResponseFuture(const Metadata::SchemaSnapshot& schema_metadata)
      : Future(FUTURE_TYPE_RESPONSE)
      , schema_metadata(new Metadata::SchemaSnapshot(schema_metadata)) {}

  bool set_response(Address address, const Response::Ptr& response) {
    ScopedMutex lock(&mutex_);
    /* CASSCPP-6 We may still be able to get the mutex even if another thread is in internal_set() but in that case
       future.state should have moved to at least STARTED... so we only proceed here if the future is still in an UNSTARTED
       state.  */
    if (is_unstarted()) {
      address_ = address;
      response_ = response;
      internal_set(lock);
      return true;
    }
    return false;
  }

  const Response::Ptr& response() {
    ScopedMutex lock(&mutex_);
    internal_wait(lock);
    return response_;
  }

  bool set_error_with_address(Address address, CassError code, const String& message) {
    ScopedMutex lock(&mutex_);
    if (is_unstarted()) {
      address_ = address;
      internal_set_error(code, message, lock);
      return true;
    }
    return false;
  }

  bool set_error_with_response(Address address, const Response::Ptr& response, CassError code,
                               const String& message) {
    ScopedMutex lock(&mutex_);
    if (is_unstarted()) {
      address_ = address;
      response_ = response;
      internal_set_error(code, message, lock);
      return true;
    }
    return false;
  }

  const Address& address() {
    ScopedMutex lock(&mutex_);
    internal_wait(lock);
    return address_;
  }

  // Currently, used for testing only, but it could be exposed in the future.
  AddressVec attempted_addresses() {
    ScopedMutex lock(&mutex_);
    internal_wait(lock);
    return attempted_addresses_;
  }

  PrepareRequest::ConstPtr prepare_request;
  ScopedPtr<Metadata::SchemaSnapshot> schema_metadata;

private:
  friend class RequestHandler;

  void add_attempted_address(const Address& address) {
    ScopedMutex lock(&mutex_);
    attempted_addresses_.push_back(address);
  }

private:
  Address address_;
  Response::Ptr response_;
  AddressVec attempted_addresses_;
};

class RequestExecution;
class RequestListener;

class RequestHandler : public RefCounted<RequestHandler> {
  friend class Memory;

public:
  typedef SharedRefPtr<RequestHandler> Ptr;

  RequestHandler(const Request::ConstPtr& request, const ResponseFuture::Ptr& future,
                 Metrics* metrics = NULL);
  ~RequestHandler();

  void set_prepared_metadata(const PreparedMetadata::Entry::Ptr& entry);

  void init(const ExecutionProfile& profile, ConnectionPoolManager* manager,
            const TokenMap* token_map, TimestampGenerator* timestamp_generator,
            RequestListener* listener);

  void execute();

  const RequestWrapper& wrapper() const { return wrapper_; }
  const Request* request() const { return wrapper_.request().get(); }
  CassConsistency consistency() const { return wrapper_.consistency(); }

public:
  class Protected {
    friend class RequestExecution;
    Protected() {}
    Protected(Protected const&) {}
  };

  void retry(RequestExecution* request_execution, Protected);

  Host::Ptr next_host(Protected);
  int64_t next_execution(const Host::Ptr& current_host, Protected);

  void start_request(uv_loop_t* loop, Protected);

  void add_attempted_address(const Address& address, Protected);

  void notify_result_metadata_changed(const String& prepared_id, const String& query,
                                      const String& keyspace, const String& result_metadata_id,
                                      const ResultResponse::ConstPtr& result_response, Protected);

  void notify_keyspace_changed(const String& keyspace, const Host::Ptr& current_host,
                               const Response::Ptr& response);

  bool wait_for_tracing_data(const Host::Ptr& current_host, const Response::Ptr& response);
  bool wait_for_schema_agreement(const Host::Ptr& current_host, const Response::Ptr& response);

  bool prepare_all(const Host::Ptr& current_host, const Response::Ptr& response);

  void set_response(const Host::Ptr& host, const Response::Ptr& response);
  void set_error(CassError code, const String& message);
  void set_error(const Host::Ptr& host, CassError code, const String& message);
  void set_error_with_error_response(const Host::Ptr& host, const Response::Ptr& error,
                                     CassError code, const String& message);

  void stop_timer();

private:
  void on_timeout(Timer* timer);

private:
  void stop_request();
  void internal_retry(RequestExecution* request_execution);

private:
  RequestWrapper wrapper_;
  SharedRefPtr<ResponseFuture> future_;

  bool is_done_;
  int running_executions_;

  ScopedPtr<QueryPlan> query_plan_;
  ScopedPtr<SpeculativeExecutionPlan> execution_plan_;
  Timer timer_;

  const uint64_t start_time_ns_;
  RequestListener* listener_;
  ConnectionPoolManager* manager_;

  Metrics* const metrics_;

  RequestTryVec request_tries_;
};

class KeyspaceChangedResponse {
public:
  KeyspaceChangedResponse(const RequestHandler::Ptr& request_handler, const Host::Ptr& current_host,
                          const Response::Ptr& response)
      : request_handler_(request_handler)
      , current_host_(current_host)
      , response_(response) {}

  void set_response() { request_handler_->set_response(current_host_, response_); }

private:
  RequestHandler::Ptr request_handler_;
  Host::Ptr current_host_;
  Response::Ptr response_;
};

class PreparedMetadataListener {
public:
  virtual ~PreparedMetadataListener() {}

  virtual void on_prepared_metadata_changed(const String& id,
                                            const PreparedMetadata::Entry::Ptr& entry) = 0;
};

class RequestListener : public PreparedMetadataListener {
public:
  virtual ~RequestListener() {}

  /**
   * A callback called when the keyspace has changed.
   *
   * @param keyspace The new keyspace.
   * @param response The response for the keyspace change. `set_response()`
   * must be called when the callback is done processing the keyspace change.
   */
  virtual void on_keyspace_changed(const String& keyspace, KeyspaceChangedResponse response) = 0;

  virtual bool on_wait_for_tracing_data(const RequestHandler::Ptr& request_handler,
                                        const Host::Ptr& current_host,
                                        const Response::Ptr& response) = 0;

  virtual bool on_wait_for_schema_agreement(const RequestHandler::Ptr& request_handler,
                                            const Host::Ptr& current_host,
                                            const Response::Ptr& response) = 0;

  virtual bool on_prepare_all(const RequestHandler::Ptr& request_handler,
                              const Host::Ptr& current_host, const Response::Ptr& response) = 0;

  virtual void on_done() = 0;
};

class RequestExecution : public RequestCallback {
public:
  typedef SharedRefPtr<RequestExecution> Ptr;

  RequestExecution(RequestHandler* request_handler);

  const Host::Ptr& current_host() const { return current_host_; }
  void next_host() { current_host_ = request_handler_->next_host(RequestHandler::Protected()); }

  void notify_result_metadata_changed(const Request* request, ResultResponse* result_response);
  void notify_prepared_id_mismatch(const String& expected_id, const String& received_id);

  virtual void on_retry_current_host();
  virtual void on_retry_next_host();

private:
  void on_execute_next(Timer* timer);

  void retry_current_host();
  void retry_next_host();

  virtual void on_write(Connection* connection);

  virtual void on_set(ResponseMessage* response);
  virtual void on_error(CassError code, const String& message);

  void on_result_response(Connection* connection, ResponseMessage* response);
  void on_error_response(Connection* connection, ResponseMessage* response);
  void on_error_unprepared(Connection* connection, ErrorResponse* error);

private:
  void set_response(const Response::Ptr& response);
  void set_error(CassError code, const String& message);
  void set_error_with_error_response(const Response::Ptr& error, CassError code,
                                     const String& message);

private:
  RequestHandler::Ptr request_handler_;
  Host::Ptr current_host_;
  Connection* connection_;
  Timer schedule_timer_;
  int num_retries_;
  const uint64_t start_time_ns_;
};

}}} // namespace datastax::internal::core

#endif
