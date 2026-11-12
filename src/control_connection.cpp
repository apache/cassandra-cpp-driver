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

#include "control_connection.hpp"

#include "collection_iterator.hpp"
#include "constants.hpp"
#include "error_response.hpp"
#include "event_response.hpp"
#include "load_balancing.hpp"
#include "logger.hpp"
#include "metadata.hpp"
#include "metrics.hpp"
#include "query_request.hpp"
#include "result_iterator.hpp"
#include "result_response.hpp"
#include "session.hpp"
#include "timer.hpp"
#include "utils.hpp"
#include "vector.hpp"

#include <algorithm>
#include <iomanip>
#include <iterator>

using namespace datastax;
using namespace datastax::internal::core;

namespace datastax { namespace internal { namespace core {

/**
 * A class for handling a single query on behalf of the control connection.
 */
class ControlRequestCallback : public SimpleRequestCallback {
public:
  typedef SharedRefPtr<ControlRequestCallback> Ptr;
  typedef void (*Callback)(ControlRequestCallback*);

  /**
   * Constructor. Initialize with a query.
   *
   * @param query The query to run.
   * @param control_connection The control connection the query is run on.
   * @param callback A callback that handles a successful query.
   */
  ControlRequestCallback(const String& query, ControlConnection* control_connection,
                         Callback callback)
      : SimpleRequestCallback(query)
      , control_connection_(control_connection)
      , callback_(callback) {}

  /**
   * Constructor. Initialize with a request object.
   *
   * @param request The request to run.
   * @param control_connection The control connection the request is run on.
   * @param callback A callback that handles a successful request.
   */
  ControlRequestCallback(const Request::ConstPtr& request, ControlConnection* control_connection,
                         Callback callback)
      : SimpleRequestCallback(request)
      , control_connection_(control_connection)
      , callback_(callback) {}

  virtual void on_internal_set(ResponseMessage* response) {
    if (response->opcode() != CQL_OPCODE_RESULT) {
      control_connection_->defunct();
      return;
    }
    result_ = ResultResponse::Ptr(response->response_body());
    callback_(this);
  }

  virtual void on_internal_error(CassError code, const String& message) {
    control_connection_->defunct();
  }

  virtual void on_internal_timeout() { control_connection_->defunct(); }

  ControlConnection* control_connection() { return control_connection_; }
  const ResultResponse::Ptr& result() const { return result_; }

private:
  ControlConnection* control_connection_;
  Callback callback_;
  ResultResponse::Ptr result_;
};

/**
 * A class for handling multiple simultaneous queries on behalf of the control
 * connection.
 */
class ChainedControlRequestCallback : public ChainedRequestCallback {
public:
  typedef SharedRefPtr<ChainedControlRequestCallback> Ptr;
  typedef void (*Callback)(ChainedControlRequestCallback*);

  /**
   * Constructor.
   *
   * @param key The key of the first query. Used to reference the query result.
   * @param query The first query.
   * @param control_connection The control connection to run the queries on.
   * @param callback The callback for a successful run of all the queries.
   */
  ChainedControlRequestCallback(const String& key, const String& query,
                                ControlConnection* control_connection, Callback callback)
      : ChainedRequestCallback(key, query)
      , control_connection_(control_connection)
      , callback_(callback) {}

  virtual void on_chain_set() {
    for (Map::const_iterator it = responses().begin(), end = responses().end(); it != end; ++it) {
      if (it->second->opcode() != CQL_OPCODE_RESULT) {
        control_connection_->defunct();
        return;
      }
    }
    callback_(this);
  }

  virtual void on_chain_error(CassError code, const String& message) {
    control_connection_->defunct();
  }

  virtual void on_chain_timeout() { control_connection_->defunct(); }

