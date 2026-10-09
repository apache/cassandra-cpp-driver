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

#ifndef __CCM_BRIDGE_HPP__
#define __CCM_BRIDGE_HPP__

#include "authentication_type.hpp"
#include "bridge_exception.hpp"
#include "cass_version.hpp"
#include "deployment_type.hpp"
#include "dse_credentials_type.hpp"
#include "process.hpp"
#include "server_type.hpp"
#include "tsocket.hpp"

#include <map>
#include <string>
#include <vector>

#ifdef CASS_USE_LIBSSH2
// Forward declarations for libssh2
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;
#endif

// Default values
#define DEFAULT_CASSANDRA_VERSION CassVersion("3.11.6")
#define DEFAULT_DSE_VERSION DseVersion("6.7.7")
#define DEFAULT_USE_GIT false
#define DEFAULT_USE_INSTALL_DIR false
#define DEFAULT_SERVER_TYPE ServerType(ServerType::CASSANDRA)
#define DEFAULT_USE_DSE false
#define DEFAULT_USE_DDAC false
#define DEFAULT_CLUSTER_PREFIX "cpp-driver"
#define DEFAULT_DSE_CREDENTIALS DseCredentialsType::USERNAME_PASSWORD
#define DEFAULT_DEPLOYMENT DeploymentType::LOCAL
#define DEFAULT_AUTHENTICATION AuthenticationType::USERNAME_PASSWORD
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_REMOTE_DEPLOYMENT_PORT 22
#define DEFAULT_REMOTE_DEPLOYMENT_USERNAME "vagrant"
#define DEFAULT_REMOTE_DEPLOYMENT_PASSWORD "vagrant"
#define DEFAULT_IS_VERBOSE false
#define DEFAULT_JVM_ARGUMENTS std::vector<std::string>()

// Define the node limit for a cluster
#define CLUSTER_NODE_LIMIT 6

/**
 * Node status information for a cluster
 */
struct ClusterStatus {
  /**
   * List of IPv4 addresses for `DECOMMISSIONED` nodes
   */
  std::vector<std::string> nodes_decommissioned;
  /**
   * List of IPv4 addresses for `DOWN` or unavailable nodes
   */
  std::vector<std::string> nodes_down;
  /**
   * List of IPv4 addresses for `uninitialized` nodes
   */
  std::vector<std::string> nodes_uninitialized;
  /**
   * List of IPv4 addresses for `UP` or ready nodes
   */
  std::vector<std::string> nodes_up;
  /**
   * Total number of nodes in the cluster
   */
  unsigned int node_count;

  /**
   * Constructor
   */
  ClusterStatus()
      : node_count(0) {}
};

namespace CCM {
/**
 * Enumeration for a DSE workload
 */
enum DseWorkload {
  /**
   * Cassandra
   */
  DSE_WORKLOAD_CASSANDRA = 0,
  /**
   * CFS - Cassandra file system (Hadoop Distributed File System (HDFS)
   * replacement
   */
  DSE_WORKLOAD_CFS,
  /**
   * DSEFS - DataStax Enterprise file system (Spark streaming and Write Ahead
   * Logging (WAL))
   */
  DSE_WORKLOAD_DSEFS,
  /**
   * Graph
   */
  DSE_WORKLOAD_GRAPH,
  /**
   * Hadoop
   */
  DSE_WORKLOAD_HADOOP,
  /**
   * Solr
   */
  DSE_WORKLOAD_SOLR,
  /**
   * Spark
   */
  DSE_WORKLOAD_SPARK
};

class Bridge {
public:
  /**
   * Default DSE workload to apply (Cassandra)
   */
  static const std::vector<DseWorkload> DEFAULT_DSE_WORKLOAD;

