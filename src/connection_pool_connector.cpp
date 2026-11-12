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

#include "connection_pool_connector.hpp"

#include "event_loop.hpp"
#include "metrics.hpp"

using namespace datastax;
using namespace datastax::internal::core;

ConnectionPoolConnector::ConnectionPoolConnector(const Host::Ptr& host,
                                                 ProtocolVersion protocol_version,
                                                 const Callback& callback)
    : loop_(NULL)
    , callback_(callback)
    , is_canceled_(false)
    , remaining_(0)
    , host_(host)
    , protocol_version_(protocol_version)
    , listener_(NULL)
    , metrics_(NULL) {}

ConnectionPoolConnector* ConnectionPoolConnector::with_listener(ConnectionPoolListener* listener) {
  listener_ = listener;
  return this;
}

ConnectionPoolConnector* ConnectionPoolConnector::with_keyspace(const String& keyspace) {
  keyspace_ = keyspace;
  return this;
}

ConnectionPoolConnector* ConnectionPoolConnector::with_metrics(Metrics* metrics) {
  metrics_ = metrics;
  return this;
}

ConnectionPoolConnector*
ConnectionPoolConnector::with_settings(const ConnectionPoolSettings& settings) {
  settings_ = settings;
  return this;
}

void ConnectionPoolConnector::connect(uv_loop_t* loop) {
  inc_ref();
  loop_ = loop;
  remaining_ = settings_.num_connections_per_host;
  for (size_t i = 0; i < settings_.num_connections_per_host; ++i) {
    Connector::Ptr connector(new Connector(
        host_, protocol_version_, bind_callback(&ConnectionPoolConnector::on_connect, this)));
    pending_connections_.push_back(connector);
    connector->with_keyspace(keyspace_)
        ->with_metrics(metrics_)
        ->with_settings(settings_.connection_settings)
        ->with_event_types(settings_.graceful_disconnect ? CASS_EVENT_GRACEFUL_DISCONNECT : 0)
        ->connect(loop);
  }
}

void ConnectionPoolConnector::cancel() {
  is_canceled_ = true;
  if (pool_) {
    pool_->close();
  } else {
    for (Connector::Vec::iterator it = pending_connections_.begin(),
                                  end = pending_connections_.end();
         it != end; ++it) {
      (*it)->cancel();
    }
    for (Connection::Vec::iterator it = connections_.begin(), end = connections_.end(); it != end;
         ++it) {
      (*it)->close();
    }
  }
}

ConnectionPool::Ptr ConnectionPoolConnector::release_pool() {
  ConnectionPool::Ptr temp = pool_;
  pool_.reset();
  return temp;
}

Connector::ConnectionError ConnectionPoolConnector::error_code() const {
  return critical_error_connector_ ? critical_error_connector_->error_code()
                                   : Connector::CONNECTION_OK;
}

String ConnectionPoolConnector::error_message() const {
  return critical_error_connector_ ? critical_error_connector_->error_message() : "";
}

bool ConnectionPoolConnector::is_ok() const { return !is_critical_error(); }

bool ConnectionPoolConnector::is_critical_error() const { return critical_error_connector_; }

bool ConnectionPoolConnector::is_keyspace_error() const {
  if (critical_error_connector_) {
    return critical_error_connector_->is_keyspace_error();
  }
  return false;
}

void ConnectionPoolConnector::on_connect(Connector* connector) {
  pending_connections_.erase(
      std::remove(pending_connections_.begin(), pending_connections_.end(), connector),
      pending_connections_.end());

  if (connector->is_ok()) {
    connections_.push_back(connector->release_connection());
  } else if (!connector->is_canceled()) {
    LOG_ERROR("Connection pool was unable to connect to host %s because of the following error: %s",
              host_->address().to_string().c_str(), connector->error_message().c_str());

    if (connector->is_critical_error()) {
      if (!critical_error_connector_) {
        critical_error_connector_.reset(connector);
        for (Connector::Vec::iterator it = pending_connections_.begin(),
                                      end = pending_connections_.end();
             it != end; ++it) {
          (*it)->cancel();
        }
      }
    }
  }

  if (--remaining_ == 0) {
    if (!is_canceled_) {
      if (!critical_error_connector_) {
        pool_.reset(new ConnectionPool(connections_, listener_, keyspace_, loop_, host_,
                                       protocol_version_, settings_, metrics_));
      } else {
        if (listener_) {
          listener_->on_pool_critical_error(host_->address(),
                                            critical_error_connector_->error_code(),
                                            critical_error_connector_->error_message());
        }
      }
    }
    callback_(this);
    // If the pool hasn't been released then close it.
    if (pool_) {
      // If the callback doesn't take possession of the pool then we should
      // also clear the listener.
      pool_->set_listener();
      pool_->close();
    }
    dec_ref();
  }
}