  ControlConnection* control_connection() { return control_connection_; }

private:
  ControlConnection* control_connection_;
  Callback callback_;
};

/**
 * A specialized request callback for handling node queries. This is needed for
 * new node and node moved events.
 */
class RefreshNodeCallback : public ControlRequestCallback {
public:
  /**
   * Constructor.
   *
   * @param address The address of the host that changed.
   * @param type The type of node change.
   * @param is_all_peers If true then the whole "system.peers" table is queried
   * instead of querying by the peer's primary key.
   * @param query The query to run for the node change.
   * @param control_connection The control connection the query is run on.
   */
  RefreshNodeCallback(const Address& address, ControlConnection::RefreshNodeType type,
                      bool is_all_peers, const String& query, ControlConnection* control_connection)
      : ControlRequestCallback(query, control_connection, ControlConnection::on_refresh_node)
      , address(address)
      , type(type)
      , is_all_peers(is_all_peers) {}

  const Address address;
  const ControlConnection::RefreshNodeType type;
  const bool is_all_peers;
};

/**
 * A specialized request callback for keyspace queries. This is needed for
 * keyspace change events.
 */
class RefreshKeyspaceCallback : public ControlRequestCallback {
public:
  /**
   * Constructor.
   *
   * @param keyspace_name The name of the keyspace that changed.
   * @param query The query to run for the keyspace change.
   * @param control_connection The control connection the query is run on.
   */
  RefreshKeyspaceCallback(const String& keyspace_name, const String& query,
                          ControlConnection* control_connection)
      : ControlRequestCallback(query, control_connection, ControlConnection::on_refresh_keyspace)
      , keyspace_name(keyspace_name) {}

  const String keyspace_name;
};

/**
 * A specialized request callback for table queries. This is needed for
 * table change events. Table/Materialized View changes require querying
 * multiple tables (tables, columns and indexes) to get all the metadata
 * necessary.
 */
class RefreshTableCallback : public ChainedControlRequestCallback {
public:
  /**
   * Constructor.
   *
   * @param keyspace_name The name of the table/view's keyspace.
   * @param table_or_view_name The name of the table/view that changed.
   * @param key The query key of the first query.
   * @param query The first query to run.
   * @param control_connection The control connection to run the queries on.
   */
  RefreshTableCallback(const String& keyspace_name, const String& table_or_view_name,
                       const String& key, const String& query,
                       ControlConnection* control_connection)
      : ChainedControlRequestCallback(key, query, control_connection,
                                      ControlConnection::on_refresh_table_or_view)
      , keyspace_name(keyspace_name)
      , table_or_view_name(table_or_view_name) {}

  const String keyspace_name;
  const String table_or_view_name;
};

/**
 * A specialized request callback for user type queries. This is needed for
 * user type change events.
 */
class RefreshTypeCallback : public ControlRequestCallback {
public:
  /**
   * Constructor.
   *
   * @param keyspace_name The name of the type's keyspace.
   * @param type_name The name of the type that changed.
   * @param query The query to run for the type change.
   * @param control_connection The control connection to run the query on.
   */
  RefreshTypeCallback(const String& keyspace_name, const String& type_name, const String& query,
                      ControlConnection* control_connection)
      : ControlRequestCallback(query, control_connection, ControlConnection::on_refresh_type)
      , keyspace_name(keyspace_name)
      , type_name(type_name) {}

  const String keyspace_name;
  const String type_name;
};

/**
 * A specialized request callback for function queries. This is needed for
 * function change events.
 */
class RefreshFunctionCallback : public ControlRequestCallback {
public:
  typedef Vector<String> StringVec;

  /**
   * Constructor.
   *
   * @param keyspace_name The name of the function/aggregate's keyspace.
   * @param function_name The name of the function/aggregate that changed.
   * @param arg_types The function/aggregate's argument types.
   * @param is_aggregate True if the function is an aggregate.
   * @param request The request to run for the function/aggregate change.
   * @param control_connection The control connection to run the query on.
   */
  RefreshFunctionCallback(const String& keyspace_name, const String& function_name,
                          const StringVec& arg_types, bool is_aggregate,
                          const Request::ConstPtr& request, ControlConnection* control_connection)
      : ControlRequestCallback(request, control_connection, ControlConnection::on_refresh_function)
      , keyspace_name(keyspace_name)
      , function_name(function_name)
      , arg_types(arg_types)
      , is_aggregate(is_aggregate) {}