  /**
   * Constructor
   *
   * @param cassandra_version Cassandra version to use
   *                          (default: DEFAULT_CASSANDRA_VERSION)
   * @param use_git True if version should be obtained from ASF/GitHub; false
   *                otherwise (default: DEFAULT_USE_GIT). Prepends
   *                `cassandra-` to version when creating cluster through CCM
   *                if using Cassandra; otherwise passes version information
   *                to CCM for git download of DSE. Set branch_tag to
   *                override default action.
   * @param branch_tag Branch/Tag to use when use_git is enabled
   *                   (default: Empty). This value is independent of the
   *                   version specified.
   * @param use_install_dir True if CCM should use a particular installation
   *                        directory; false otherwise
   *                        (default: DEAFAULT_USE_INSTALL_DIR)
   * @param install_dir Installation directory to use when use_install_dir is
   *                    enabled (default: Empty)
   * @param server_type Server type CCM should create (default: CASSANDRA)
   * @param dse_workload DSE workload to utilize
   *                     (default: DEFAULT_DSE_WORKLOAD)
   * @param cluster_prefix Prefix to use when creating a cluster name
   *                       (default: DEFAULT_CLUSTER_PREFIX)
   * @param dse_credentials_type Username|Password/INI file credentials
   *                             (default: DEFAULT_DSE_CREDENTIALS)
   * @param dse_username Username for DSE download authentication; empty if
   *                     using INI file credentials (default: Empty)
   * @param dse_password Password for DSE download authentication; empty if
   *                     using INI file credentials (default: Empty)
   * @param deployment_type Local|Remote deployment (execute command locally
   *                        or remote (default: DEFAULT_DEPLOYMENT)
   * @param authentication_type Username|Password/Public key authentication
   *                            (default: DEFAULT_AUTHENTICATION)
   * @param host Host/IP address for CCM cluster and/or SSH connection
   *             (default: DEFAULT_HOST)
   * @param port TCP/IP port for SSH connection
   *             (default: DEFAULT_REMOTE_DEPLOYMENT_PORT)
   * @param username Username for SSH authentication
   *                 (default: DEFAULT_REMOTE_DEPLOYMENT_USERNAME)
   * @param password Password for SSH authentication; Empty if using public
   *                 key (default: DEFAULT_REMOTE_DEPLOYMENT_PASSWORD)
   * @param public_key Public key for authentication; Empty if using username
   *                   and password authentication (default: Empty)
   * @param private_key Private key for authentication; Empty if using
   *                   username and password authentication (default: Empty)
   * @param is_verbose True if verbose output should be enabled; false
   *                   otherwise (default: false)
   * @throws BridgeException
   */
  Bridge(CassVersion cassandra_version = DEFAULT_CASSANDRA_VERSION, bool use_git = DEFAULT_USE_GIT,
         const std::string& branch_tag = "", bool use_install_dir = DEFAULT_USE_INSTALL_DIR,
         const std::string& install_dir = "", ServerType server_type = DEFAULT_SERVER_TYPE,
         std::vector<DseWorkload> dse_workload = DEFAULT_DSE_WORKLOAD,
         const std::string& cluster_prefix = DEFAULT_CLUSTER_PREFIX,
         DseCredentialsType dse_credentials_type = DEFAULT_DSE_CREDENTIALS,
         const std::string& dse_username = "", const std::string& dse_password = "",
         DeploymentType deployment_type = DEFAULT_DEPLOYMENT,
         AuthenticationType authentication_type = DEFAULT_AUTHENTICATION,
         const std::string& host = DEFAULT_HOST, short port = DEFAULT_REMOTE_DEPLOYMENT_PORT,
         const std::string& username = DEFAULT_REMOTE_DEPLOYMENT_USERNAME,
         const std::string& password = DEFAULT_REMOTE_DEPLOYMENT_PASSWORD,
         const std::string& public_key = "", const std::string& private_key = "",
         bool is_verbose = DEFAULT_IS_VERBOSE);

  /**
   * Destructor
   */
  ~Bridge();

  /**
   * Clear the data on the active cluster; as a side effect the cluster is
   * also stopped
   */
  void clear_cluster_data();

