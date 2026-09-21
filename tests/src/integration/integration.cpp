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

#include "integration.hpp"
#include "options.hpp"

#include "driver_info.hpp"
#include "get_time.hpp"
#include "murmur3.hpp"

#include <algorithm>
#include <cstdarg>
#include <iostream>
#include <sys/stat.h>

#define FORMAT_BUFFER_SIZE 10240
#define ENTITY_MAXIMUM_LENGTH 48
#define SIMPLE_KEYSPACE_FORMAT "CREATE KEYSPACE IF NOT EXISTS %s WITH replication = %s"
#define REPLICATION_STRATEGY "{ 'class': %s }"
#define SELECT_SERVER_VERSION "SELECT release_version FROM system.local"
#define LOGGER_MAXIMUM_WAIT_TIME_MS 10000u
#define LOGGER_WAIT_FOR_NAP_MS 100

// Initialize static variables
bool Integration::skipped_message_displayed_ = false;

using namespace datastax::internal;
using namespace ::testing;

Integration::Integration()
    : ccm_(NULL)
    , session_()
    , keyspace_name_("")
    , table_name_("")
    , system_schema_keyspaces_("system.schema_keyspaces")
    , uuid_generator_()
    , server_version_(Options::server_version())
    , number_dc1_nodes_(1)
    , number_dc2_nodes_(0)
    , replication_factor_(0)
    , replication_strategy_("")
    , contact_points_("")
    , is_password_authenticator_(false)
    , is_client_authentication_(false)
    , is_ssl_(false)
    , is_with_vnodes_(false)
    , is_randomized_contact_points_(false)
    , is_schema_metadata_(false)
    , is_ccm_requested_(true)
    , is_ccm_start_requested_(true)
    , is_ccm_start_node_individually_(false)
    , is_session_requested_(true)
    , is_keyspace_change_requested_(true)
    , is_test_chaotic_(false)
    , protocol_version_(CASS_PROTOCOL_VERSION_V4)
    , create_keyspace_query_("")
    , start_time_(0ull) {
  // Determine if the schema keyspaces table should be updated
  // TODO: Make cass_version (and dse_version) available for all tests
  CCM::CassVersion cass_version = server_version_;
  if (!Options::is_cassandra()) {
    cass_version = static_cast<CCM::DseVersion>(cass_version).get_cass_version();
  }
  if (cass_version >= "3.0.0") {
    system_schema_keyspaces_ = "system_schema.keyspaces";
  }

  // Get the name of the test and the case/suite it belongs to
  const TestInfo* test_information = UnitTest::GetInstance()->current_test_info();
  test_name_ = test_information->name();

  // Determine if this is a typed test (e.g. ends in a number)
  const char* type_param = test_information->type_param();
  if (type_param) {
    std::vector<std::string> tokens = explode(test_information->test_case_name(), '/');
    for (std::vector<std::string>::const_iterator iterator = tokens.begin();
         iterator < tokens.end(); ++iterator) {
      std::string token = *iterator;

      // Determine if we are looking at the last token
      if ((iterator + 1) == tokens.end()) {
        size_t number = 0;
        std::stringstream tokenStream(token);
        if (!(tokenStream >> number).fail()) {
          std::vector<std::string> type_param_tokens = explode(type_param, ':');
          size_t size = type_param_tokens.size();
          test_case_name_ += test::Utils::replace_all(type_param_tokens[size - 1], ">", "");
        }
      } else {
        test_case_name_ += token + "_";
      }
    }
  } else {
    test_case_name_ = test_information->test_case_name();
  }

  // Determine if file logging should be enabled for the integration tests
  if (Options::log_tests()) {
    logger_.initialize(test_case_name_, test_name_);
  }
}

Integration::~Integration() {
  try {
    session_.close(false);
  } catch (...) {
  }

  // Reset the skipped message displayed state
  skipped_message_displayed_ = false;
}