  const String keyspace_name;
  const String function_name;
  const StringVec arg_types;
  const bool is_aggregate;
};

/**
 * A no operation control connection listener. This is used if no listener
 * is set.
 */
class NopControlConnectionListener : public ControlConnectionListener {
public:
  virtual void on_up(const Address& address) {}
  virtual void on_down(const Address& address) {}

  virtual void on_add(const Host::Ptr& host) {}
  virtual void on_remove(const Address& address) {}

  virtual void on_update_schema(SchemaType type, const ResultResponse::Ptr& result,
                                const String& keyspace_name, const String& target_name) {}

  virtual void on_drop_schema(SchemaType type, const String& keyspace_name,
                              const String& target_name) {}

  virtual void on_close(ControlConnection* connection) {}
};

}}} // namespace datastax::internal::core

static NopControlConnectionListener nop_listener__;

ControlConnectionSettings::ControlConnectionSettings()
    : use_schema(CASS_DEFAULT_USE_SCHEMA)
    , use_token_aware_routing(CASS_DEFAULT_USE_TOKEN_AWARE_ROUTING)
    , graceful_disconnect(CASS_DEFAULT_GRACEFUL_DISCONNECT_ENABLED)
    , address_factory(new AddressFactory()) {}

ControlConnectionSettings::ControlConnectionSettings(const Config& config)
    : connection_settings(config)
    , use_schema(config.use_schema())
    , use_token_aware_routing(config.token_aware_routing())
    , graceful_disconnect(config.graceful_disconnect())
    , address_factory(create_address_factory_from_config(config)) {}

ControlConnector::ControlConnector(const Host::Ptr& host, ProtocolVersion protocol_version,
                                   const Callback& callback)
    : connector_(
          new Connector(host, protocol_version, bind_callback(&ControlConnector::on_connect, this)))
    , callback_(callback)
    , error_code_(CONTROL_CONNECTION_OK)
    , listener_(NULL)
    , metrics_(NULL) {}

ControlConnection::ControlConnection(const Connection::Ptr& connection,
                                     ControlConnectionListener* listener,
                                     const ControlConnectionSettings& settings,
                                     const VersionNumber& server_version,
                                     const VersionNumber& dse_server_version,
                                     ListenAddressMap listen_addresses, Metrics* metrics)
    : connection_(connection)
    , settings_(settings)
    , server_version_(server_version)
    , dse_server_version_(dse_server_version)
    , listen_addresses_(listen_addresses)
    , listener_(listener ? listener : &nop_listener__)
    , metrics_(metrics) {
  connection_->set_listener(this);
  inc_ref();
}

int32_t ControlConnection::write_and_flush(const RequestCallback::Ptr& callback) {
  // Update the current time of the event loop because processing the token map
  // and schema metadata could take a bit of time.
  uv_update_time(connection_->loop());

  return connection_->write_and_flush(callback);
}

void ControlConnection::close() { connection_->close(); }

void ControlConnection::defunct() { connection_->defunct(); }

void ControlConnection::set_listener(ControlConnectionListener* listener) {
  listener_ = listener ? listener : &nop_listener__;
}

void ControlConnection::refresh_node(RefreshNodeType type, const Address& address) {
  bool is_connected_host = connection_->host()->rpc_address().equals(address, false);

  String query;
  bool is_all_peers = false;

  String listen_address(listen_addresses_[address]);

  if (is_connected_host) {
    query.assign(SELECT_LOCAL);
  } else if (!listen_address.empty()) {
    query.assign(SELECT_PEERS);
    query.append(" WHERE peer = '");
    query.append(listen_address);
    query.append("'");
  } else {
    is_all_peers = true;
    query.assign(SELECT_PEERS);
  }

  LOG_DEBUG("Refresh node: %s", query.c_str());

  RequestCallback::Ptr callback(new RefreshNodeCallback(address, type, is_all_peers, query, this));
  if (write_and_flush(callback) < 0) {
    LOG_ERROR("No more stream available while attempting to refresh node info");
    defunct();
  }
}

