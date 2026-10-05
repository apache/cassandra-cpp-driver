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

#include "request_callback.hpp"

#include "connection.hpp"
#include "constants.hpp"
#include "execute_request.hpp"
#include "execution_profile.hpp"
#include "frame.hpp"
#include "logger.hpp"
#include "metrics.hpp"
#include "query_request.hpp"
#include "request.hpp"
#include "result_response.hpp"
#include "serialization.hpp"

using namespace datastax;
using namespace datastax::internal::core;

void RequestWrapper::set_prepared_metadata(const PreparedMetadata::Entry::Ptr& entry) {
  prepared_metadata_entry_ = entry;
}

void RequestWrapper::init(const ExecutionProfile& profile,
                          TimestampGenerator* timestamp_generator) {
  consistency_ = profile.consistency();
  serial_consistency_ = profile.serial_consistency();
  request_timeout_ms_ = profile.request_timeout_ms();
  timestamp_ = timestamp_generator->next();
  retry_policy_ = profile.retry_policy();
}

void RequestCallback::notify_write(Connection* connection, int stream) {
  protocol_version_ = connection->protocol_version();
  framed_ = connection->use_frame_codec();
  stream_ = stream;
  on_write(connection);
}

bool RequestCallback::skip_metadata() const {
  // Skip the metadata if this an execute request and we have an entry cached
  return request()->opcode() == CQL_OPCODE_EXECUTE && prepared_metadata_entry() &&
         prepared_metadata_entry()->result()->result_metadata();
}

int32_t RequestCallback::encode(BufferVec* bufs) {
  const ProtocolVersion version = protocol_version_;
  if (version < ProtocolVersion::lowest_supported()) {
    on_error(CASS_ERROR_LIB_MESSAGE_ENCODE, "Operation unsupported by this protocol version");
    return Request::REQUEST_ERROR_UNSUPPORTED_PROTOCOL;
  }

  size_t index = bufs->size();
  bufs->push_back(Buffer()); // Placeholder

  const Request* req = request();
  int flags = req->flags();
  int32_t length = 0;

  if (version.is_beta()) {
    flags |= CASS_FLAG_BETA;
  }

  if (version >= CASS_PROTOCOL_VERSION_V4 && req->has_custom_payload()) {
    flags |= CASS_FLAG_CUSTOM_PAYLOAD;
    length += req->encode_custom_payload(bufs);
  }

  int32_t result = req->encode(version, this, bufs);
  if (result < 0) return result;
  length += result;

  const size_t header_size = CASS_HEADER_SIZE_V3;

  Buffer buf(header_size);
  size_t pos = 0;
  pos = buf.encode_byte(pos, static_cast<uint8_t>(version.value()));
  pos = buf.encode_byte(pos, static_cast<uint8_t>(flags));

  pos = buf.encode_int16(pos, static_cast<int16_t>(stream_));

  pos = buf.encode_byte(pos, req->opcode());
  buf.encode_int32(pos, length);
  (*bufs)[index] = buf;

  const size_t envelope_size = length + header_size;

  if (!framed_) {
    return static_cast<int32_t>(envelope_size);
  }

  if (envelope_size <= FrameCodec::MAX_PAYLOAD_LENGTH) {
    // Common case: one self contained frame. Splice the frame header in ahead of
    // the envelope and append the CRC32 trailer. The payload buffers are
    // referenced directly rather than copied.
    Buffer frame_header(FrameCodec::UNCOMPRESSED_HEADER_SIZE);
    FrameCodec::encode_header(frame_header.data(), envelope_size, true);
    bufs->insert(bufs->begin() + index, frame_header);

    Buffer trailer(FrameCodec::TRAILER_SIZE);
    FrameCodec::encode_trailer(trailer.data(),
                               FrameCodec::payload_crc(*bufs, index + 1, bufs->size()));
    bufs->push_back(trailer);

    return static_cast<int32_t>(envelope_size + FrameCodec::OVERHEAD);
  }

  // Rare case: the envelope is too large for a single frame and has to be
  // split across several, which requires reworking the buffer list.
  BufferVec framed;
  const int32_t framed_size = FrameCodec::encode(*bufs, index, bufs->size(), &framed);
  bufs->resize(index);
  bufs->insert(bufs->end(), framed.begin(), framed.end());
  return framed_size;
}

