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

#include "options.hpp"
#include "test_utils.hpp"

#include "authentication_type.hpp"
#include "deployment_type.hpp"

#include <algorithm>
#include <iostream>

#define DEFAULT_OPTIONS_CASSSANDRA_VERSION CCM::CassVersion("3.11.6")
#define DEFAULT_OPTIONS_DSE_VERSION CCM::DseVersion("6.9.0")
#define DEFAULT_OPTIONS_DDAC_VERSION CCM::DseVersion("5.1.17")
#define DEFAULT_OPTIONS_HCD_VERSION CCM::DseVersion("1.0.0")

// Initialize the defaults for all the options
bool Options::is_initialized_ = false;
bool Options::is_help_ = false;
bool Options::is_keep_clusters_ = false;
bool Options::is_log_tests_ = true;
CCM::CassVersion Options::server_version_ = DEFAULT_OPTIONS_CASSSANDRA_VERSION;
bool Options::use_git_ = false;
std::string Options::branch_tag_;
bool Options::use_install_dir_ = false;
std::string Options::install_dir_;
std::string Options::cluster_prefix_ = "cpp-driver";
std::string Options::dse_username_;
std::string Options::dse_password_;
std::string Options::host_ = "127.0.0.1";
short Options::port_ = 22;
std::string Options::username_ = "vagrant";
std::string Options::password_ = "vagrant";
std::string Options::public_key_ = "public.key";
std::string Options::private_key_ = "private.key";
bool Options::is_verbose_ccm_ = false;
bool Options::is_verbose_integration_ = false;

// Static initialization is not guaranteed for the following types
CCM::DseCredentialsType Options::dse_credentials_type_;
CCM::AuthenticationType Options::authentication_type_;
CCM::DeploymentType Options::deployment_type_;
std::set<TestCategory> Options::categories_;
CCM::ServerType Options::server_type_;

