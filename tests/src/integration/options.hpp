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

#ifndef __OPTIONS_HPP__
#define __OPTIONS_HPP__
#include "bridge.hpp"
#include "exception.hpp"
#include "shared_ptr.hpp"
#include "test_category.hpp"

#include <string>

/**
 * Static class for retrieving integration test options parsed from the
 * command line.
 */
class Options {
public:
  /**
   * Initialize/Parse the options from the command line arguments
   *
   * @param argc Number of command line arguments
   * @param argv Command line arguments
   * @return True if settings were parsed correctly; false if --help was used
   *         or there was an issue parsing the command line arguments.
   */
  static bool initialize(int argc, char* argv[]);
  /**
   * Print the help message for the options
   */
  static void print_help();
  /**
   * Print the settings message for the options
   */
  static void print_settings();
  /**
   * Flag to determine if the help flag was indicated
   *
   * @return True if help was indicated; false otherwise
   */
  static bool is_help();
  /**
   * Flag to determine if integration tests should remove the server cluster(s)
   * after completion
   *
   * @return True if clusters should be removed; false otherwise
   */
  static bool keep_clusters();
  /**
   * Flag to determine if integration tests should log the driver logs to a file
   * for each test
   *
   * @return True if tests should be logged to a file; false otherwise
   */
  static bool log_tests();
  /**
   * Get the server version (Cassandra/DSE/DDAC) to use
   *
   * @return Cassandra/DSE/DDAC version to use
   */
  static CCM::CassVersion server_version();
  /**
   * Get the server type (Cassandra/DSE/DDAC)
   *
   * @return Server type
   */
  static CCM::ServerType server_type();
  /**
   * Flag to determine if Cassandra should be used or not
   *
   * @return True if Cassandra should be used; false otherwise
   */
  static bool is_cassandra();
  /**
   * Flag to determine if DSE should be used or not
   *
   * @return True if DSE should be used; false otherwise
   */
  static bool is_dse();
  /**
   * Flag to determine if DDAC should be used or not
   *
   * @return True if DDAC should be used; false otherwise
   */
  static bool is_ddac();
  /**
   * Flag to determine if HCD should be used or not
   *
   * @return True if HCD should be used; false otherwise
   */
  static bool is_hcd();
  /**
   * Get the DSE credentials type (username|password/INI file)
   *
   * @return DSE credentials type
   */
  static CCM::DseCredentialsType dse_credentials();
  /**
   * Get the username for SSH authentication
   *
   * @return Username for remote deployment
   */
  static const std::string& dse_username();
  /**
   * Get the password for SSH authentication
   *
   * @return Password for remote deployment
   */
  static const std::string& dse_password();
  /**
   * Flag to determine if Cassandra/DSE should be built from ASF/GitHub
   *
   * @return True if ASF/GitHub should be used; false otherwise
   */
  static bool use_git();
  /**
   * Get the branch/tag to use for ASF/GitHub
   *
   * @return Branch/Tag
   */
  static const std::string& branch_tag();
  /**
   * Flag to determine if installation directory should be used (Passed to CCM)
   *
   * @return True if installation directory should be used; false otherwise
   */
  static bool use_install_dir();
  /**
   * Get the installation directory to use
   *
   * @return Installation directory
   */
  static const std::string& install_dir();
  /**
   * Get the cluster prefix to use for the CCM clusters (e.g. cpp-driver)
   *
   * @return Cluster prefix
   */
  static const std::string& cluster_prefix();
  /**
   * Get the deployment type (local|remote)
   *
   * @return Deployment type
   */
  static CCM::DeploymentType deployment_type();
  /**
   * Get the authentication type for remote deployment
   *
   * @return Authentication type
   */
  static CCM::AuthenticationType authentication_type();
  /**
   * Get the test categories being applied
   *
   * @return Test categories being applied
   */
  static std::set<TestCategory> categories();
  /**
   * Get the IP address to use when establishing SSH connection for remote CCM
   * command execution and/or IP address to use for server connection IP
   * generation.
   *
   * @return Host to use for server (and SSH connection for remote deployment)
   */
  static const std::string& host();
  /**
   * Get the IP address to use when establishing SSH connection for remote CCM
   * command execution and/or IP address to use for server connection IP
   * generation.
   *
   * @return Host to use for server (and SSH connection for remote deployment)
   */
  /**
   * Get the IP address prefix from the host IP address
   *
   * @return IP address prefix
   */
  static std::string host_prefix();
  /**
   * Get the TCP/IP port for SSH connection
   *
   * @return SSH port to use
   */
  static short port();
  /**
   * Get the username for SSH authentication
   *
   * @return Username for remote deployment
   */
  static const std::string& username();
  /**
   * Get the password for SSH authentication
   *
   * @return Password for remote deployment
   */
  static const std::string& password();
  /**
   * Get the public key for SSH authentication
   *
   * @return Public key for remote deployment
   */
  static const std::string& public_key();
  /**
   * Get the private key for SSH authentication
   *
   * @return Private key for remote deployment
   */
  static const std::string& private_key();
  /**
   * Flag to determine if verbose CCM output should enabled
   *
   * @return True if verbose CCM output should be enabled; false otherwise
   */
  static bool is_verbose_ccm();
  /**
   * Flag to determine if verbose integration output should enabled
   *
   * @return True if verbose integration output should be enabled; false
   *         otherwise
   */
  static bool is_verbose_integration();
  /**
   * Get a CCM instance based on the options
   *
   * @return CCM instance
   */
  static SharedPtr<CCM::Bridge, StdDeleter<CCM::Bridge> > ccm();

private:
  /**
   * Flag to determine if the options have been initialized
   */
  static bool is_initialized_;
  /**
   * Flag to determine if the help flag was indicated
   */
  static bool is_help_;
  /**
   * Flag to indicate if the cluster(s) should remain after tests are completed
   */
  static bool is_keep_clusters_;
  /**
   * Flag to indicate if log messages should be generated for each test
   */
  static bool is_log_tests_;
  /**
   * Server version to use (Cassandra/DSE/DDAC)
   */
  static CCM::CassVersion server_version_;
  /**
   * Server type to use
   */
  static CCM::ServerType server_type_;
  /**
   * Flag to determine if Cassandra should be built from ASF git (github if DSE)
   */
  static bool use_git_;
  /**
   * Branch/Tag name to use if value is present
   */
  static std::string branch_tag_;
  /**
   * Flag to determine if installation directory should be used (passed to CCM)
   */
  static bool use_install_dir_;
  /**
   * Installation directory to use if value is present
   */
  static std::string install_dir_;
  /**
   * Cluster prefix to apply to cluster name during create command
   */
  static std::string cluster_prefix_;
  /**
   * DSE credentials type (username|password/INI file)
   *
   * Flag to indicate how DSE credentials should be obtained
   */
  static CCM::DseCredentialsType dse_credentials_type_;
  /**
   * Username to use when authenticating download access for DSE
   */
  static std::string dse_username_;
  /**
   * Password to use when authenticating download access for DSE
   */
  static std::string dse_password_;
  /**
   * Authentication type (username|password/public key)
   *
   * Flag to indicate how SSH authentication should be established
   */
  static CCM::AuthenticationType authentication_type_;
  /**
   * Deployment type (local|ssh)
   *
   * Flag to indicate how CCM commands should be executed
   */
  static CCM::DeploymentType deployment_type_;
  /**
   * Test types (Cassandra|DSE)
   */
  static std::set<TestCategory> categories_;
  /**
   * IP address to use when establishing SSH connection for remote CCM command
   * execution and/or IP address to use for server connection IP generation
   */
  static std::string host_;
  /**
   * TCP/IP port for SSH connection
   */
  static short port_;
  /**
   * Username for SSH authentication
   */
  static std::string username_;
  /**
   * Password for SSH authentication; Empty if using public key
   */
  static std::string password_;
  /**
   * Public key for authentication; Empty if using username and password
   * authentication
   */
  static std::string public_key_;
  /**
   * Private key for authentication; Empty if using username and password
   * authentication
   */
  static std::string private_key_;
  /**
   * Flag to determine if verbose CCM output should enabled
   */
  static bool is_verbose_ccm_;
  /**
   * Flag to determine if verbose integration output should enabled
   */
  static bool is_verbose_integration_;

  /**
   * Hidden default constructor
   */
  Options();
  /**
   * Get the boolean value of a string
   *
   * @param value Value to convert to boolean
   * @return True if value is yes, true, on, or 0; false otherwise
   */
  static bool bool_value(const std::string& value);
};

#endif // __OPTIONS_HPP__
