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

#ifndef DATASTAX_INTERNAL_REQUEST_CALLBACK_HPP
#define DATASTAX_INTERNAL_REQUEST_CALLBACK_HPP

#include "buffer.hpp"
#include "cassandra.h"
#include "constants.hpp"
#include "dense_hash_map.hpp"
#include "list.hpp"
#include "prepared.hpp"
#include "request.hpp"
#include "response.hpp"
#include "scoped_ptr.hpp"
#include "socket.hpp"
#include "string.hpp"
#include "timer.hpp"
#include "timestamp_generator.hpp"
#include "utils.hpp"

#include <uv.h>

namespace datastax { namespace internal { namespace core {

class Config;
class Connection;
class ExecutionProfile;
class Metrics;
class Pool;
class PooledConnection;
class PreparedMetadata;
class ResponseMessage;
class ResultResponse;

typedef Vector<uv_buf_t> UvBufVec;

/**
 * A wrapper class for keeping a request's state grouped together with the
 * request object. This is necessary because a request object is immutable
 * when it's being executed.
 */
class RequestWrapper {
public:
  RequestWrapper(const Request::ConstPtr& request,
                 uint64_t request_timeout_ms = CASS_DEFAULT_REQUEST_TIMEOUT_MS)
      : request_(request)
      , consistency_(CASS_DEFAULT_CONSISTENCY)
      , serial_consistency_(CASS_DEFAULT_SERIAL_CONSISTENCY)
      , request_timeout_ms_(request_timeout_ms)
      , timestamp_(CASS_INT64_MIN) {}

  void set_prepared_metadata(const PreparedMetadata::Entry::Ptr& entry);

  void init(const ExecutionProfile& profile, TimestampGenerator* timestamp_generator);

  const Request::ConstPtr& request() const { return request_; }

  CassConsistency consistency() const {
    if (request()->consistency() != CASS_CONSISTENCY_UNKNOWN) {
      return request()->consistency();
    }
    return consistency_;
  }

  CassConsistency serial_consistency() const {
    if (request()->serial_consistency() != CASS_CONSISTENCY_UNKNOWN) {
      return request()->serial_consistency();
    }
    return serial_consistency_;
  }

  uint64_t request_timeout_ms() const {
    if (request()->request_timeout_ms() != CASS_UINT64_MAX) {
      return request()->request_timeout_ms();
    }
    return request_timeout_ms_;
  }

  int64_t timestamp() const {
    if (request()->timestamp() != CASS_INT64_MIN) {
      return request()->timestamp();
    }
    return timestamp_;
  }

  const RetryPolicy::Ptr& retry_policy() const {
    if (request()->retry_policy()) {
      return request()->retry_policy();
    }
    return retry_policy_;
  }

  const PreparedMetadata::Entry::Ptr& prepared_metadata_entry() const {
    return prepared_metadata_entry_;
  }

private:
  Request::ConstPtr request_;
  CassConsistency consistency_;
  CassConsistency serial_consistency_;
  uint64_t request_timeout_ms_;
  int64_t timestamp_;
  RetryPolicy::Ptr retry_policy_;
  PreparedMetadata::Entry::Ptr prepared_metadata_entry_;
};

class RequestCallback
    : public RefCounted<RequestCallback>
    , public SocketRequest {
public:
  typedef SharedRefPtr<RequestCallback> Ptr;
  typedef Vector<Ptr> Vec;

  enum State {
    REQUEST_STATE_NEW,
    REQUEST_STATE_WRITING,
    REQUEST_STATE_READING,
    REQUEST_STATE_READ_BEFORE_WRITE,
    REQUEST_STATE_FINISHED
  };

  RequestCallback(const RequestWrapper& wrapper)
      : wrapper_(wrapper)
      , framed_(false)
      , stream_(-1)
      , state_(REQUEST_STATE_NEW)
      , retry_consistency_(CASS_CONSISTENCY_UNKNOWN) {}

  virtual ~RequestCallback() {}

  void notify_write(Connection* connection, int stream);

public:
  // Called to retry a request on a different connection
  virtual void on_retry_current_host() = 0;
  virtual void on_retry_next_host() = 0;

protected:
  // Called right before a request is written to a connection
  virtual void on_write(Connection* connection) = 0;

public:
  // Called to finish a request
  virtual void on_set(ResponseMessage* response) = 0;
  virtual void on_error(CassError code, const String& message) = 0;

public:
  const Request* request() const { return wrapper_.request().get(); }

  bool skip_metadata() const;

  CassConsistency consistency() {
    // The retry consistency takes the highest priority
    if (retry_consistency_ != CASS_CONSISTENCY_UNKNOWN) {
      return retry_consistency_;
    }
    return wrapper_.consistency();
  }

  CassConsistency serial_consistency() { return wrapper_.serial_consistency(); }

  uint64_t request_timeout_ms() { return wrapper_.request_timeout_ms(); }

  int64_t timestamp() { return wrapper_.timestamp(); }

  /** Protocol v5 only; `CASS_INT32_MIN` means unset. */
  int32_t now_in_seconds() { return request()->now_in_seconds(); }

  const RetryPolicy::Ptr& retry_policy() { return wrapper_.retry_policy(); }

  const PreparedMetadata::Entry::Ptr& prepared_metadata_entry() const {
    return wrapper_.prepared_metadata_entry();
  }

  void set_retry_consistency(CassConsistency cl) { retry_consistency_ = cl; }

  int stream() const { return stream_; }

  State state() const { return state_; }
  void set_state(State next_state);

  const char* state_string() const;

  ResponseMessage* read_before_write_response() const { return read_before_write_response_.get(); }

  void set_read_before_write_response(ResponseMessage* response) {
    read_before_write_response_.reset(response);
  }

private:
  virtual int32_t encode(BufferVec* bufs);
  virtual void on_close();

  /**
   * Whether this request must be wrapped in the protocol v5 frame format.
   *
   * This is captured when the write is notified because framing only applies
   * once the connection's initial handshake has completed.
   */
  bool framed() const { return framed_; }

private:
  const RequestWrapper wrapper_;
  ProtocolVersion protocol_version_;
  bool framed_;
  int stream_;
  State state_;
  CassConsistency retry_consistency_;
  ScopedPtr<ResponseMessage> read_before_write_response_;

private:
  DISALLOW_COPY_AND_ASSIGN(RequestCallback);
};

class SimpleRequestCallback : public RequestCallback {
public:
  SimpleRequestCallback(const String& query,
                        uint64_t request_timeout_ms = CASS_DEFAULT_REQUEST_TIMEOUT_MS);