  /**
   * Get a comma separated list of IPv4 addresses for nodes in the active
   * Cassandra cluster
   *
   * @param is_all True if all node IPv4 addresses should be returned; false
   *               if only the `UP` nodes (default: true)
   * @return Comma separated list of IPv4 addresses
   */
  std::string cluster_contact_points(bool is_all = true);

  /**
   * Get the list of IPv4 addresses for node in the active Cassandra cluster
   *
   * @param is_all True if all node IPv4 addresses should be returned; false
   *               if only the `UP` nodes (default: true)
   * @return Array/Vector of IPv4 addresses
   */
  std::vector<std::string> cluster_ip_addresses(bool is_all = true);

  /**
   * Get the status for the active cluster
   *
   * @return Status of the nodes in the active cluster
   */
  ClusterStatus cluster_status();

  /**
   * Create a Cassandra cluster with nodes in multiple data centers
   *
   * @param data_center_nodes Vector of data center nodes
   * @param with_vnodes True if vnodes tokens should be used; false otherwise
   *                   (default: false)
   * @param is_password_authenticator True if password authenticator is
   *                                  enabled; false otherwise
   *                                  (default: false)
   * @param is_ssl True if SSL should be enabled; false otherwise
   *               (default: false)
   * @param is_client_authentication True if client authentication should be
   *                                enabled; false otherwise (default: false)
   * @return True if cluster was created or switched; false otherwise
   * @throws BridgeException
   */
  bool create_cluster(std::vector<unsigned short> data_center_nodes, bool with_vnodes = false,
                      bool is_password_authenticator = false, bool is_ssl = false,
                      bool is_client_authentication = false);

  /**
   * Check to see if the active cluster is no longer accepting connections
   *
   * NOTE: This method may check the status of the nodes in the cluster
   *       multiple times
   *
   * @return True if cluster is no longer accepting connections; false
   *    otherwise
   */
  bool is_cluster_down();

  /**
   * Check to see if the active cluster is ready to accept connections
   *
   * NOTE: This method may check the status of the nodes in the cluster
   *       multiple times
   *
   * @return True if cluster is ready to accept connections; false otherwise
   */
  bool is_cluster_up();

  /**
   * "Hang up" the active Cassandra cluster (SIGHUP)
   *
   * @return True if cluster is down; false otherwise
   */
  bool hang_up_cluster();

  /**
   * Kill the active Cassandra cluster (SIGKILL)
   *
   * @return True if cluster is down; false otherwise
   */
  bool kill_cluster();

  /**
   * Remove active cluster
   */
  void remove_cluster();

  /**
   * Remove a cluster
   *
   * @param cluster_name Cluster name to remove
   */
  void remove_cluster(const std::string& cluster_name);

  /**
   * Remove all the available clusters
   * (default deletes generated bridge clusters)
   *
   * @param is_all If true all CCM clusters are removed; otherwise false to
   *               delete bridge generated clusters (default: false)
   */
  void remove_all_clusters(bool is_all = false);

  /**
   * Start the active Cassandra cluster
   *
   * @param jvm_arguments Array/Vector of JVM arguments to apply during
   *                     cluster start
   * @return True if cluster is up; false otherwise
   */
  bool start_cluster(std::vector<std::string> jvm_arguments);

  /**
   * Start the active Cassandra cluster
   *
   * @param jvm_argument JVM argument to apply during cluster start (optional)
   * @return True if cluster is down; false otherwise
   */
  bool start_cluster(std::string jvm_argument = "");

  /**
   * Stop the active Cassandra cluster
   *
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if cluster is down; false otherwise
   */
  bool stop_cluster(bool is_kill = false);

  /**
   * Switch to another available cluster
   *
   * @param cluster_name Cluster name to switch to
   * @return True if switched or is currently active cluster; false otherwise
   */
  bool switch_cluster(const std::string& cluster_name);