void RequestCallback::on_close() {
  switch (state()) {
    case RequestCallback::REQUEST_STATE_NEW:
    case RequestCallback::REQUEST_STATE_FINISHED:
      assert(false && "Request state is invalid in cleanup");
      break;

    case RequestCallback::REQUEST_STATE_READ_BEFORE_WRITE:
      set_state(RequestCallback::REQUEST_STATE_FINISHED);
      // Use the response saved in the read callback
      on_set(read_before_write_response());
      break;

    case RequestCallback::REQUEST_STATE_WRITING:
    case RequestCallback::REQUEST_STATE_READING:
      set_state(RequestCallback::REQUEST_STATE_FINISHED);
      if (request()->is_idempotent()) {
        on_retry_next_host();
      } else {
        on_error(CASS_ERROR_LIB_REQUEST_TIMED_OUT, "Request timed out");
      }
      break;
  }
}

void RequestCallback::set_state(RequestCallback::State next_state) {
  switch (state_) {
    case REQUEST_STATE_NEW:
      if (next_state == REQUEST_STATE_NEW || next_state == REQUEST_STATE_WRITING) {
        state_ = next_state;
      } else {
        assert(false && "Invalid request state after new");
      }
      break;

    case REQUEST_STATE_WRITING:
      if (next_state == REQUEST_STATE_READING || next_state == REQUEST_STATE_READ_BEFORE_WRITE ||
          next_state == REQUEST_STATE_FINISHED) {
        state_ = next_state;
      } else {
        assert(false && "Invalid request state after writing");
      }
      break;

    case REQUEST_STATE_READING:
      if (next_state == REQUEST_STATE_FINISHED) {
        state_ = next_state;
      } else {
        assert(false && "Invalid request state after reading");
      }
      break;

    case REQUEST_STATE_READ_BEFORE_WRITE:
      if (next_state == REQUEST_STATE_FINISHED) {
        state_ = next_state;
      } else {
        assert(false && "Invalid request state after read before write");
      }
      break;

    case REQUEST_STATE_FINISHED:
      if (next_state == REQUEST_STATE_NEW) {
        state_ = next_state;
      } else {
        assert(false && "Invalid request state after finished");
      }
      break;

    default:
      assert(false && "Invalid request state");
      break;
  }
}

const char* RequestCallback::state_string() const {
  switch (state_) {
    case REQUEST_STATE_NEW:
      return "NEW";
    case REQUEST_STATE_WRITING:
      return "WRITING";
    case REQUEST_STATE_READING:
      return "READING";
    case REQUEST_STATE_READ_BEFORE_WRITE:
      return "READ_BEFORE_WRITE";
    case REQUEST_STATE_FINISHED:
      return "FINISHED";
  }
  return "INVALID";
}

SimpleRequestCallback::SimpleRequestCallback(const String& query, uint64_t request_timeout_ms)
    : RequestCallback(
          RequestWrapper(Request::ConstPtr(new QueryRequest(query)), request_timeout_ms)) {}

void SimpleRequestCallback::on_write(Connection* connection) {
  uint64_t request_timeout_ms = this->request_timeout_ms();
  if (request_timeout_ms > 0) { // 0 means no timeout
    timer_.start(connection->loop(), request_timeout_ms,
                 bind_callback(&SimpleRequestCallback::on_timeout, this));
  }
  on_internal_write(connection);
}

void SimpleRequestCallback::on_set(ResponseMessage* response) {
  timer_.stop();
  on_internal_set(response);
}

void SimpleRequestCallback::on_error(CassError code, const String& message) {
  timer_.stop();
  on_internal_error(code, message);
}