  SimpleRequestCallback(const Request::ConstPtr& request,
                        uint64_t request_timeout_ms = CASS_DEFAULT_REQUEST_TIMEOUT_MS)
      : RequestCallback(RequestWrapper(request, request_timeout_ms)) {}

  SimpleRequestCallback(const RequestWrapper& wrapper)
      : RequestCallback(wrapper) {}

protected:
  virtual void on_internal_write(Connection* connection) {}
  virtual void on_internal_set(ResponseMessage* response) = 0;
  virtual void on_internal_error(CassError code, const String& message) = 0;
  virtual void on_internal_timeout() = 0;

private:
  virtual void on_retry_current_host();
  virtual void on_retry_next_host();

  virtual void on_write(Connection* connection);

protected:
  virtual void on_set(ResponseMessage* response);
  virtual void on_error(CassError code, const String& message);

private:
  void on_timeout(Timer* timer);

private:
  Timer timer_;
};

/**
 * A request callback that chains multiple requests together as a single
 * request.
 */
class ChainedRequestCallback : public SimpleRequestCallback {
public:
  typedef SharedRefPtr<ChainedRequestCallback> Ptr;

  class Map : public DenseHashMap<String, Response::Ptr> {
  public:
    Map() { set_empty_key(String()); }
  };

  /**
   * Constructor for a simple query.
   *
   * @param key A key to map the response to the query.
   * @param query The actual query to run.
   * @param chain A request that's chained to this request. Don't use directly
   * instead use the chain() method.
   */
  ChainedRequestCallback(const String& key, const String& query, const Ptr& chain = Ptr());

  /**
   * Constructor for any type of request.
   *
   * @param key A key to map the response to the request.
   * @param request The actual request to run.
   * @param chain A request that's chained to this request. Don't use directly
   * instead use the chain() method.
   */
  ChainedRequestCallback(const String& key, const Request::ConstPtr& request,
                         const Ptr& chain = Ptr());

  /**
   * Add a chained query to this request callback.
   *
   * Note: The last request in the chain must be executed for all prior chained
   * requests to execute properly.
   *
   * @param key A key to map the response to the query.
   * @param query The actual query to chain.
   * @return Returns the new chained request callback so that another request
   * can be chained. e.g. callback->chain(...)->chain(...)
   */
  ChainedRequestCallback::Ptr chain(const String& key, const String& query);

  /**
   * Add a chained request to this request callback.
   *
   * Note: The last request in the chain must be executed for all prior chained
   * requests to execute properly.
   *
   * @param key A key to map the response to the request.
   * @param request The actual request to run.
   * @return Returns the new chained request callback so that another request
   * can be chained. e.g. callback->chain(...)->chain(...)
   */
  ChainedRequestCallback::Ptr chain(const String& key, const Request::ConstPtr& request);

  /**
   * The responses for the chained callbacks.
   *
   * @return A map of the responses by key.
   */
  const Map& responses() const { return responses_; }

  /**
   * Get the result response for a key.
   *
   * @param The key the query/request was created with.
   * @return  The result response for the chained request, null if not a
   * result response or if the key doesn't exist.
   */
  ResultResponse::Ptr result(const String& key) const;

protected:
  /**
   * A callback for when the chained request is written to a connection.
   *
   * @param connection The connection the callback was written to.
   */
  virtual void on_chain_write(Connection* connection) {}

  /**
   * A callback for when all responses have been received.
   */
  virtual void on_chain_set() {}

  /**
   * A callback for when an error occurs. A single error causes the whole
   * chain to fail.
   *
   * @param code The error code.
   * @param message The error message.
   */
  virtual void on_chain_error(CassError code, const String& message) {}

  /**
   * A callback for a request timeout. A single timeout causes the whole
   * chain to fail.
   */
  virtual void on_chain_timeout() {}

private:
  virtual void on_internal_write(Connection* connection);
  virtual void on_internal_set(ResponseMessage* response);
  virtual void on_internal_error(CassError code, const String& message);
  virtual void on_internal_timeout();

private:
  void set_chain_responses(Map& responses);

  bool is_finished() const;
  void maybe_finish();

private:
  const ChainedRequestCallback::Ptr chain_;
  bool has_pending_;
  bool has_error_or_timeout_;
  String key_;
  Response::Ptr response_;
  Map responses_;
};

}}} // namespace datastax::internal::core

#endif