void ControlConnection::on_refresh_node(ControlRequestCallback* callback) {
  RefreshNodeCallback* refresh_callback = static_cast<RefreshNodeCallback*>(callback);
  refresh_callback->control_connection()->handle_refresh_node(refresh_callback);
}

void ControlConnection::handle_refresh_node(RefreshNodeCallback* callback) {
  bool found_host = false;
  const Row* row = NULL;
  ResultIterator rows(callback->result().get());

  if (callback->is_all_peers) {
    while (!found_host && rows.next()) {
      row = rows.row();
      if (settings_.address_factory->is_peer(row, connection_->host(), callback->address)) {
        found_host = true;
      }
    }
  } else if (rows.next()) {
    row = rows.row();
    found_host = true;
  }

  if (!found_host) {
    String address_str = callback->address.to_string();
    LOG_ERROR("No row found for host %s in %s's peers system table. "
              "%s will be ignored.",
              address_str.c_str(), address_string().c_str(), address_str.c_str());
    return;
  }

  Address address;
  if (settings_.address_factory->create(row, connection_->host(), &address)) {
    Host::Ptr host(new Host(address));
    host->set(row, settings_.use_token_aware_routing);
    listen_addresses_[host->rpc_address()] = determine_listen_address(address, row);

    switch (callback->type) {
      case NEW_NODE:
        listener_->on_add(host);
        break;
      case MOVED_NODE:
        listener_->on_remove(host->address());
        listener_->on_add(host);
        break;
    }
  }
}

void ControlConnection::refresh_keyspace(const StringRef& keyspace_name) {
  String query;

  if (server_version_ >= VersionNumber(3, 0, 0)) {
    query.assign(SELECT_KEYSPACES_30);
  } else {
    query.assign(SELECT_KEYSPACES_20);
  }
  query.append(" WHERE keyspace_name='")
      .append(keyspace_name.data(), keyspace_name.size())
      .append("'");

  LOG_DEBUG("Refreshing keyspace %s", query.c_str());

  RequestCallback::Ptr callback(
      new RefreshKeyspaceCallback(keyspace_name.to_string(), query, this));
  if (write_and_flush(callback) < 0) {
    LOG_ERROR("No more stream available while attempting to refresh keyspace info");
    defunct();
  }
}

void ControlConnection::on_refresh_keyspace(ControlRequestCallback* callback) {
  RefreshKeyspaceCallback* refresh_callback = static_cast<RefreshKeyspaceCallback*>(callback);
  refresh_callback->control_connection()->handle_refresh_keyspace(refresh_callback);
}

void ControlConnection::handle_refresh_keyspace(RefreshKeyspaceCallback* callback) {
  const ResultResponse::Ptr result = callback->result();
  if (result->row_count() == 0) {
    LOG_ERROR("No row found for keyspace %s in system schema table.",
              callback->keyspace_name.c_str());
    return;
  }
  listener_->on_update_schema(ControlConnectionListener::KEYSPACE, result, callback->keyspace_name);
}