  /**
   * Update the cluster configuration
   *
   * @param key_value_pairs Key:Value to update
   * @param is_dse True if key/value pair should update the dse.yaml file;
   *               otherwise false (default: false; update cassandra.yaml)
   * @param is_yaml True if key_value_pairs is a YAML string; otherwise false
   *                (default: false)
   *                NOTE: key_value_pairs in this instance must be a vector
   *                      of strings for use with CCM literal YAML updates.
   *                      These strings are single or multi-line YAML
   *                      configurations; multi-line YAML must contain EOL
   *                      marker at the end of each line (e.g. \n)
   */
  void update_cluster_configuration(std::vector<std::string> key_value_pairs, bool is_dse = false,
                                    bool is_yaml = false);

  /**
   * Update the cluster configuration
   *
   * @param key Key to update
   * @param value Value to apply to key configuration
   * @param is_dse True if key/value pair should update the dse.yaml file;
   *               otherwise false (default: false; update cassandra.yaml)
   */
  void update_cluster_configuration(const std::string& key, const std::string& value,
                                    bool is_dse = false);

  /**
   * Update the cluster configuration using a YAML configuration
   *
   * @param yaml YAML configuration to apply to the cluster
   *             NOTE: These strings are single or multi-line YAML
   *                   configurations; multi-line YAML must contain EOL
   *                   marker at the end of each line (e.g. \n)
   * @param is_dse True if yaml configuration should update the dse.yaml file;
   *               otherwise false (default: false; update cassandra.yaml)
   */
  void update_cluster_configuration_yaml(const std::string& yaml, bool is_dse = false);

  /**
   * Update the node configuration
   *
   * @param node Node to update configuration on
   * @param key_value_pairs Key:Value to update
   */
  void update_node_configuration(unsigned int node, std::vector<std::string> key_value_pairs);

  /**
   * Update the node configuration
   *
   * @param node Node to update configuration on
   * @param key Key to update
   * @param value Value to apply to key configuration
   */
  void update_node_configuration(unsigned int node, const std::string& key,
                                 const std::string& value);

  /**
   * Add a node on the active Cassandra cluster
   *
   * @param data_center If provided add the node to the data center; otherwise
   *                    add node normally (default: no data center)
   * @return Node added to cluster
   * @throws BridgeException
   */
  unsigned int add_node(const std::string& data_center = "");

  /**
   * Bootstrap (add and start) a node on the active cluster
   *
   * @param jvm_arguments JVM arguments to apply during node start
   * @param data_center If provided add the node to the data center; otherwise
   *                    add node normally (default: no data center)
   * @return Node added to cluster
   * @throws BridgeException
   */
  unsigned int bootstrap_node(const std::vector<std::string>& jvm_arguments,
                              const std::string& data_center = "");

  /**
   * Bootstrap (add and start) a node on the active cluster
   *
   * @param jvm_argument JVM argument to apply during node start
   *                     (default: no JVM argument)
   * @param data_center If provided add the node to the data center; otherwise
   *                    add node normally (default: no data center)
   * @return Node added to cluster
   * @throws BridgeException
   */
  unsigned int bootstrap_node(const std::string& jvm_argument = "",
                              const std::string& data_center = "");

  /**
   * Decommission a node on the active Cassandra cluster
   *
   * @param node Node to decommission
   * @oaram is_force True if decommission should be forced; false otherwise
   *                 (default: false)
   * @return True if node was decommissioned; false otherwise
   */
  bool decommission_node(unsigned int node, bool is_force = false);

  /**
   * Disable binary protocol for a node on the active Cassandra cluster
   *
   * @param node Node to disable binary protocol
   */
  void disable_node_binary_protocol(unsigned int node);

  /**
   * Disable gossip for a node on the active Cassandra cluster
   *
   * @param node Node to disable gossip
   */
  void disable_node_gossip(unsigned int node);

  /**
   * Disable trace for a node on the active Cassandra cluster
   *
   * @param node Node to disable tracing
   */
  void disable_node_trace(unsigned int node);

