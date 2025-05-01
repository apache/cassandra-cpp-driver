/*
  Copyright (c) DataStax, Inc.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#include "cluster_config.hpp"

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

extern "C" {

CassCluster* cass_cluster_new() { return CassCluster::to(new ClusterConfig()); }

CassError cass_cluster_set_port(CassCluster* cluster, int port) {
  if (port <= 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  } else if (cluster->config().cloud_secure_connection_config().is_loaded()) {
    LOG_ERROR("Port cannot be overridden with cloud secure connection bundle");
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  cluster->config().set_port(port);
  return CASS_OK;
}

void cass_cluster_set_ssl(CassCluster* cluster, CassSsl* ssl) {
  if (cluster->config().cloud_secure_connection_config().is_loaded()) {
    LOG_ERROR("SSL context cannot be overridden with cloud secure connection bundle");
  } else {
    cluster->config().set_ssl_context(ssl->from());
  }
}

CassError cass_cluster_set_protocol_version(CassCluster* cluster, int protocol_version) {
  if (cluster->config().use_beta_protocol_version()) {
    LOG_ERROR("The protocol version is already set to the newest beta version %s "
              "and cannot be explicitly set.",
              ProtocolVersion::newest_beta().to_string().c_str());
    return CASS_ERROR_LIB_BAD_PARAMS;
  } else {
    ProtocolVersion version(protocol_version);
    if (version < ProtocolVersion::lowest_supported()) {
      LOG_ERROR("Protocol version %s is lower than the lowest supported "
                "protocol version %s",
                version.to_string().c_str(),
                ProtocolVersion::lowest_supported().to_string().c_str());
      return CASS_ERROR_LIB_BAD_PARAMS;
    } else if (version > ProtocolVersion::highest_supported(version.is_dse())) {
      LOG_ERROR("Protocol version %s is higher than the highest supported "
                "protocol version %s (consider using the newest beta protocol version).",
                version.to_string().c_str(),
                ProtocolVersion::highest_supported(version.is_dse()).to_string().c_str());
      return CASS_ERROR_LIB_BAD_PARAMS;
    }
    cluster->config().set_protocol_version(version);
    return CASS_OK;
  }
}

CassError cass_cluster_set_use_beta_protocol_version(CassCluster* cluster, cass_bool_t enable) {
  cluster->config().set_use_beta_protocol_version(enable == cass_true);
  cluster->config().set_protocol_version(enable ? ProtocolVersion::newest_beta()
                                                : ProtocolVersion::highest_supported());
  return CASS_OK;
}

CassError cass_cluster_set_consistency(CassCluster* cluster, CassConsistency consistency) {
  if (consistency == CASS_CONSISTENCY_UNKNOWN) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_consistency(consistency);
  return CASS_OK;
}

CassError cass_cluster_set_serial_consistency(CassCluster* cluster, CassConsistency consistency) {
  if (consistency == CASS_CONSISTENCY_UNKNOWN) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_serial_consistency(consistency);
  return CASS_OK;
}

CassError cass_cluster_set_num_threads_io(CassCluster* cluster, unsigned num_threads) {
  if (num_threads == 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_thread_count_io(num_threads);
  return CASS_OK;
}

CassError cass_cluster_set_queue_size_io(CassCluster* cluster, unsigned queue_size) {
  if (queue_size == 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_queue_size_io(queue_size);
  return CASS_OK;
}

CassError cass_cluster_set_queue_size_event(CassCluster* cluster, unsigned queue_size) {
  return CASS_OK;
}

CassError cass_cluster_set_contact_points(CassCluster* cluster, const char* contact_points) {
  return cass_cluster_set_contact_points_n(cluster, contact_points, SAFE_STRLEN(contact_points));
}

CassError cass_cluster_set_contact_points_n(CassCluster* cluster, const char* contact_points,
                                            size_t contact_points_length) {
  if (cluster->config().cloud_secure_connection_config().is_loaded()) {
    LOG_ERROR("Contact points cannot be overridden with cloud secure connection bundle");
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  if (contact_points_length == 0) {
    cluster->config().contact_points().clear();
  } else {
    Vector<String> exploded;
    explode(String(contact_points, contact_points_length), exploded);
    for (Vector<String>::const_iterator it = exploded.begin(), end = exploded.end(); it != end;
         ++it) {
      cluster->config().contact_points().push_back(Address(*it, -1));
    }
  }
  return CASS_OK;
}

CassError cass_cluster_set_core_connections_per_host(CassCluster* cluster,
                                                     unsigned num_connections) {
  if (num_connections == 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_core_connections_per_host(num_connections);
  return CASS_OK;
}

CassError cass_cluster_set_max_connections_per_host(CassCluster* cluster,
                                                    unsigned num_connections) {
  return CASS_OK;
}

void cass_cluster_set_reconnect_wait_time(CassCluster* cluster, unsigned wait_time_ms) {
  cass_cluster_set_constant_reconnect(cluster, wait_time_ms);
}

void cass_cluster_set_constant_reconnect(CassCluster* cluster, cass_uint64_t delay_ms) {
  cluster->config().set_constant_reconnect(delay_ms);
}

CassError cass_cluster_set_exponential_reconnect(CassCluster* cluster, cass_uint64_t base_delay_ms,
                                                 cass_uint64_t max_delay_ms) {
  if (base_delay_ms <= 1) {
    LOG_ERROR("Base delay must be greater than 1");
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  if (max_delay_ms <= 1) {
    LOG_ERROR("Max delay must be greater than 1");
    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  if (max_delay_ms < base_delay_ms) {
    LOG_ERROR("Max delay cannot be less than base delay");
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_exponential_reconnect(base_delay_ms, max_delay_ms);
  return CASS_OK;
}

CassError cass_cluster_set_coalesce_delay(CassCluster* cluster, cass_int64_t delay_us) {
  if (delay_us < 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_coalesce_delay_us(delay_us);
  return CASS_OK;
}

CassError cass_cluster_set_new_request_ratio(CassCluster* cluster, cass_int32_t ratio) {
  if (ratio <= 0 || ratio > 100) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_new_request_ratio(ratio);
  return CASS_OK;
}

CassError cass_cluster_set_max_concurrent_creation(CassCluster* cluster, unsigned num_connections) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_max_concurrent_requests_threshold(CassCluster* cluster,
                                                             unsigned num_requests) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_max_requests_per_flush(CassCluster* cluster, unsigned num_requests) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_write_bytes_high_water_mark(CassCluster* cluster, unsigned num_bytes) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_write_bytes_low_water_mark(CassCluster* cluster, unsigned num_bytes) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_pending_requests_high_water_mark(CassCluster* cluster,
                                                            unsigned num_requests) {
  // Deprecated
  return CASS_OK;
}

CassError cass_cluster_set_pending_requests_low_water_mark(CassCluster* cluster,
                                                           unsigned num_requests) {
  // Deprecated
  return CASS_OK;
}

void cass_cluster_set_connect_timeout(CassCluster* cluster, unsigned timeout_ms) {
  cluster->config().set_connect_timeout(timeout_ms);
}

void cass_cluster_set_request_timeout(CassCluster* cluster, unsigned timeout_ms) {
  cluster->config().set_request_timeout(timeout_ms);
}

void cass_cluster_set_resolve_timeout(CassCluster* cluster, unsigned timeout_ms) {
  cluster->config().set_resolve_timeout(timeout_ms);
}

void cass_cluster_set_max_schema_wait_time(CassCluster* cluster, unsigned wait_time_ms) {
  cluster->config().set_max_schema_wait_time_ms(wait_time_ms);
}

void cass_cluster_set_tracing_max_wait_time(CassCluster* cluster, unsigned wait_time_ms) {
  cluster->config().set_max_tracing_wait_time_ms(wait_time_ms);
}

void cass_cluster_set_tracing_retry_wait_time(CassCluster* cluster, unsigned wait_time_ms) {
  cluster->config().set_retry_tracing_wait_time_ms(wait_time_ms);
}

void cass_cluster_set_tracing_consistency(CassCluster* cluster, CassConsistency consistency) {
  cluster->config().set_tracing_consistency(consistency);
}

void cass_cluster_set_credentials(CassCluster* cluster, const char* username,
                                  const char* password) {
  return cass_cluster_set_credentials_n(cluster, username, SAFE_STRLEN(username), password,
                                        SAFE_STRLEN(password));
}

void cass_cluster_set_credentials_n(CassCluster* cluster, const char* username,
                                    size_t username_length, const char* password,
                                    size_t password_length) {
  cluster->config().set_credentials(String(username, username_length),
                                    String(password, password_length));
}

void cass_cluster_set_load_balance_round_robin(CassCluster* cluster) {
  cluster->config().set_load_balancing_policy(new RoundRobinPolicy());
}

CassError cass_cluster_set_load_balance_dc_aware(CassCluster* cluster, const char* local_dc,
                                                 unsigned used_hosts_per_remote_dc,
                                                 cass_bool_t allow_remote_dcs_for_local_cl) {
  return cass_cluster_set_load_balance_dc_aware_n(cluster, local_dc, SAFE_STRLEN(local_dc),
                                                  used_hosts_per_remote_dc,
                                                  allow_remote_dcs_for_local_cl);
}

CassError cass_cluster_set_load_balance_dc_aware_n(CassCluster* cluster, const char* local_dc,
                                                   size_t local_dc_length,
                                                   unsigned used_hosts_per_remote_dc,
                                                   cass_bool_t allow_remote_dcs_for_local_cl) {
  // If local_dc is NULL or length is 0, we use an empty string which causes the driver
  // to use the DC of the first connected node
  String dc_name = (local_dc != NULL && local_dc_length > 0) ? 
                    String(local_dc, local_dc_length) : String();
  
  cluster->config().set_load_balancing_policy(new DCAwarePolicy(
      dc_name, used_hosts_per_remote_dc, !allow_remote_dcs_for_local_cl));
  return CASS_OK;
}

void cass_cluster_set_token_aware_routing(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_token_aware_routing(enabled == cass_true);
}

void cass_cluster_set_token_aware_routing_shuffle_replicas(CassCluster* cluster,
                                                           cass_bool_t enabled) {
  cluster->config().set_token_aware_routing_shuffle_replicas(enabled == cass_true);
}

void cass_cluster_set_latency_aware_routing(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_latency_aware_routing(enabled == cass_true);
}

void cass_cluster_set_latency_aware_routing_settings(
    CassCluster* cluster, cass_double_t exclusion_threshold, cass_uint64_t scale_ms,
    cass_uint64_t retry_period_ms, cass_uint64_t update_rate_ms, cass_uint64_t min_measured) {
  LatencyAwarePolicy::Settings settings;
  settings.exclusion_threshold = exclusion_threshold;
  settings.scale_ns = scale_ms * 1000 * 1000;
  settings.retry_period_ns = retry_period_ms * 1000 * 1000;
  settings.update_rate_ms = update_rate_ms;
  settings.min_measured = min_measured;
  cluster->config().set_latency_aware_routing_settings(settings);
}

void cass_cluster_set_whitelist_filtering(CassCluster* cluster, const char* hosts) {
  cass_cluster_set_whitelist_filtering_n(cluster, hosts, SAFE_STRLEN(hosts));
}

void cass_cluster_set_whitelist_filtering_n(CassCluster* cluster, const char* hosts,
                                            size_t hosts_length) {
  if (hosts_length == 0) {
    cluster->config().default_profile().whitelist().clear();
  } else {
    explode(String(hosts, hosts_length), cluster->config().default_profile().whitelist());
  }
}

void cass_cluster_set_blacklist_filtering(CassCluster* cluster, const char* hosts) {
  cass_cluster_set_blacklist_filtering_n(cluster, hosts, SAFE_STRLEN(hosts));
}

void cass_cluster_set_blacklist_filtering_n(CassCluster* cluster, const char* hosts,
                                            size_t hosts_length) {
  if (hosts_length == 0) {
    cluster->config().default_profile().blacklist().clear();
  } else {
    explode(String(hosts, hosts_length), cluster->config().default_profile().blacklist());
  }
}

void cass_cluster_set_whitelist_dc_filtering(CassCluster* cluster, const char* dcs) {
  cass_cluster_set_whitelist_dc_filtering_n(cluster, dcs, SAFE_STRLEN(dcs));
}

void cass_cluster_set_whitelist_dc_filtering_n(CassCluster* cluster, const char* dcs,
                                               size_t dcs_length) {
  if (dcs_length == 0) {
    cluster->config().default_profile().whitelist_dc().clear();
  } else {
    explode(String(dcs, dcs_length), cluster->config().default_profile().whitelist_dc());
  }
}

void cass_cluster_set_blacklist_dc_filtering(CassCluster* cluster, const char* dcs) {
  cass_cluster_set_blacklist_dc_filtering_n(cluster, dcs, SAFE_STRLEN(dcs));
}

void cass_cluster_set_blacklist_dc_filtering_n(CassCluster* cluster, const char* dcs,
                                               size_t dcs_length) {
  if (dcs_length == 0) {
    cluster->config().default_profile().blacklist_dc().clear();
  } else {
    explode(String(dcs, dcs_length), cluster->config().default_profile().blacklist_dc());
  }
}

void cass_cluster_set_tcp_nodelay(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_tcp_nodelay(enabled == cass_true);
}

void cass_cluster_set_tcp_keepalive(CassCluster* cluster, cass_bool_t enabled,
                                    unsigned delay_secs) {
  cluster->config().set_tcp_keepalive(enabled == cass_true, delay_secs);
}

CassError cass_cluster_set_authenticator_callbacks(
    CassCluster* cluster, const CassAuthenticatorCallbacks* exchange_callbacks,
    CassAuthenticatorDataCleanupCallback cleanup_callback, void* data) {
  cluster->config().set_auth_provider(
      AuthProvider::Ptr(new ExternalAuthProvider(exchange_callbacks, cleanup_callback, data)));
  return CASS_OK;
}

void cass_cluster_set_connection_heartbeat_interval(CassCluster* cluster, unsigned interval_secs) {
  cluster->config().set_connection_heartbeat_interval_secs(interval_secs);
}

void cass_cluster_set_connection_idle_timeout(CassCluster* cluster, unsigned timeout_secs) {
  cluster->config().set_connection_idle_timeout_secs(timeout_secs);
}

void cass_cluster_set_retry_policy(CassCluster* cluster, CassRetryPolicy* retry_policy) {
  cluster->config().set_retry_policy(retry_policy);
}

void cass_cluster_set_timestamp_gen(CassCluster* cluster, CassTimestampGen* timestamp_gen) {
  cluster->config().set_timestamp_gen(timestamp_gen);
}

void cass_cluster_set_use_schema(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_use_schema(enabled == cass_true);
}

CassError cass_cluster_set_use_hostname_resolution(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_use_hostname_resolution(enabled == cass_true);
  return CASS_OK;
}

CassError cass_cluster_set_use_randomized_contact_points(CassCluster* cluster,
                                                         cass_bool_t enabled) {
  cluster->config().set_use_randomized_contact_points(enabled == cass_true);
  return CASS_OK;
}

CassError cass_cluster_set_constant_speculative_execution_policy(CassCluster* cluster,
                                                                 cass_int64_t constant_delay_ms,
                                                                 int max_speculative_executions) {
  if (constant_delay_ms < 0 || max_speculative_executions < 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_speculative_execution_policy(
      new ConstantSpeculativeExecutionPolicy(constant_delay_ms, max_speculative_executions));
  return CASS_OK;
}

CassError cass_cluster_set_no_speculative_execution_policy(CassCluster* cluster) {
  cluster->config().set_speculative_execution_policy(new NoSpeculativeExecutionPolicy());
  return CASS_OK;
}

CassError cass_cluster_set_max_reusable_write_objects(CassCluster* cluster, unsigned num_objects) {
  cluster->config().set_max_reusable_write_objects(num_objects);
  return CASS_OK;
}

CassError cass_cluster_set_execution_profile(CassCluster* cluster, const char* name,
                                             CassExecProfile* profile) {
  return cass_cluster_set_execution_profile_n(cluster, name, SAFE_STRLEN(name), profile);
}

CassError cass_cluster_set_execution_profile_n(CassCluster* cluster, const char* name,
                                               size_t name_length, CassExecProfile* profile) {
  if (name_length == 0 || !profile) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_execution_profile(String(name, name_length), profile);
  return CASS_OK;
}

CassError cass_cluster_set_prepare_on_all_hosts(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_prepare_on_all_hosts(enabled == cass_true);
  return CASS_OK;
}

CassError cass_cluster_set_prepare_on_up_or_add_host(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_prepare_on_up_or_add_host(enabled == cass_true);
  return CASS_OK;
}

CassError cass_cluster_set_local_address(CassCluster* cluster, const char* name) {
  return cass_cluster_set_local_address_n(cluster, name, SAFE_STRLEN(name));
}

CassError cass_cluster_set_local_address_n(CassCluster* cluster, const char* name,
                                           size_t name_length) {
  if (name_length == 0 || name == NULL) {
    cluster->config().set_local_address(Address());
  } else {
    Address address(String(name, name_length), 0);
    if (address.is_valid_and_resolved()) {
      cluster->config().set_local_address(address);
    } else {
      return CASS_ERROR_LIB_HOST_RESOLUTION;
    }
  }
  return CASS_OK;
}

CassError cass_cluster_set_no_compact(CassCluster* cluster, cass_bool_t enabled) {
  cluster->config().set_no_compact(enabled == cass_true);
  return CASS_OK;
}

CassError cass_cluster_set_host_listener_callback(CassCluster* cluster,
                                                  CassHostListenerCallback callback, void* data) {
  cluster->config().set_host_listener(
      DefaultHostListener::Ptr(new ExternalHostListener(callback, data)));
  return CASS_OK;
}

CassError cass_cluster_set_cloud_secure_connection_bundle(CassCluster* cluster, const char* path) {
  return cass_cluster_set_cloud_secure_connection_bundle_n(cluster, path, SAFE_STRLEN(path));
}

CassError cass_cluster_set_cloud_secure_connection_bundle_n(CassCluster* cluster, const char* path,
                                                            size_t path_length) {
  if (cluster->config().contact_points().empty() && !cluster->config().ssl_context()) {
    SslContextFactory::init_once();
  }
  return cass_cluster_set_cloud_secure_connection_bundle_no_ssl_lib_init_n(cluster, path,
                                                                           path_length);
}

CassError cass_cluster_set_cloud_secure_connection_bundle_no_ssl_lib_init(CassCluster* cluster,
                                                                          const char* path) {
  return cass_cluster_set_cloud_secure_connection_bundle_no_ssl_lib_init_n(cluster, path,
                                                                           SAFE_STRLEN(path));
}

CassError cass_cluster_set_cloud_secure_connection_bundle_no_ssl_lib_init_n(CassCluster* cluster,
                                                                            const char* path,
                                                                            size_t path_length) {
  const AddressVec& contact_points = cluster->config().contact_points();
  const SslContext::Ptr& ssl_context = cluster->config().ssl_context();
  if (!contact_points.empty() || ssl_context) {
    String message;
    if (!cluster->config().contact_points().empty()) {
      message.append("Contact points");
    }
    if (cluster->config().ssl_context()) {
      if (!message.empty()) {
        message.append(" and ");
      }
      message.append("SSL context");
    }
    message.append(" must not be specified with cloud secure connection bundle");
    LOG_ERROR("%s", message.c_str());

    return CASS_ERROR_LIB_BAD_PARAMS;
  }

  if (!cluster->config().set_cloud_secure_connection_bundle(String(path, path_length))) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  return CASS_OK;
}

void cass_cluster_set_application_name(CassCluster* cluster, const char* application_name) {
  cass_cluster_set_application_name_n(cluster, application_name, SAFE_STRLEN(application_name));
}

void cass_cluster_set_application_name_n(CassCluster* cluster, const char* application_name,
                                         size_t application_name_length) {
  cluster->config().set_application_name(String(application_name, application_name_length));
}

void cass_cluster_set_application_version(CassCluster* cluster, const char* application_version) {
  cass_cluster_set_application_version_n(cluster, application_version,
                                         SAFE_STRLEN(application_version));
}

void cass_cluster_set_application_version_n(CassCluster* cluster, const char* application_version,
                                            size_t application_version_length) {
  cluster->config().set_application_version(
      String(application_version, application_version_length));
}

void cass_cluster_set_client_id(CassCluster* cluster, CassUuid client_id) {
  cluster->config().set_client_id(client_id);
}

void cass_cluster_set_monitor_reporting_interval(CassCluster* cluster, unsigned interval_secs) {
  cluster->config().set_monitor_reporting_interval_secs(interval_secs);
}

CassError cass_cluster_set_histogram_refresh_interval(CassCluster* cluster, unsigned refresh_interval) {
  if (refresh_interval <= 0) {
    return CASS_ERROR_LIB_BAD_PARAMS;
  }
  cluster->config().set_cluster_histogram_refresh_interval(refresh_interval);
  return CASS_OK;
}

void cass_cluster_free(CassCluster* cluster) { delete cluster->from(); }

} // extern "C"