void ControlConnection::refresh_table_or_view(const StringRef& keyspace_name,
                                              const StringRef& table_or_view_name) {
  String table_query;
  String view_query;
  String column_query;
  String index_query;

  if (server_version_ >= VersionNumber(3, 0, 0)) {
    table_query.assign(SELECT_TABLES_30);
    table_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND table_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    view_query.assign(SELECT_VIEWS_30);
    view_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND view_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    column_query.assign(SELECT_COLUMNS_30);
    column_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND table_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    index_query.assign(SELECT_INDEXES_30);
    index_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND table_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    LOG_DEBUG("Refreshing table/view %s; %s; %s; %s", table_query.c_str(), view_query.c_str(),
              column_query.c_str(), index_query.c_str());
  } else {
    table_query.assign(SELECT_COLUMN_FAMILIES_20);
    table_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND columnfamily_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    column_query.assign(SELECT_COLUMNS_20);
    column_query.append(" WHERE keyspace_name='")
        .append(keyspace_name.data(), keyspace_name.size())
        .append("' AND columnfamily_name='")
        .append(table_or_view_name.data(), table_or_view_name.size())
        .append("'");

    LOG_DEBUG("Refreshing table %s; %s", table_query.c_str(), column_query.c_str());
  }

  ChainedRequestCallback::Ptr callback(new RefreshTableCallback(
      keyspace_name.to_string(), table_or_view_name.to_string(), "tables", table_query, this));

  callback = callback->chain("columns", column_query);

  if (!view_query.empty()) {
    callback = callback->chain("views", view_query);
  }
  if (!index_query.empty()) {
    callback = callback->chain("indexes", index_query);
  }

  if (write_and_flush(callback) < 0) {
    LOG_ERROR("No more stream available while attempting to refresh table/view info");
    defunct();
  }
}

void ControlConnection::on_refresh_table_or_view(ChainedControlRequestCallback* callback) {
  RefreshTableCallback* refresh_callback = static_cast<RefreshTableCallback*>(callback);
  refresh_callback->control_connection()->handle_refresh_table_or_view(refresh_callback);
}

void ControlConnection::handle_refresh_table_or_view(RefreshTableCallback* callback) {
  ResultResponse::Ptr tables_result(callback->result("tables"));
  if (!tables_result || tables_result->row_count() == 0) {
    ResultResponse::Ptr views_result(callback->result("views"));
    if (!views_result || views_result->row_count() == 0) {
      LOG_ERROR("No row found for table (or view) %s.%s in system schema tables.",
                callback->keyspace_name.c_str(), callback->table_or_view_name.c_str());
      return;
    }
    listener_->on_update_schema(ControlConnectionListener::VIEW, views_result,
                                callback->keyspace_name, callback->table_or_view_name);
  } else {
    listener_->on_update_schema(ControlConnectionListener::TABLE, tables_result,
                                callback->keyspace_name, callback->table_or_view_name);
  }

  ResultResponse::Ptr columns_result(callback->result("columns"));
  if (columns_result) {
    listener_->on_update_schema(ControlConnectionListener::COLUMN, columns_result,
                                callback->keyspace_name, callback->table_or_view_name);
  }

  ResultResponse::Ptr indexes_result(callback->result("indexes"));
  if (indexes_result) {
    listener_->on_update_schema(ControlConnectionListener::INDEX, indexes_result,
                                callback->keyspace_name, callback->table_or_view_name);
  }
}

void ControlConnection::refresh_type(const StringRef& keyspace_name, const StringRef& type_name) {
  String query;
  if (server_version_ >= VersionNumber(3, 0, 0)) {
    query.assign(SELECT_USERTYPES_30);
  } else {
    query.assign(SELECT_USERTYPES_21);
  }

  query.append(" WHERE keyspace_name='")
      .append(keyspace_name.data(), keyspace_name.size())
      .append("' AND type_name='")
      .append(type_name.data(), type_name.size())
      .append("'");

  LOG_DEBUG("Refreshing type %s", query.c_str());

  RequestCallback::Ptr callback(
      new RefreshTypeCallback(keyspace_name.to_string(), type_name.to_string(), query, this));
  if (write_and_flush(callback) < 0) {
    LOG_ERROR("No more stream available while attempting to refresh type info");
    defunct();
  }
}

void ControlConnection::on_refresh_type(ControlRequestCallback* callback) {
  RefreshTypeCallback* refresh_callback = static_cast<RefreshTypeCallback*>(callback);
  refresh_callback->control_connection()->handle_refresh_type(refresh_callback);
}