  /**
   * Enable binary protocol for a node on the active Cassandra cluster
   *
   * @param node Node to enable binary protocol
   */
  void enable_node_binary_protocol(unsigned int node);

  /**
   * Enable gossip for a node on the active Cassandra cluster
   *
   * @param node Node to enable gossip
   */
  void enable_node_gossip(unsigned int node);

  /**
   * Enable trace for a node on the active Cassandra cluster
   *
   * @param node Node to enable tracing
   */
  void enable_node_trace(unsigned int node);

  /**
   * Execute a CQL statement on a particular node
   *
   * @param node Node to execute CQL statement on
   * @param cql CQL statement to execute
   */
  void execute_cql_on_node(unsigned int node, const std::string& cql);

  /**
   * Determine if server type is Apache Cassandra
   *
   * @return True if Cassandra; false otherwise
   */
  bool is_cassandra() { return server_type_ == ServerType::CASSANDRA; }

  /**
   * Determine if server type is DataStax Enterprise
   *
   * @return True if DSE; false otherwise
   */
  bool is_dse() { return server_type_ == ServerType::DSE; }

  /**
   * Determine if server type is DataStax Distribution of Apache Cassandra
   *
   * @return True if DDAC; false otherwise
   */
  bool is_ddac() { return server_type_ == ServerType::DDAC; }

  /**
   * Force decommission of a node on the active Cassandra cluster
   *
   * NOTE: Alias for decommission_node(node, true)
   *
   * @param node Node to decommission
   * @return True if node was decommissioned; false otherwise
   */
  bool force_decommission_node(unsigned int node);

  /**
   * "Hang up" a node on the active Cassandra cluster (SIGHUP)
   *
   * @param node Node send SIGHUP signal to
   * @return True if node is down; false otherwise
   */
  bool hang_up_node(unsigned int node);

  /**
   * Kill a node on the active Cassandra cluster (SIGKILL)
   *
   * @param node Node to kill
   * @return True if node is down; false otherwise
   */
  bool kill_node(unsigned int node);

  /**
   * Pause a node on the active Cassandra cluster
   *
   * @param node Node to pause
   */
  void pause_node(unsigned int node);

  /**
   * Resume a node on the active Cassandra cluster
   *
   * @param node Node to resume
   */
  void resume_node(unsigned int node);

  /**
   * Start a node on the active Cassandra cluster
   *
   * @param node Node to start
   * @param jvm_arguments Array/Vector of JVM arguments to apply during node
   *                      start (default: DEFAULT_JVM_ARGUMENTS)
   * @return True if node is up; false otherwise
   */
  bool start_node(unsigned int node,
                  const std::vector<std::string>& jvm_arguments = DEFAULT_JVM_ARGUMENTS);

  /**
   * Start a node on the active Cassandra cluster with an additional JVM
   * argument
   *
   * @param node Node to start
   * @param jvm_argument JVM argument to apply during node start
   * @return True if node is up; false otherwise
   */
  bool start_node(unsigned int node, const std::string& jvm_argument);

  /**
   * Stop a node on the active Cassandra cluster
   *
   * @param node Node to stop
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if node is down; false otherwise
   */
  bool stop_node(unsigned int node, bool is_kill = false);

  /**
   * Get the IP address prefix from the host IP address
   *
   * @return IP address prefix
   */
  std::string get_ip_prefix();

  /**
   * Get the Cassandra version from the active cluster
   *
   * @return Cassandra version
   * @throws BridgeException
   */
  CassVersion get_cassandra_version();

  /**
   * Get the DSE version from the active cluster
   *
   * @return DSE version
   * @throws BridgeException
   */
  DseVersion get_dse_version();

  /**
   * Set the DSE workload on a node
   *
   * NOTE: This operation should be performed before starting the node;
   *       otherwise the node will be stopped and restarted
   *
   * @param node Node to set DSE workload on
   * @param workload Workload to be set
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if node was restarted; false otherwise
   */
  bool set_dse_workload(unsigned int node, DseWorkload workload, bool is_kill = false);