void Integration::SetUp() {
  // Initialize the DSE workload (iff not set)
  if (dse_workload_.empty()) {
    dse_workload_.push_back(CCM::DSE_WORKLOAD_CASSANDRA);
  }
  // Generate the default settings for most tests (handles overridden values)
  keyspace_name_ = default_keyspace();
  table_name_ = default_table();

  if (replication_factor_ == 0) {
    replication_factor_ = default_replication_factor();
  }
  replication_strategy_ = default_replication_strategy();

  // Generate the keyspace query
  create_keyspace_query_ =
      format_string(SIMPLE_KEYSPACE_FORMAT, keyspace_name_.c_str(), replication_strategy_.c_str());

  // Create the data center nodes vector
  std::vector<unsigned short> data_center_nodes;
  data_center_nodes.push_back(number_dc1_nodes_);
  data_center_nodes.push_back(number_dc2_nodes_);

  if (is_ccm_requested_) {
    try {
      // Create and start the CCM cluster (if not already created)
      ccm_ = new CCM::Bridge(
          server_version_, Options::use_git(), Options::branch_tag(), Options::use_install_dir(),
          Options::install_dir(), Options::server_type(), dse_workload_, Options::cluster_prefix(),
          Options::dse_credentials(), Options::dse_username(), Options::dse_password(),
          Options::deployment_type(), Options::authentication_type(), Options::host(),
          Options::port(), Options::username(), Options::password(), Options::public_key(),
          Options::private_key(), Options::is_verbose_ccm());
      if (ccm_->create_cluster(data_center_nodes, is_with_vnodes_, is_password_authenticator_,
                               is_ssl_, is_client_authentication_)) {
        if (is_ccm_start_requested_) {
          if (is_ccm_start_node_individually_) {
            for (unsigned short node = 1; node <= (number_dc1_nodes_ + number_dc2_nodes_); ++node) {
              if (is_password_authenticator_) {
                ccm_->start_node(node, "-Dcassandra.superuser_setup_delay_ms=0");
              } else {
                ccm_->start_node(node);
              }
            }
          } else {
            if (is_password_authenticator_) {
              ccm_->start_cluster("-Dcassandra.superuser_setup_delay_ms=0");
            } else {
              ccm_->start_cluster();
            }
          }
        }
      }

      // Generate the default contact points
      contact_points_ =
          generate_contact_points(ccm_->get_ip_prefix(), number_dc1_nodes_ + number_dc2_nodes_);

      // Determine if the session connection should be established
      if (is_session_requested_ && is_ccm_start_requested_) {
        connect();
      }
    } catch (CCM::BridgeException be) {
      // Issue creating the CCM bridge instance (force failure)
      FAIL() << be.what();
    }
  }
}

void Integration::TearDown() {
  // Restart and resume all stopped and paused nodes
  if (!is_test_chaotic_) { // No need to restart as cluster will be destroyed
    for (std::vector<unsigned int>::iterator it = stopped_nodes_.begin(),
                                             end = stopped_nodes_.end();
         it != end; ++it) {
      TEST_LOG("Restarting Node Stopped in " << test_name_ << ": " << *it);
      ccm_->start_node(*it);
    }
    for (std::vector<unsigned int>::iterator it = paused_nodes_.begin(), end = paused_nodes_.end();
         it != end; ++it) {
      TEST_LOG("Resuming Node Paused in " << test_name_ << ": " << *it);
      ccm_->resume_node(*it);
    }
  }
  stopped_nodes_.clear();
  paused_nodes_.clear();

  // Drop keyspace for integration test (may or may have not been created)
  if (!is_test_chaotic_) { // No need to drop keyspace as cluster will be destroyed
    std::stringstream use_keyspace_query;
    use_keyspace_query << "DROP KEYSPACE " << keyspace_name_;
    try {
      session_.execute(use_keyspace_query.str(), CASS_CONSISTENCY_ANY, false, false);
    } catch (...) {
    }
  }

  // Determine if the CCM cluster should be destroyed
  if (is_test_chaotic_) {
    // Destroy the current cluster and reset the chaos flag for the next test
    if (!Options::keep_clusters()) {
      ccm_->remove_cluster();
    } else {
      TEST_LOG_ERROR("Chaotic test detected and keep clusters requested. Cluster will not be "
                     "removed and may result in later failures.");
    }
    is_test_chaotic_ = false;
  }
}

std::string Integration::default_keyspace() {
  if (!keyspace_name_.empty()) {
    return keyspace_name_;
  }

  // Clean up the initial keyspace name (remove category information)
  keyspace_name_ = to_lower(test_case_name_) + "_" + to_lower(test_name_);
  keyspace_name_ = replace_all(keyspace_name_, "tests",
                               ""); // TODO: Rename integration tests (remove 's' or add 's')
  keyspace_name_ = replace_all(keyspace_name_, "test", "");
  keyspace_name_ = replace_all(keyspace_name_, "integration", "");
  for (TestCategory::iterator iterator = TestCategory::begin(); iterator != TestCategory::end();
       ++iterator) {
    keyspace_name_ = replace_all(keyspace_name_, "_" + to_lower(iterator->name()) + "_", "");
  }

  // Generate the keyspace name
  maybe_shrink_name(keyspace_name_);
  return keyspace_name_;
}