bool Options::initialize(int argc, char* argv[]) {
  // Only allow initialization to occur once
  if (!is_initialized_) {
    // Initialize values that may not be assigned during static initialization
    dse_credentials_type_ = CCM::DseCredentialsType::USERNAME_PASSWORD;
    authentication_type_ = CCM::AuthenticationType::USERNAME_PASSWORD;
    deployment_type_ = CCM::DeploymentType::LOCAL;
    server_type_ = CCM::ServerType::CASSANDRA;

    // Check for the help argument first (keeps defaults for help display)
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--help") {
        print_help();
        is_help_ = true;
        return false;
      }
    }

    // Check for the DSE argument (update default server version)
    for (int i = 1; i < argc; ++i) {
      if (std::string(argv[i]) == "--dse") {
        server_version_ = DEFAULT_OPTIONS_DSE_VERSION;
      } else if (std::string(argv[i]) == "--ddac") {
        server_version_ = DEFAULT_OPTIONS_DDAC_VERSION;
      } else if (std::string(argv[i]) == "--hcd") {
        server_version_ = DEFAULT_OPTIONS_HCD_VERSION;
      }
    }

    // Iterate through the command line arguments and parse the options
    for (int i = 1; i < argc; ++i) {
      std::vector<std::string> option = test::Utils::explode(argv[i], '=');
      std::string key(test::Utils::to_lower(option[0]));
      std::string value;
      if (option.size() > 1) {
        value = option[1];
      }
      // Integration test options
      if (key == "--keep-clusters") {
        is_keep_clusters_ = true;
      } else if (key == "--log-tests") {
        if (!value.empty()) {
          is_log_tests_ = bool_value(value);
        } else {
          std::cerr << "Missing Log Tests Boolean: Using default " << is_log_tests_ << std::endl;
        }
      }
      // CCM bridge specific options
      else if (key == "--version") {
        if (!value.empty()) {
          server_version_ = CCM::CassVersion(value);
        } else {
          std::cerr << "Missing Server Version: Using default " << server_version_.to_string()
                    << std::endl;
        }
      } else if (key == "--dse") {
        server_type_ = CCM::ServerType::DSE;
      } else if (key == "--ddac") {
        server_type_ = CCM::ServerType::DDAC;
      } else if (key == "--hcd") {
        server_type_ = CCM::ServerType::HCD;
      } else if (key == "--dse-username") {
        if (!value.empty()) {
          dse_username_ = value;
        }
      } else if (key == "--dse-password") {
        if (!value.empty()) {
          dse_password_ = value;
        }
      } else if (key == "--dse-credentials") {
        CCM::DseCredentialsType type = CCM::DseCredentialsType::from_string(value);
        if (type == CCM::DseCredentialsType::INVALID) {
          std::cerr << "Invalid DSE/DDAC Credentials Type: Using default "
                    << dse_credentials_type_.to_string() << std::endl;
        } else {
          dse_credentials_type_ = type;
        }
      } else if (key == "--git") {
        use_git_ = true;
        if (!value.empty()) {
          branch_tag_ = value;
        }
      } else if (key == "--install-dir") {
        use_install_dir_ = true;
        if (value.empty()) {
          std::cerr << "Disabling the Use of the Installation Directory: Installation directory "
                       "must not be empty"
                    << std::endl;
          use_install_dir_ = false;
        } else {
          install_dir_ = value;
        }
      } else if (key == "--prefix") {
        if (!value.empty()) {
          cluster_prefix_ = value;
        } else {
          std::cerr << "Missing Cluster Prefix: Using default " << cluster_prefix_ << std::endl;
        }
      } else if (key == "--category") {
        if (!value.empty()) {
          std::vector<std::string> categories = test::Utils::explode(value, ':');
          for (std::vector<std::string>::iterator iterator = categories.begin();
               iterator != categories.end(); ++iterator) {
            try {
              categories_.insert(*iterator);
            } catch (TestCategory::Exception& tce) {
              std::cerr << "Invalid Category: " << *iterator << " will be ignored"
                        << " (" << tce.what() << ")" << std::endl;
            }
          }
        } else {
          std::cerr << "Missing Category: All applicable tests will run" << std::endl;
        }
      } else if (key == "--verbose") {
        if (!value.empty() && !bool_value(value)) {
          std::vector<std::string> components = test::Utils::explode(value, ',');
          for (std::vector<std::string>::iterator it = components.begin(), end = components.end();
               it != end; ++it) {
            std::string component = test::Utils::to_lower(*it);
            if (component == "ccm") {
              is_verbose_ccm_ = true;
            } else if (component == "integration") {
              is_verbose_integration_ = true;
            } else {
              std::cerr << "Invalid Component \"" << *it
                        << "\": Available components are [ccm, integration]" << std::endl;
            }
          }
        } else {
          is_verbose_ccm_ = true;
          is_verbose_integration_ = true;
        }
      }
#ifdef CASS_USE_LIBSSH2
      else if (key == "--authentication") {
        CCM::AuthenticationType type = CCM::AuthenticationType::from_string(value);
        if (type == CCM::AuthenticationType::INVALID) {
          std::cerr << "Invalid Authentication Type: Using default "
                    << authentication_type_.to_string() << std::endl;
        } else {
          authentication_type_ = type;
        }
      } else if (key == "--deployment") {
        CCM::DeploymentType type = CCM::DeploymentType::from_string(value);
        if (type == CCM::DeploymentType::INVALID) {
          std::cerr << "Invalid Deployment Type: Using default " << deployment_type_.to_string()
                    << std::endl;
        } else {
          deployment_type_ = type;
        }
      } else if (key == "--host") {
        if (!value.empty()) {
          host_ = value;
        } else {
          std::cerr << "Missing Host: Using default " << host_ << std::endl;
        }
      } else if (key == "--port") {
        // Convert the value
        if (!value.empty()) {
          std::stringstream ss(value);
          if (!(ss >> port_).fail()) {
            continue;
          } else {
            std::cerr << "Invalid Port: Using default [" << port_ << "]";
          }
        }
      } else if (key == "--username") {
        if (!value.empty()) {
          username_ = value;
        } else {
          std::cerr << "Missing Username: Using default " << username_ << std::endl;
        }
      } else if (key == "--password") {
        if (!value.empty()) {
          password_ = value;
        } else {
          std::cerr << "Missing Password: Using default " << password_ << std::endl;
        }
      } else if (key == "--public-key") {
        if (!value.empty()) {
          public_key_ = value;
        } else {
          std::cerr << "Missing Public Key Filename: Using default " << public_key_ << std::endl;
        }
      } else if (key == "--private-key") {
        if (!value.empty()) {
          private_key_ = value;
        } else {
          std::cerr << "Missing Private Key Filename: Using default " << private_key_ << std::endl;
        }
      }
#endif
    }

    // Determine if the options should have their defaults reset
    if (categories_.empty()) {
      for (TestCategory::iterator iterator = TestCategory::begin(); iterator != TestCategory::end();
           ++iterator) {
        // Only add the DSE test category if DSE is enabled
        if (*iterator != TestCategory::DSE || is_dse()) {
          categories_.insert(*iterator);
        } else {
          std::cerr << "DSE Category Will be Ignored: DSE is not enabled [--dse]" << std::endl;
        }
      }
    }
    if (deployment_type_ == CCM::DeploymentType::LOCAL) {
      host_ = "127.0.0.1";
    }
    if (!is_cassandra() && !is_hcd() && !use_install_dir_) {
      // Determine if the DSE/DDAC credentials type should be updated
      if (dse_credentials_type_ == CCM::DseCredentialsType::USERNAME_PASSWORD) {
        if (dse_username_.empty() || dse_password_.empty()) {
          std::cerr << "Invalid Username and/or Password: Default to INI_FILE DSE/DDAC credentials"
                    << std::endl;
          dse_credentials_type_ = CCM::DseCredentialsType::INI_FILE;
        }
      }
    }

    is_initialized_ = true;
  }

  // Indicate the arguments were parsed successfully
  return true;
}