  /**
   * Set the DSE workloads on a node
   *
   * NOTE: This operation should be performed before starting the node;
   *       otherwise the node will be stopped and restarted
   *
   * @param node Node to set DSE workload on
   * @param workloads Workloads to be set
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if node was restarted; false otherwise
   */
  bool set_dse_workloads(unsigned int node, std::vector<DseWorkload> workloads,
                         bool is_kill = false);

  /**
   * Set the DSE workload on the cluster
   *
   * NOTE: This operation should be performed before starting the cluster;
   *       otherwise the cluster will be stopped and restarted
   *
   * @param workload Workload to be set
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if cluster was restarted; false otherwise
   */
  bool set_dse_workload(DseWorkload workload, bool is_kill = false);

  /**
   * Set the DSE workloads on the cluster
   *
   * NOTE: This operation should be performed before starting the cluster;
   *       otherwise the cluster will be stopped and restarted
   *
   * @param workloads Workloads to be set
   * @param is_kill True if forced termination requested; false otherwise
   *                (default: false)
   * @return True if cluster was restarted; false otherwise
   */
  bool set_dse_workloads(std::vector<DseWorkload> workloads, bool is_kill = false);

  /**
   * Check to see if a node has been decommissioned
   *
   * @param node Node to check `DECOMMISSION` status
   * @return True if node is decommissioned; false otherwise
   */
  bool is_node_decommissioned(unsigned int node);

  /**
   * Check to see if a node will no longer accept connections
   *
   * NOTE: This method may check the status of the node multiple times
   *
   * @param node Node to check `DOWN` status
   * @param is_quick_check True if `DOWN` status is checked once; false
   *        otherwise (default: false)
   * @return True if node is no longer accepting connections; false otherwise
   */
  bool is_node_down(unsigned int node, bool is_quick_check = false);

  /**
   * Check to see if a node is ready to accept connections
   *
   * NOTE: This method may check the status of the node multiple times
   *
   * @param node Node to check `UP` status
   * @param is_quick_check True if `UP` status is checked once; false
   *        otherwise (default: false)
   * @return True if node is ready to accept connections; false otherwise
   */
  bool is_node_up(unsigned int node, bool is_quick_check = false);

private:
  /**
   * Cassandra version to use
   */
  CassVersion cassandra_version_;
  /**
   * DSE version to use
   */
  DseVersion dse_version_;
  /**
   * Flag to determine if Cassandra/DSE should be built from ASF/GitHub
   */
  bool use_git_;
  /**
   * Branch/Tag to retrieve from ASF/GitHub
   */
  std::string branch_tag_;
  /**
   * Flag to determine if installation directory should be used (passed to
   * CCM)
   */
  bool use_install_dir_;
  /**
   * Installation directory to pass to CCM
   */
  std::string install_dir_;
  /**
   * Server type to use with CCM
   */
  ServerType server_type_;
  /**
   * Workload to apply to the DSE cluster
   *
   * NOTE: Multiple workloads will be applied simultaneously via CCM
   */
  std::vector<DseWorkload> dse_workload_;
  /**
   * Cluster prefix to apply to cluster name during create command
   */
  std::string cluster_prefix_;
  /**
   * Authentication type (username|password/public key)
   *
   * Flag to indicate how SSH authentication should be established
   */
  AuthenticationType authentication_type_;
  /**
   * DSE credentials type (username|password/INI file)
   *
   * Flag to indicate how DSE credentials should be obtained
   */
  DseCredentialsType dse_credentials_type_;
  /**
   * Username to use when authenticating download access for DSE
   */
  std::string dse_username_;
  /**
   * Password to use when authenticating download access for DSE
   */
  std::string dse_password_;
  /**
   * Deployment type (local|ssh)
   *
   * Flag to indicate how CCM commands should be executed
   */
  DeploymentType deployment_type_;
  /**
   * IP address to use when establishing SSH connection for remote CCM
   * command execution and/or IP address to use for server connection IP
   * generation
   */
  std::string host_;
#ifdef CASS_USE_LIBSSH2
  /**
   * SSH session handle for establishing connection
   */
  LIBSSH2_SESSION* session_;
  /**
   * SSH channel handle for interacting with the session
   */
  LIBSSH2_CHANNEL* channel_;
  /**
   * Socket instance
   */
  Socket* socket_;
#endif
  /**
   * Workload values to use when setting the workload via CCM
   */
  static const std::vector<std::string> dse_workloads_;
  /**
   * Flag to determine if verbose output is enabled
   */
  bool is_verbose_;

#ifdef CASS_USE_LIBSSH2
  /**
   * Initialize the socket
   *
   * @param host Host/IP address for SSH connection
   * @param port TCP/IP port for SSH connection
   * @throws SocketException
   */
  void initialize_socket(const std::string& host, short port);