unsigned short Integration::default_replication_factor() {
  // Calculate and return the default replication factor
  return (number_dc1_nodes_ % 2 == 0) ? number_dc1_nodes_ / 2 : (number_dc1_nodes_ + 1) / 2;
}

std::string Integration::default_replication_strategy() {
  // Determine the replication strategy
  std::stringstream replication_strategy_s;
  if (number_dc2_nodes_ > 0) {
    replication_strategy_s << "'NetworkTopologyStrategy', 'dc1': " << number_dc1_nodes_ << ", "
                           << "'dc2': " << number_dc2_nodes_;
  } else {
    replication_strategy_s << "'SimpleStrategy', 'replication_factor': ";

    // Ensure the replication factor has not been overridden or already set
    if (replication_factor_ == 0) {
      replication_factor_ = default_replication_factor();
    }
    replication_strategy_s << replication_factor_;
  }

  // Return the default replication strategy
  std::string replication_strategy = replication_strategy_s.str();
  return format_string(REPLICATION_STRATEGY, replication_strategy.c_str());
}

std::string Integration::default_select_all() {
  std::stringstream cql;
  cql << "SELECT * FROM " << default_keyspace() << "." << default_table();
  return cql.str();
}

int64_t Integration::default_select_count() {
  Result result = session_.execute(format_string(SELECT_COUNT_FORMAT, table_name_.c_str()));
  EXPECT_EQ(CASS_OK, result.error_code()) << "Unable to get Row Count: " << result.error_message();
  return result.first_row().next().as<BigInteger>().value();
}

std::string Integration::default_table() {
  if (!table_name_.empty()) {
    return table_name_;
  }

  table_name_ = to_lower(test_name_);
  table_name_ = replace_all(table_name_, "integration_", "");
  maybe_shrink_name(table_name_);
  return table_name_;
}

void Integration::drop_table(const std::string& table_name) {
  // Drop table from the current keyspace
  std::stringstream drop_table_query;
  drop_table_query << "DROP TABLE " << table_name;
  session_.execute(drop_table_query.str(), CASS_CONSISTENCY_ANY, false, false);
}

void Integration::drop_type(const std::string& type_name) {
  // Drop type from the current keyspace
  std::stringstream drop_type_query;
  drop_type_query << "DROP TYPE " << type_name;
  session_.execute(drop_type_query.str(), CASS_CONSISTENCY_ANY, false, false);
}

bool Integration::use_keyspace(const std::string& keyspace_name) {
  std::stringstream use_keyspace_query;
  use_keyspace_query << "USE " << keyspace_name;
  session_.execute(use_keyspace_query.str());
  if (this->HasFailure()) {
    return false;
  }
  return true;
}

void Integration::connect(Cluster cluster) {
  // Establish the session connection
  cluster_ = cluster;
  session_ = cluster.connect();
  CHECK_FAILURE;

  // Update the server version if branch_tag was specified
  if (Options::use_git() && !Options::branch_tag().empty()) {
    if (Options::is_ddac()) {
      FAIL() << "Unable to build DDAC from Branch/Tag";
      return;
    }
    if (Options::is_hcd()) {
      FAIL() << "Unable to build HCD from Branch/Tag";
      return;
    }
    if (Options::is_dse()) {
      server_version_ = ccm_->get_dse_version();
    } else {
      server_version_ = ccm_->get_cassandra_version();
    }
    TEST_LOG("Branch/Tag Option was Used: Retrieved server version is "
             << server_version_.to_string());
  }

  // Create the keyspace for the integration test
  session_.execute(create_keyspace_query_);
  CHECK_FAILURE;

  // Update the session to use the new keyspace by default
  if (is_keyspace_change_requested_) {
    use_keyspace(keyspace_name_);
  }
}

void Integration::connect() {
  // Create the cluster configuration and establish the session connection
  cluster_ = default_cluster();
  connect(cluster_);
}

test::driver::Cluster Integration::default_cluster(bool is_with_default_contact_points /*= true*/) {
  // Create the default cluster object
  Cluster cluster = Cluster::build()
                        .with_randomized_contact_points(is_randomized_contact_points_)
                        .with_schema_metadata(is_schema_metadata_);
  if (is_with_default_contact_points) {
    cluster.with_contact_points(contact_points_);
  }
  cluster.with_protocol_version(protocol_version_);

  // Assign the execution profiles to the cluster object (if available)
  for (ExecutionProfile::Map::iterator it = profiles_.begin(); it != profiles_.end(); ++it) {
    cluster.with_execution_profile(it->first, it->second);
  }
  return cluster;
}