void Options::print_help() {
  std::cout << std::endl << "Integration Test Options:" << std::endl;
  std::cout << "  --log-tests=(yes|no)" << std::endl
            << "      "
            << "Enable/Disable logging of driver messages per test to a file." << std::endl
            << "      The default is " << (log_tests() ? "yes" : "no") << "." << std::endl;
  std::cout << std::endl << "CCM Options:" << std::endl;
  std::cout << "  --version=[VERSION]" << std::endl
            << "      "
            << "Cassandra/DSE/DDAC/HCD version to use." << std::endl
            << "      Default:" << std::endl
            << "        Cassandra Version: " << server_version().to_string() << std::endl
            << "        DSE Version: " << DEFAULT_OPTIONS_DSE_VERSION.to_string() << std::endl
            << "        DDAC Version: " << DEFAULT_OPTIONS_DDAC_VERSION.to_string() << std::endl
            << "        HCD Version: " << DEFAULT_OPTIONS_HCD_VERSION.to_string() << std::endl;
  std::string categories;
  for (TestCategory::iterator iterator = TestCategory::begin(); iterator != TestCategory::end();
       ++iterator) {
    if (iterator != TestCategory::begin()) {
      categories += "|";
    }
    categories += iterator->name();
  }
  std::cout << "  --category=[" << categories << "]" << std::endl
            << "      "
            << "Run only the categories whose name matches one of the available" << std::endl
            << "      categories; ':' separates two categories. The default is all categories"
            << std::endl
            << "      being executed." << std::endl;
  std::cout << "  --dse" << std::endl
            << "      "
            << "Indicate server version supplied is DSE." << std::endl;
  std::cout << "  --ddac" << std::endl
            << "      "
            << "Indicate server version supplied is DDAC." << std::endl;
  std::cout << "  --hcd" << std::endl
            << "      "
            << "Indicate server version supplied is HCD." << std::endl;
  std::cout << "  --dse-credentials=(USERNAME_PASSWORD|INI_FILE)" << std::endl
            << "      "
            << "DSE/DDAC credentials to use for download authentication. The default is "
            << std::endl
            << "      " << dse_credentials().to_string() << "." << std::endl;
  std::cout << "  --dse-username=[USERNAME]" << std::endl
            << "      "
            << "Username to use for DSE/DDAC download authentication." << std::endl;
  std::cout << "  --dse-password=[PASSWORD]" << std::endl
            << "      "
            << "Password to use for DSE/DDAC download authentication." << std::endl;
  std::cout << "  --git" << std::endl
            << "      "
            << "Indicate Cassandra/DSE server download should be obtained from" << std::endl
            << "      ASF/GitHub." << std::endl;
  std::cout << "  --git=[BRANCH_OR_TAG]" << std::endl
            << "      "
            << "Indicate Cassandra/DSE server branch/tag should be obtained from" << std::endl
            << "      ASF/GitHub." << std::endl;
  std::cout << "  --install-dir=[INSTALL_DIR]" << std::endl
            << "      "
            << "Indicate Cassandra/DSE installation directory to use." << std::endl;
  std::cout << "  --prefix=[PREFIX]" << std::endl
            << "      "
            << "CCM cluster prefix. The default is " << cluster_prefix() << "." << std::endl;
#ifdef CASS_USE_LIBSSH2
  std::cout << "  --authentication=(USERNAME_PASSWORD|PUBLIC_KEY)" << std::endl
            << "      "
            << "Authentication to use for remote deployment. The default is" << std::endl
            << "      " << authentication_type().to_string() << "." << std::endl;
  std::cout << "  --deployment=(LOCAL|REMOTE)" << std::endl
            << "      "
            << "Deployment to use. The default is " << deployment_type().to_string() << "."
            << std::endl;
  std::cout << "  --host=[IP_ADDRESS]" << std::endl
            << "      "
            << "IP address to use for remote deployment. The default is " << host() << "."
            << std::endl;
  std::cout << "  --port=[PORT]" << std::endl
            << "      "
            << "Port to use for remote deployment. The default is " << port() << "." << std::endl;
  std::cout << "  --username=[USERNAME]" << std::endl
            << "      "
            << "Username to use for remote deployment. The default is " << username() << "."
            << std::endl;
  std::cout << "  --password=[PASSWORD]" << std::endl
            << "      "
            << "Password to use for remote deployment. The default is " << password() << "."
            << std::endl;
  std::cout << "  --public-key=[FILENAME]" << std::endl
            << "      "
            << "Public key filename to use for remote deployment. The default is" << std::endl
            << "      " << public_key() << "." << std::endl;
  std::cout << "  --private-key=[FILENAME]" << std::endl
            << "      "
            << "Private key filename to use for remote deployment. The default is" << std::endl
            << "      " << private_key() << "." << std::endl;
#endif
  std::cout << "  --keep-clusters" << std::endl
            << "      "
            << "Indicate CCM clusters should not be removed after tests terminate." << std::endl;
  std::cout << "  --verbose(=ccm,integration)" << std::endl
            << "      "
            << "Enable verbose output for component(s)." << std::endl;
  std::cout << std::endl;
}

