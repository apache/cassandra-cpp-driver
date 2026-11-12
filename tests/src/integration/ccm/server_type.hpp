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

#ifndef CCM_SERVER_TYPE_HPP
#define CCM_SERVER_TYPE_HPP

#include <string>

namespace CCM {

/**
 * Server type to indicate which type of server to initialize and start using
 * CCM
 */
class ServerType {
public:
  enum Type { INVALID, CASSANDRA, DSE, DDAC, HCD };

  ServerType(Type type = INVALID)
      : type_(type) {}

  ServerType(const ServerType& server_type)
      : type_(server_type.type_) {}

  const char* name() {
    switch (type_) {
      case CASSANDRA:
        return "CASSANDRA";
      case DSE:
        return "DSE";
      case DDAC:
        return "DDAC";
      case HCD:
        return "HCD";
      default:
        return "INVALID";
    }
  }

  const char* to_string() {
    switch (type_) {
      case CASSANDRA:
        return "Apache Cassandra";
      case DSE:
        return "DataStax Enterprise";
      case DDAC:
        return "DataStax Distribution of Apache Cassandra";
      case HCD:
        return "Hyper-Converged Database";
      default:
        return "Invalid Server Type";
    }
  }

  bool operator==(const ServerType& other) const { return type_ == other.type_; }

  bool operator<(const ServerType& other) const { return type_ < other.type_; }

private:
  Type type_;
};

} // namespace CCM

#endif // CCM_SERVER_TYPE_HPP