void Integration::enable_cluster_tracing(bool enable /*= true*/) {
  std::vector<std::string> active_nodes = ccm_->cluster_ip_addresses();
  for (std::vector<std::string>::iterator iterator = active_nodes.begin();
       iterator != active_nodes.end(); ++iterator) {
    // Get the node number from the IP address
    std::string node_ip_address = *iterator;
    std::stringstream node_value;
    node_value << node_ip_address[node_ip_address.length() - 1];

    // Enable tracing on the node
    unsigned int node;
    node_value >> node;
    ccm_->enable_node_trace(node);
  }
}

bool Integration::decommission_node(unsigned int node, bool is_force /*= false */) {
  // Decommission the requested node
  bool status = ccm_->decommission_node(node, is_force);
  if (status) {
    // Indicate the test is chaotic
    is_test_chaotic_ = true;
  }
  return status;
}

bool Integration::force_decommission_node(unsigned int node) {
  return decommission_node(node, true);
}

bool Integration::start_node(unsigned int node) {
  // Start the requested node and ensure paused nodes are ignored
  std::vector<unsigned>::iterator it = std::find(paused_nodes_.begin(), paused_nodes_.end(), node);
  if (it == paused_nodes_.end() && ccm_->is_node_down(node, true)) {
    bool status = ccm_->start_node(node);
    it = std::find(stopped_nodes_.begin(), stopped_nodes_.end(), node);
    if (it != stopped_nodes_.end()) {
      stopped_nodes_.erase(it);
    }
    return status;
  }
  return false;
}

bool Integration::stop_node(unsigned int node, bool is_kill /*= false*/) {
  if (ccm_->is_node_up(node, true)) {
    bool status = ccm_->stop_node(node, is_kill);
    if (status) {
      stopped_nodes_.push_back(node);
    }
    return status;
  }
  return false;
}

bool Integration::pause_node(unsigned int node) {
  std::vector<unsigned>::iterator it = std::find(paused_nodes_.begin(), paused_nodes_.end(), node);
  if (it == paused_nodes_.end() && ccm_->is_node_up(node, true)) {
    ccm_->pause_node(node);
    paused_nodes_.push_back(node);
    return true;
  }
  return false;
}

bool Integration::resume_node(unsigned int node) {
  std::vector<unsigned>::iterator it = std::find(paused_nodes_.begin(), paused_nodes_.end(), node);
  if (it != paused_nodes_.end()) {
    ccm_->resume_node(node);
    paused_nodes_.erase(it);
    return true;
  }
  return false;
}

std::string Integration::generate_contact_points(const std::string& ip_prefix,
                                                 size_t number_of_nodes) {
  // Iterate over the total number of nodes to create the contact list
  std::vector<std::string> contact_points;
  for (size_t i = 1; i <= number_of_nodes; ++i) {
    std::stringstream contact_point;
    contact_point << ip_prefix << i;
    contact_points.push_back(contact_point.str());
  }
  return implode(contact_points, ',');
}

std::string Integration::format_string(const char* format, ...) const {
  // Create a buffer for the formatting of the string
  char buffer[FORMAT_BUFFER_SIZE] = { '\0' };

  // Parse the arguments into the buffer
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, FORMAT_BUFFER_SIZE, format, args);
  va_end(args);

  // Return the formatted string
  return buffer;
}

void Integration::maybe_shrink_name(std::string& name) {
  if (name.size() > ENTITY_MAXIMUM_LENGTH) {
    // Update the name with a UUID (first portions of v4 UUID)
    std::vector<std::string> uuid_octets = explode(uuid_generator_.generate_timeuuid().str(), '-');
    std::string id = uuid_octets[0] + uuid_octets[3];
    name = name.substr(0, ENTITY_MAXIMUM_LENGTH - id.size()) + id;
  }
}

bool Integration::wait_for_logger(size_t expected_count) {
  start_timer();
  while (elapsed_time() < LOGGER_MAXIMUM_WAIT_TIME_MS && logger_.count() < expected_count) {
    msleep(LOGGER_WAIT_FOR_NAP_MS);
  }
  return logger_.count() >= expected_count;
}

int64_t Integration::murmur3_hash(const std::string& value) {
  return MurmurHash3_x64_128(value.data(), value.size(), 0);
}

uint64_t Integration::time_since_epoch_in_ms() { return get_time_since_epoch_ms(); }

uint64_t Integration::time_since_epoch_us() { return get_time_since_epoch_us(); }

std::string Integration::driver_name() { return datastax::internal::driver_name(); }

std::string Integration::driver_version() { return datastax::internal::driver_version(); }