  /**
   * Synchronize the socket based on the direction of the libssh2 session
   *
   * @throws BridgeException
   * @throws SocketException
   */
  void synchronize_socket();

  /**
   * Initialize the libssh2 connection
   *
   * @throws BridgeException
   */
  void initialize_libssh2();

  /**
   * Establish connection via libssh2
   *
   * @param authentication_type Username|Password/Public key authentication
   * @param username Username for SSH authentication
   * @param password Password for SSH authentication; NULL if using public
   *                 key
   * @param public_key Public key for authentication; Empty if using username
   *                   and password authentication
   * @param private_key Private key for authentication; Empty if using
   *                   username and password authentication
   * @throws BridgeException
   */
  void establish_libssh2_connection(AuthenticationType authentication_type,
                                    const std::string& username, const std::string& password,
                                    const std::string& public_key, const std::string& private_key);

  /**
   * Create/Open the libssh2 terminal
   *
   * @throws BridgeException
   */
  void open_libssh2_terminal();

  /**
   * Terminate/Close the libssh2 terminal
   *
   * @throws BridgeException
   */
  void close_libssh2_terminal();

  /**
   * Execute a remote command on the libssh2 connection
   *
   * @param command Command array to execute ([0] = command, [1-n] arguments)
   * @return Output from executed command
   */
  std::string execute_libssh2_command(const std::vector<std::string>& command);
#endif

#ifdef CASS_USE_LIBSSH2
  /**
   * Read the output (stdout and stderr) from the libssh2 terminal
   *
   * @return Output from the terminal (may not be in order)
   */
  std::string read_libssh2_terminal();

  /**
   * Finalize the libssh2 library usage and socket used by libssh2
   */
  void finalize_libssh2();
#endif

  /**
   * Execute the CCM command
   *
   * @param command Command array to execute ([0] = CCM command,
   *                [1-n] arguments)
   * @return Output from executing CCM command
   */
  std::string execute_ccm_command(const std::vector<std::string>& command);

  /**
   * Get the active Cassandra cluster
   *
   * @return Currently active Cassandra cluster
   */
  std::string get_active_cluster();

  /**
   * Get the list of available Cassandra clusters
   *
   * @return Array/Vector of available Cassandra clusters
   */
  std::vector<std::string> get_available_clusters();

  /**
   * Get the list of available Cassandra clusters
   *
   * @param [out] active_cluster Current active cluster in the list
   * @return Array/Vector of available Cassandra clusters
   */
  std::vector<std::string> get_available_clusters(std::string& active_cluster);

  /**
   * Generate the name of the Cassandra cluster based on the number of nodes
   * in each data center
   *
   * @param cassandra_version Cassandra version being used
   * @param data_center_nodes Vector of nodes for each data center
   * @param with_vnodes True if vnodes are enabled; false otherwise
   * @param is_password_authenticator True if password authenticator is
   *                                  enabled; false otherwise
   * @param is_ssl True if SSL is enabled; false otherwise
   * @param is_client_authentication True if client authentication is enabled;
   *                                false otherwise
   * @return Cluster name
   */
  std::string generate_cluster_name(CassVersion cassandra_version,
                                    std::vector<unsigned short> data_center_nodes, bool with_vnodes,
                                    bool is_password_authenticator, bool is_ssl,
                                    bool is_client_authentication);

