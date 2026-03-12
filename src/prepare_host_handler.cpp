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

#include "prepare_host_handler.hpp"

#include "prepare_request.hpp"
#include "protocol.hpp"
#include "query_request.hpp"
#include "stream_manager.hpp"

#include <algorithm>

using namespace datastax;
using namespace datastax::internal::core;

struct CompareEntryKeyspace {
  bool operator()(const PreparedMetadata::Entry::Ptr& lhs,
                  const PreparedMetadata::Entry::Ptr& rhs) const {
    return lhs->keyspace() < rhs->keyspace();
  }
};

PrepareHostHandler::PrepareHostHandler(
    const Host::Ptr& host, const PreparedMetadata::Entry::Vec& prepared_metadata_entries,
    const Callback& callback, ProtocolVersion protocol_version, unsigned max_requests_per_flush)
    : host_(host)
    , protocol_version_(protocol_version)
    , callback_(callback)
    , connection_(NULL)
    , prepares_outstanding_(0)
    , max_prepares_outstanding_(CASS_MAX_STREAMS)
    , prepared_metadata_entries_(prepared_metadata_entries) {

  // Sort by keyspace to minimize the number of times the keyspace
  // needs to be changed.
  std::sort(prepared_metadata_entries_.begin(), prepared_metadata_entries_.end(),
            CompareEntryKeyspace());

  current_entry_it_ = prepared_metadata_entries_.begin();
}

void PrepareHostHandler::prepare(uv_loop_t* loop, const ConnectionSettings& settings) {
  if (prepared_metadata_entries_.empty()) {
    callback_(this);
    return;
  }

  inc_ref(); // Reference for the event loop

  Connector::Ptr connector(new Connector(host_, protocol_version_,
                                         bind_callback(&PrepareHostHandler::on_connect, this)));

  connector->with_settings(settings)->with_listener(this)->connect(loop);
}

void PrepareHostHandler::on_close(Connection* connection) {
  dec_ref(); // The event loop is done with this handler
}

void PrepareHostHandler::on_connect(Connector* connector) {
  if (connector->is_ok()) {
    connection_ = connector->release_connection().get();
    prepare_next();
  } else {
    callback_(this);
    dec_ref(); // The event loop is done with this handler
  }
}

// This is the main loop for preparing statements. It's called after each
// request successfully completes, either setting the keyspace or preparing
// a statement. It attempts to group prepare requests into a single batch as
// long as the keyspace is the same and the number of outstanding requests is
// under the maximum.
void PrepareHostHandler::prepare_next() {
  // Finish current prepares before continuing
  if (--prepares_outstanding_ > 0) {
    return;
  }

  // Check to see if we're done
  if (is_done()) {
    close();
    return;
  }

  prepares_outstanding_ = 0;

  // Write prepare requests until there's no more left, the keyspace changes,
  // or the maximum number of outstanding prepares is reached.
  while (!is_done() && check_and_set_keyspace() &&
         prepares_outstanding_ < max_prepares_outstanding_) {
    const String& query((*current_entry_it_)->query());
    PrepareRequest::Ptr prepare_request(new PrepareRequest(query));

    // Set the keyspace in case per request keyspaces are supported
    prepare_request->set_keyspace(current_keyspace_);

    PrepareCallback::Ptr callback(new PrepareCallback(prepare_request, Ptr(this)));
    if (connection_->write(callback) < 0) {
      LOG_WARN("Failed to write prepare request while preparing all queries on host %s",
               host_->address_string().c_str());
      close();
      return;
    }

    prepares_outstanding_++;
    current_entry_it_++;
  }

  connection_->flush();
}

bool PrepareHostHandler::check_and_set_keyspace() {
  if (protocol_version_.supports_set_keyspace()) {
    return true;
  }

  const String& keyspace((*current_entry_it_)->keyspace());

  if (keyspace != current_keyspace_) {
    PrepareCallback::Ptr callback(new SetKeyspaceCallback(keyspace, Ptr(this)));
    if (connection_->write_and_flush(callback) < 0) {
      LOG_WARN("Failed to write \"USE\" keyspace request while preparing all queries on host %s",
               host_->address_string().c_str());
      close();
      return false;
    }
    current_keyspace_ = keyspace;
    return false;
  }

  return true;
}

bool PrepareHostHandler::is_done() const {
  return current_entry_it_ == prepared_metadata_entries_.end();
}

void PrepareHostHandler::close() { connection_->close(); }

PrepareHostHandler::PrepareCallback::PrepareCallback(
    const PrepareRequest::ConstPtr& prepare_request, const PrepareHostHandler::Ptr& handler)
    : SimpleRequestCallback(prepare_request)
    , handler_(handler) {}

void PrepareHostHandler::PrepareCallback::on_internal_set(ResponseMessage* response) {
  LOG_DEBUG("Successfully prepared query \"%s\" on host %s while preparing all queries",
            static_cast<const PrepareRequest*>(request())->query().c_str(),
            handler_->host()->address_string().c_str());
  handler_->prepare_next();
}

void PrepareHostHandler::PrepareCallback::on_internal_error(CassError code, const String& message) {
  LOG_WARN("Prepare request failed on host %s while attempting to prepare all queries: %s (%s)",
           handler_->host_->address_string().c_str(), message.c_str(), cass_error_desc(code));
  handler_->close();
}

void PrepareHostHandler::PrepareCallback::on_internal_timeout() {
  LOG_WARN("Prepare request timed out on host %s while attempting to prepare all queries",
           handler_->host_->address_string().c_str());
  handler_->close();
}

PrepareHostHandler::SetKeyspaceCallback::SetKeyspaceCallback(const String& keyspace,
                                                             const PrepareHostHandler::Ptr& handler)
    : SimpleRequestCallback(Request::ConstPtr(new QueryRequest("USE " + keyspace)))
    , handler_(handler) {}

void PrepareHostHandler::SetKeyspaceCallback::on_internal_set(ResponseMessage* response) {
  LOG_TRACE("Successfully set keyspace to \"%s\" on host %s while preparing all queries",
            handler_->current_keyspace_.c_str(), handler_->host()->address_string().c_str());
  handler_->prepare_next();
}

void PrepareHostHandler::SetKeyspaceCallback::on_internal_error(CassError code,
                                                                const String& message) {
  LOG_WARN(
      "\"USE\" keyspace request failed on host %s while attempting to prepare all queries: %s (%s)",
      handler_->host_->address_string().c_str(), message.c_str(), cass_error_desc(code));
  handler_->close();
}

void PrepareHostHandler::SetKeyspaceCallback::on_internal_timeout() {
  LOG_WARN("\"USE\" keyspace request timed out on host %s while attempting to prepare all queries",
           handler_->host_->address_string().c_str());
  handler_->close();
}