void Options::print_settings() {
  if (keep_clusters()) {
    std::cout << "  Keep clusters" << std::endl;
  }
  if (log_tests()) {
    std::cout << "  Logging driver messages" << std::endl;
  }
  if (!is_cassandra()) {
    std::cout << "  " << server_type_.to_string()
              << " Version: " << CCM::DseVersion(server_version()).to_string() << std::endl;
    if (!use_install_dir() && !is_hcd()) {
      if (dse_credentials() == CCM::DseCredentialsType::USERNAME_PASSWORD) {
        std::cout << "      Username: " << dse_username() << std::endl;
        std::cout << "      Password: " << dse_password() << std::endl;
      } else {
        std::cout << "      Using INI file for DSE/DDAC download authentication" << std::endl;
      }
    }
  } else {
    std::cout << "  " << server_type_.to_string() << " Version: " << server_version().to_string()
              << std::endl;
  }
  if (use_install_dir()) {
    std::cout << "    Using installation directory [" << install_dir() << "]" << std::endl;
  } else {
    if (use_git()) {
      std::cout << "      Using " << (is_dse() ? "GitHub" : "ASF") << " repository" << std::endl;
      if (!branch_tag_.empty()) {
        std::cout << "          Using branch/tag: " << branch_tag_ << std::endl;
      }
    }
  }
  std::cout << "  CCM Cluster Prefix: " << cluster_prefix() << std::endl;
#ifdef CASS_USE_LIBSSH2
  if (deployment_type() == CCM::DeploymentType::REMOTE) {
    std::cout << "  Remote Deployment:" << std::endl;
    std::cout << "      Host: " << host() << std::endl;
    std::cout << "      Port: " << port() << std::endl;
    if (authentication_type() == CCM::AuthenticationType::USERNAME_PASSWORD) {
      std::cout << "      Username: " << username() << std::endl;
      std::cout << "      Password: " << password() << std::endl;
    } else {
      std::cout << "      Public Key Filename: " << public_key() << std::endl;
      std::cout << "      Private Key Filename: " << private_key() << std::endl;
    }
  }
#endif
}

