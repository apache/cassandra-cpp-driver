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

#include "delayed_connector.hpp"

#include "event_loop.hpp"

using namespace datastax;
using namespace datastax::internal::core;

DelayedConnector::DelayedConnector(const Host::Ptr& host, ProtocolVersion protocol_version,
                                   const Callback& callback)
    : connector_(
          new Connector(host, protocol_version, bind_callback(&DelayedConnector::on_connect, this)))
    , callback_(callback)
    , is_canceled_(false) {}

DelayedConnector* DelayedConnector::with_keyspace(const String& keyspace) {
  connector_->with_keyspace(keyspace);
  return this;
}

DelayedConnector* DelayedConnector::with_metrics(Metrics* metrics) {
  connector_->with_metrics(metrics);
  return this;
}

DelayedConnector* DelayedConnector::with_event_types(int event_types) {
  connector_->with_event_types(event_types);
  return this;
}

DelayedConnector* DelayedConnector::with_settings(const ConnectionSettings& settings) {
  connector_->with_settings(settings);
  return this;
}

void DelayedConnector::delayed_connect(uv_loop_t* loop, uint64_t wait_time_ms) {
  inc_ref();
  if (wait_time_ms > 0) {
    delayed_connect_timer_.start(loop, wait_time_ms,
                                 bind_callback(&DelayedConnector::on_delayed_connect, this));
  } else {
    internal_connect(loop);
  }
}

void DelayedConnector::attempt_immediate_connect() {
  if (delayed_connect_timer_.is_running() && !is_canceled_) {
    internal_connect(delayed_connect_timer_.loop());
    delayed_connect_timer_.stop();
  }
}

void DelayedConnector::cancel() {
  is_canceled_ = true;
  if (delayed_connect_timer_.is_running()) {
    delayed_connect_timer_.stop();
    callback_(this);
    dec_ref();
  } else {
    connector_->cancel();
  }
}

Connection::Ptr DelayedConnector::release_connection() { return connector_->release_connection(); }

bool DelayedConnector::is_canceled() const { return is_canceled_; }

bool DelayedConnector::is_ok() const { return !is_canceled() && connector_->is_ok(); }

bool DelayedConnector::is_critical_error() const {
  return !is_canceled() && connector_->is_critical_error();
}

bool DelayedConnector::is_keyspace_error() const {
  return !is_canceled() && connector_->is_keyspace_error();
}

Connector::ConnectionError DelayedConnector::error_code() const {
  if (is_canceled()) {
    return Connector::CONNECTION_CANCELED;
  }
  return connector_->error_code();
}

void DelayedConnector::internal_connect(uv_loop_t* loop) { connector_->connect(loop); }

void DelayedConnector::on_connect(Connector* connector) {
  callback_(this);
  dec_ref();
}

void DelayedConnector::on_delayed_connect(Timer* timer) { internal_connect(timer->loop()); }