void ControlConnection::handle_refresh_type(RefreshTypeCallback* callback) {
  const ResultResponse::Ptr result = callback->result();
  if (result->row_count() == 0) {
    LOG_ERROR("No row found for keyspace %s and type %s in system schema.",
              callback->keyspace_name.c_str(), callback->type_name.c_str());
    return;
  }
  listener_->on_update_schema(ControlConnectionListener::USER_TYPE, result, callback->keyspace_name,
                              callback->type_name);
}

void ControlConnection::refresh_function(const StringRef& keyspace_name,
                                         const StringRef& function_name,
                                         const StringRefVec& arg_types, bool is_aggregate) {
  String query;
  if (server_version_ >= VersionNumber(3, 0, 0)) {
    if (is_aggregate) {
      query.assign(SELECT_AGGREGATES_30);
      query.append(" WHERE keyspace_name=? AND aggregate_name=? AND argument_types=?");
    } else {
      query.assign(SELECT_FUNCTIONS_30);
      query.append(" WHERE keyspace_name=? AND function_name=? AND argument_types=?");
    }
  } else {
    if (is_aggregate) {
      query.assign(SELECT_AGGREGATES_22);
      query.append(" WHERE keyspace_name=? AND aggregate_name=? AND signature=?");
    } else {
      query.assign(SELECT_FUNCTIONS_22);
      query.append(" WHERE keyspace_name=? AND function_name=? AND signature=?");
    }
  }

  LOG_DEBUG("Refreshing %s %s in keyspace %s", is_aggregate ? "aggregate" : "function",
            Metadata::full_function_name(function_name.to_string(), to_strings(arg_types)).c_str(),
            String(keyspace_name.data(), keyspace_name.length()).c_str());

  SharedRefPtr<QueryRequest> request(new QueryRequest(query, 3));
  SharedRefPtr<Collection> signature(new Collection(CASS_COLLECTION_TYPE_LIST, arg_types.size()));

  for (StringRefVec::const_iterator i = arg_types.begin(), end = arg_types.end(); i != end; ++i) {
    signature->append(CassString(i->data(), i->size()));
  }

  request->set(0, CassString(keyspace_name.data(), keyspace_name.size()));
  request->set(1, CassString(function_name.data(), function_name.size()));
  request->set(2, signature.get());

  RequestCallback::Ptr callback(
      new RefreshFunctionCallback(keyspace_name.to_string(), function_name.to_string(),
                                  to_strings(arg_types), is_aggregate, request, this));
  if (write_and_flush(callback) < 0) {
    LOG_ERROR("No more stream available while attempting to refresh function info");
    defunct();
  }
}

void ControlConnection::on_refresh_function(ControlRequestCallback* callback) {
  RefreshFunctionCallback* refresh_callback = static_cast<RefreshFunctionCallback*>(callback);
  refresh_callback->control_connection()->handle_refresh_function(refresh_callback);
}

void ControlConnection::handle_refresh_function(RefreshFunctionCallback* callback) {
  const ResultResponse::Ptr result = callback->result();
  if (result->row_count() == 0) {
    LOG_ERROR("No row found for keyspace %s and %s %s", callback->keyspace_name.c_str(),
              callback->is_aggregate ? "aggregate" : "function",
              Metadata::full_function_name(callback->function_name, callback->arg_types).c_str());
    return;
  }

  listener_->on_update_schema(
      callback->is_aggregate ? ControlConnectionListener::AGGREGATE
                             : ControlConnectionListener::FUNCTION,
      result, callback->keyspace_name,
      Metadata::full_function_name(callback->function_name, callback->arg_types));
}

void ControlConnection::on_close(Connection* connection) {
  listener_->on_close(this);
  dec_ref();
}