bool Options::is_help() { return is_help_; }

bool Options::keep_clusters() { return is_keep_clusters_; }

bool Options::log_tests() { return is_log_tests_; }

CCM::CassVersion Options::server_version() { return server_version_; }

CCM::ServerType Options::server_type() { return server_type_; }

bool Options::is_cassandra() { return server_type_ == CCM::ServerType::CASSANDRA; }

bool Options::is_dse() { return server_type_ == CCM::ServerType::DSE; }

bool Options::is_ddac() { return server_type_ == CCM::ServerType::DDAC; }

bool Options::is_hcd() { return server_type_ == CCM::ServerType::HCD; }

CCM::DseCredentialsType Options::dse_credentials() {
  // Static initialization cannot be guaranteed
  if (!is_initialized_) {
    return CCM::DseCredentialsType::USERNAME_PASSWORD;
  }
  return dse_credentials_type_;
}

const std::string& Options::dse_username() { return dse_username_; }

const std::string& Options::dse_password() { return dse_password_; }

bool Options::use_git() { return use_git_; }

const std::string& Options::branch_tag() { return branch_tag_; }

bool Options::use_install_dir() { return use_install_dir_; }

const std::string& Options::install_dir() { return install_dir_; }

const std::string& Options::cluster_prefix() { return cluster_prefix_; }

CCM::DeploymentType Options::deployment_type() {
  // Static initialization cannot be guaranteed
  if (!is_initialized_) {
    return CCM::DeploymentType::LOCAL;
  }
  return deployment_type_;
}

CCM::AuthenticationType Options::authentication_type() {
  // Static initialization cannot be guaranteed
  if (!is_initialized_) {
    return CCM::AuthenticationType::USERNAME_PASSWORD;
  }
  return authentication_type_;
}

std::set<TestCategory> Options::categories() { return categories_; }

const std::string& Options::host() { return host_; }

std::string Options::host_prefix() { return host_.substr(0, host_.size() - 1); }

short Options::port() { return port_; }

const std::string& Options::username() { return username_; }

const std::string& Options::password() { return password_; }

const std::string& Options::public_key() { return public_key_; }

const std::string& Options::private_key() { return private_key_; }

SharedPtr<CCM::Bridge, StdDeleter<CCM::Bridge> > Options::ccm() {
  return new CCM::Bridge(Options::server_version(), Options::use_git(), Options::branch_tag(),
                         Options::use_install_dir(), Options::install_dir(), Options::server_type(),
                         CCM::Bridge::DEFAULT_DSE_WORKLOAD, Options::cluster_prefix(),
                         Options::dse_credentials(), Options::dse_username(),
                         Options::dse_password(), Options::deployment_type(),
                         Options::authentication_type(), Options::host(), Options::port(),
                         Options::username(), Options::password(), Options::public_key(),
                         Options::private_key(), Options::is_verbose_ccm());
}

bool Options::is_verbose_ccm() { return is_verbose_ccm_; }

bool Options::is_verbose_integration() { return is_verbose_integration_; }

Options::Options() {}

bool Options::bool_value(const std::string& value) {
  std::string lower_value = test::Utils::to_lower(value);
  if (lower_value == "yes" || lower_value == "true" || lower_value == "on" || lower_value == "0") {
    return true;
  }
  return false;
}