void SimpleRequestCallback::on_retry_current_host() {
  timer_.stop();
  on_internal_timeout(); // Retries are unhandled so timeout
}

void SimpleRequestCallback::on_retry_next_host() {
  on_retry_current_host(); // Same as retry current (timeout)
}

void SimpleRequestCallback::on_timeout(Timer* timer) {
  on_internal_timeout();
  LOG_DEBUG("Request timed out (internal)");
}

ChainedRequestCallback::ChainedRequestCallback(const String& key, const String& query,
                                               const Ptr& chain)
    : SimpleRequestCallback(query)
    , chain_(chain)
    , has_pending_(false)
    , has_error_or_timeout_(false)
    , key_(key) {}

ChainedRequestCallback::ChainedRequestCallback(const String& key, const Request::ConstPtr& request,
                                               const Ptr& chain)
    : SimpleRequestCallback(request)
    , chain_(chain)
    , has_pending_(false)
    , has_error_or_timeout_(false)
    , key_(key) {}

ChainedRequestCallback::Ptr ChainedRequestCallback::chain(const String& key, const String& query) {
  has_pending_ = true;
  return ChainedRequestCallback::Ptr(new ChainedRequestCallback(key, query, Ptr(this)));
}

ChainedRequestCallback::Ptr ChainedRequestCallback::chain(const String& key,
                                                          const Request::ConstPtr& request) {
  has_pending_ = true;
  return ChainedRequestCallback::Ptr(new ChainedRequestCallback(key, request, Ptr(this)));
}

ResultResponse::Ptr ChainedRequestCallback::result(const String& key) const {
  Map::const_iterator it = responses_.find(key);
  if (it == responses_.end() || it->second->opcode() != CQL_OPCODE_RESULT) {
    return ResultResponse::Ptr();
  }
  return it->second;
}

void ChainedRequestCallback::on_internal_write(Connection* connection) {
  if (chain_) {
    if (connection->write_and_flush(chain_) < 0) {
      on_error(CASS_ERROR_LIB_NO_STREAMS,
               "No streams available when attempting to write chained request");
    }
  }
  on_chain_write(connection);
}

void ChainedRequestCallback::on_internal_set(ResponseMessage* response) {
  response_ = response->response_body();
  maybe_finish();
}

void ChainedRequestCallback::on_internal_error(CassError code, const String& message) {
  if (!has_error_or_timeout_) {
    has_error_or_timeout_ = true;
    if (chain_) {
      chain_->on_error(code, message);
    } else {
      on_chain_error(code, message);
    }
  }
}

void ChainedRequestCallback::on_internal_timeout() {
  if (!has_error_or_timeout_) {
    has_error_or_timeout_ = true;
    if (chain_) {
      chain_->on_internal_timeout();
    } else {
      on_chain_timeout();
    }
  }
}

void ChainedRequestCallback::set_chain_responses(Map& responses) {
  responses_.swap(responses);
  maybe_finish();
}

bool ChainedRequestCallback::is_finished() const {
  return response_ && !has_error_or_timeout_ &&
         ((has_pending_ && !responses_.empty()) || !has_pending_);
}

void ChainedRequestCallback::maybe_finish() {
  if (is_finished()) {
    if (response_->opcode() == CQL_OPCODE_ERROR) {
      if (request()->opcode() == CQL_OPCODE_QUERY) {
        LOG_ERROR("Chained error response %s for query \"%s\"",
                  static_cast<const ErrorResponse*>(response_.get())->error_message().c_str(),
                  static_cast<const QueryRequest*>(request())->query().c_str());
      } else {
        LOG_ERROR("Chained error response %s",
                  static_cast<const ErrorResponse*>(response_.get())->error_message().c_str());
      }
    }
    responses_[key_] = response_;
    if (chain_) {
      chain_->set_chain_responses(responses_);
    } else {
      on_chain_set();
    }
  }
}