void ControlConnection::on_event(const EventResponse::Ptr& response) {
  switch (response->event_type()) {
    case CASS_EVENT_TOPOLOGY_CHANGE: {
      String address_str = response->affected_node().to_string();
      switch (response->topology_change()) {
        case EventResponse::NEW_NODE: {
          LOG_INFO("New node %s added event", address_str.c_str());
          refresh_node(NEW_NODE, response->affected_node());
          break;
        }

        case EventResponse::REMOVED_NODE: {
          LOG_INFO("Node %s removed event", address_str.c_str());
          listen_addresses_.erase(response->affected_node());
          listener_->on_remove(response->affected_node());
          break;
        }

        case EventResponse::MOVED_NODE:
          LOG_INFO("Node %s moved event", address_str.c_str());
          refresh_node(MOVED_NODE, response->affected_node());
          break;
      }
      break;
    }

    case CASS_EVENT_STATUS_CHANGE: {
      String address_str = response->affected_node().to_string();
      switch (response->status_change()) {
        case EventResponse::UP: {
          LOG_DEBUG("Node %s is up event", address_str.c_str());
          listener_->on_up(response->affected_node());
          break;
        }

        case EventResponse::DOWN: {
          LOG_DEBUG("Node %s is down event", address_str.c_str());
          listener_->on_down(response->affected_node());
          break;
        }
      }
      break;
    }

    case CASS_EVENT_GRACEFUL_DISCONNECT: {
      // The connection has already started draining itself (CEP-59); once the
      // drain completes it closes and the cluster reconnects the control
      // connection to another host.
      LOG_INFO("Received GRACEFUL_DISCONNECT event on control connection to host %s, "
               "the server is shutting down gracefully",
               connection_->address_string().c_str());
      if (metrics_) {
        metrics_->graceful_disconnects.inc();
      }
      break;
    }

    case CASS_EVENT_SCHEMA_CHANGE:
      // Only handle keyspace events when using token-aware routing
      if (!settings_.use_schema && response->schema_change_target() != EventResponse::KEYSPACE) {
        return;
      }

      LOG_DEBUG("Schema change (%d): %.*s %.*s", response->schema_change(),
                (int)response->keyspace().size(), response->keyspace().data(),
                (int)response->target().size(), response->target().data());

      switch (response->schema_change()) {
        case EventResponse::CREATED:
        case EventResponse::UPDATED:
          switch (response->schema_change_target()) {
            case EventResponse::KEYSPACE:
              refresh_keyspace(response->keyspace());
              break;
            case EventResponse::TABLE:
              refresh_table_or_view(response->keyspace(), response->target());
              break;
            case EventResponse::TYPE:
              refresh_type(response->keyspace(), response->target());
              break;
            case EventResponse::FUNCTION:
            case EventResponse::AGGREGATE:
              refresh_function(response->keyspace(), response->target(), response->arg_types(),
                               response->schema_change_target() == EventResponse::AGGREGATE);
              break;
          }
          break;

        case EventResponse::DROPPED:
          switch (response->schema_change_target()) {
            case EventResponse::KEYSPACE:
              listener_->on_drop_schema(ControlConnectionListener::KEYSPACE,
                                        response->keyspace().to_string(),
                                        response->target().to_string());
              break;
            case EventResponse::TABLE:
              listener_->on_drop_schema(ControlConnectionListener::TABLE,
                                        response->keyspace().to_string(),
                                        response->target().to_string());
              break;
            case EventResponse::TYPE:
              listener_->on_drop_schema(ControlConnectionListener::USER_TYPE,
                                        response->keyspace().to_string(),
                                        response->target().to_string());
              break;
            case EventResponse::FUNCTION:
              listener_->on_drop_schema(
                  ControlConnectionListener::FUNCTION, response->keyspace().to_string(),
                  Metadata::full_function_name(response->target().to_string(),
                                               to_strings(response->arg_types())));
              break;
            case EventResponse::AGGREGATE:
              listener_->on_drop_schema(
                  ControlConnectionListener::AGGREGATE, response->keyspace().to_string(),
                  Metadata::full_function_name(response->target().to_string(),
                                               to_strings(response->arg_types())));
              break;
          }
          break;
      }
      break;

    default:
      assert(false);
      break;
  }
}