  /**
   * Generate the nodes parameter for theCassandra cluster based on the number
   * of nodes in each data center
   *
   * @param data_center_nodes Vector of nodes for each data center
   * @param separator Separator to use between cluster nodes
   * @return String of nodes separated by separator
   */
  std::string generate_cluster_nodes(std::vector<unsigned short> data_center_nodes,
                                     char separator = ':');

  /**
   * Generate the CCM update configuration command based on the Cassandra
   * version requested
   *
   * @param cassandra_version Cassandra version to use
   * @return Array/Vector containing the updateconf command
   */
  std::vector<std::string> generate_create_updateconf_command(CassVersion cassandra_version);

  /**
   * Translate a cassandra.yaml "key:value" update pair to the format Cassandra 4.1
   * introduced (ignoring nested keys)
   *
   * Transformations include:
   * - "_in_*"/"_*_per_sec" suffix -> appending "*[/s]" to the value
   * - "enable_" prefix -> "_enabled" suffix
   *
   * @param key_value Original "key:value" pair
   * @param cassandra_version Cassandra version being used
   * @return The pair translated if renamed and cassandra_version >= 4.1.0;
   *         otherwise key_value unchanged
   */
  std::string translate_config_for_version(const std::string& key_value,
                                           CassVersion cassandra_version);

  /**
   * Generate the command separated list for have a single or multiple
   * workloads for the CCM setworkload command
   *
   * @param workloads Workloads to be set
   * @return String representing the workloads for the setworkload command
   */
  std::string generate_dse_workloads(std::vector<DseWorkload> workloads);

  /**
   * Get the next available node
   *
   * @return Next available node
   * @throws BridgeException
   * @see CLUSTER_NODE_LIMIT
   */
  unsigned int get_next_available_node();

  /**
   * Generate the node name based on the node requested
   *
   * @param node Node to use
   * @return Name of the node for CCM node commands
   */
  std::string generate_node_name(unsigned int node);

  /**
   * Determine if a node is available
   *
   * @param node Cassandra node to check
   * @return True if node is available; false otherwise
   */
  bool is_node_availabe(unsigned int node);

  /**
   * Determine if a node is available
   *
   * @param ip_address IPv4 address of the Cassandra node
   * @return True if node is available; false otherwise
   */
  bool is_node_availabe(const std::string& ip_address);

  /**
   * Convert a string to lowercase
   *
   * @param input String to convert to lowercase
   *
   * TODO: Remove static declaration after deprecations are removed
   */
  static std::string to_lower(const std::string& input);

  /**
   * Remove the leading and trailing whitespace from a string
   *
   * @param input String to trim
   * @return Trimmed string
   *
   * TODO: Remove static declaration after deprecations are removed
   */
  static std::string trim(const std::string& input);

  /**
   * Concatenate an array/vector into a string
   *
   * @param elements Array/Vector elements to concatenate
   * @param delimiter Character to use between elements (default: <space>)
   * @return A string representation of all the array/vector elements
   */
  std::string implode(const std::vector<std::string>& elements, const char delimiter = ' ');

  /**
   * Split a string into an array/vector
   *
   * @param input String to convert to array/vector
   * @param delimiter Character to use split into elements (default: <space>)
   * @return An array/vector representation of the string
   *
   * TODO: Remove static declaration after deprecations are removed
   */
  static std::vector<std::string> explode(const std::string& input, const char delimiter = ' ');

  /**
   * Cross platform millisecond granularity sleep
   *
   * @param milliseconds Time in milliseconds to sleep
   */
  void msleep(unsigned int milliseconds);
};
} // namespace CCM

#endif // __CCM_BRIDGE_HPP__
