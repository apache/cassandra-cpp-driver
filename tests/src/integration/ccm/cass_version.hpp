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

#ifndef __CCM_CASS_VERSION_HPP__
#define __CCM_CASS_VERSION_HPP__

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>

namespace CCM {

/**
 * Cassandra release version number
 */
class CassVersion {
public:
  /**
   * Major portion of version number
   */
  unsigned short major_version;
  /**
   * Minor portion of version number
   */
  unsigned short minor_version;
  /**
   * Patch portion of version number
   */
  unsigned short patch_version;
  /**
   * Extra portion of version number
   */
  std::string extra;

  /**
   * Create the CassVersion from a human readable string
   *
   * @param version_string String representation to convert
   */
  CassVersion(const std::string& version_string)
      : major_version(0)
      , minor_version(0)
      , patch_version(0)
      , extra("")
      , ccm_version_(version_string) {
    from_string(version_string);
  }

  /**
   * Compare Cassandra version
   *
   * @param rhs Cassandra version for compare
   * @return -1 if LHS < RHS, 1 if LHS > RHS, and 0 if equal
   */
  int compare(const CassVersion& rhs) {
    if (major_version < rhs.major_version) return -1;
    if (major_version > rhs.major_version) return 1;

    if (minor_version < rhs.minor_version) return -1;
    if (minor_version > rhs.minor_version) return 1;

    if (patch_version < rhs.patch_version) return -1;
    if (patch_version > rhs.patch_version) return 1;

    return 0;
  }

  /**
   * Equal comparison operator overload
   *
   * Determine if a Cassandra version is equal to another Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra version for compare
   * @return True if LHS == RHS; false otherwise
   */
  bool operator==(const CassVersion& rhs) { return compare(rhs) == 0; }

  /**
   * Equal comparison operator overload
   *
   * Determine if a Cassandra version is equal to another Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS == RHS; false otherwise
   */
  bool operator==(const std::string& version) {
    // Check version properties for equality (except extra property)
    return compare(CassVersion(version)) == 0;
  }

  /**
   * Not equal comparison operator overload
   *
   * Determine if a Cassandra version is not equal to another Cassandra
   * version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra version for compare
   * @return True if LHS != RHS; false otherwise
   */
  bool operator!=(const CassVersion& rhs) { return compare(rhs) != 0; }

  /**
   * Not equal comparison operator overload
   *
   * Determine if a Cassandra version is not equal to another Cassandra
   * version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS != RHS; false otherwise
   */
  bool operator!=(const std::string& version) { return compare(CassVersion(version)) != 0; }

  /**
   * Less-than comparison operator overload
   *
   * Determine if a Cassandra version is less-than another Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra Version to compare
   * @return True if LHS < RHS; false otherwise
   */
  bool operator<(const CassVersion& rhs) { return compare(rhs) < 0; }

  /**
   * Less-than comparison operator overload
   *
   * Determine if a Cassandra version is less-than another Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS < RHS; false otherwise
   */
  bool operator<(const std::string& version) { return compare(CassVersion(version)) < 0; }

  /**
   * Greater-than comparison operator overload
   *
   * Determine if a Cassandra version is greater-than another Cassandra
   * version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra Version to compare
   * @return True if LHS > RHS; false otherwise
   */
  bool operator>(const CassVersion& rhs) { return compare(rhs) > 0; }

  /**
   * Greater-than comparison operator overload
   *
   * Determine if a Cassandra version is greater-than another Cassandra
   * version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS > RHS; false otherwise
   */
  bool operator>(const std::string& version) { return compare(CassVersion(version)) > 0; }

  /**
   * Less-than or equal to comparison operator overload
   *
   * Determine if a Cassandra version is less-than or equal to another
   * Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra Version to compare
   * @return True if LHS <= RHS; false otherwise
   */
  bool operator<=(const CassVersion& rhs) { return compare(rhs) <= 0; }

  /**
   * Less-than or equal to comparison operator overload
   *
   * Determine if a Cassandra version is less-than or equal to another
   * Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS <= RHS; false otherwise
   */
  bool operator<=(const std::string& version) { return compare(CassVersion(version)) <= 0; }

  /**
   * Greater-than or equal to comparison operator overload
   *
   * Determine if a Cassandra version is greater-than or equal to another
   * Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param rhs Cassandra Version to compare
   * @return True if LHS >= RHS; false otherwise
   */
  bool operator>=(const CassVersion& rhs) { return compare(rhs) >= 0; }

  /**
   * Greater-than or equal to comparison operator overload
   *
   * Determine if a Cassandra version is greater-than or equal to another
   * Cassandra version
   *
   * NOTE: Extra property is not involved in comparison
   *
   * @param version Cassandra string version for compare
   * @return True if LHS >= RHS; false otherwise
   */
  bool operator>=(const std::string& version) { return compare(CassVersion(version)) >= 0; }

  /**
   * Get the CCM version that was used
   *
   * @return CCM version string
   */
  const std::string& ccm_version() const { return ccm_version_; }

  /**
   * Convert the version into a human readable string
   *
   * @param is_extra_requested True if extra field should be added to version
   *                           string (iff !empty); false to disregard extra
   *                           field (default: true)
   */
  virtual std::string to_string(bool is_extra_requested = true) {
    std::stringstream version_string;
    // Determine if tick-tock release should be handled
    if (*this > "3.0.0" && *this < "3.11.0" && patch_version == 0 && extra.empty()) {
      version_string << major_version << "." << minor_version;
    } else {
      version_string << major_version << "." << minor_version << "." << patch_version;
    }

    // Determine if the extra version information should be added
    if (is_extra_requested && !extra.empty()) {
      version_string << "-" << extra;
    }
    return version_string.str();
  }

private:
  /**
   * CCM version string that was supplied
   */
  std::string ccm_version_;

private:
  /**
   * Convert the version from human readable string to version parameters
   *
   * @param version_string String representation to convert
   */
  void from_string(const std::string& version_string) {
    // Clean up the string for tokens
    std::string version(version_string);
    std::replace(version.begin(), version.end(), '.', ' ');
    std::size_t found = version.find("-");
    if (found != std::string::npos) {
      version.replace(found, 1, " ");
    }

    // Convert to tokens and assign version parameters
    std::istringstream tokens(version);
    if (tokens >> major_version) {
      if (tokens >> minor_version) {
        if (tokens >> patch_version) {
          tokens >> extra;
        }
      }
    }
  }
};

class DseVersion : public CassVersion {
public:
  /**
   * Create the DseVersion from the CassVersion parent type
   *
   * @param version CassVersion to convert to DseVersion type
   */
  DseVersion(CassVersion version)
      : CassVersion(version) {}

  /**
   * Create the DseVersion from a human readable string
   *
   * @param version_string String representation to convert
   */
  DseVersion(const std::string& version_string)
      : CassVersion(version_string) {}

  /**
   * Convert the version into a human readable string
   *
   * @param is_extra_requested True if extra field should be added to version
   *                           string (iff !empty); false to disregard extra
   *                           field (default: true)
   */
  std::string to_string(bool is_extra_requested = true) {
    std::stringstream version_string;
    version_string << major_version << "." << minor_version << "." << patch_version;

    // Determine if the extra version information should be added
    if (is_extra_requested && !extra.empty()) {
      version_string << "-" << extra;
    }
    return version_string.str();
  }

  CassVersion get_cass_version() {
    // Map the HCD/DSE version to the appropriate Cassandra version
    if (*this >= "1.0.0" && *this < "2.0.0") {
      return CassVersion("4.0.0");
    } else if (*this == "4.5.0" || *this == "4.5.1") {
      return CassVersion("2.0.8-39");
    } else if (*this == "4.5.2") {
      return CassVersion("2.0.10-71");
    } else if (*this == "4.5.3") {
      return CassVersion("2.0.11-82");
    } else if (*this == "4.5.4") {
      return CassVersion("2.0.11-92");
    } else if (*this == "4.5.5") {
      return CassVersion("2.0.12-156");
    } else if (*this == "4.5.6") {
      return CassVersion("2.0.12-200");
    } else if (*this == "4.5.7") {
      return CassVersion("2.0.12-201");
    } else if (*this == "4.5.8") {
      return CassVersion("2.0.14-352");
    } else if (*this == "4.5.9") {
      return CassVersion("2.0.16-762");
    } else if (*this == "4.6.0") {
      return CassVersion("2.0.11-83");
    } else if (*this == "4.6.1") {
      return CassVersion("2.0.12-200");
    } else if (*this == "4.6.2") {
      return CassVersion("2.0.12-274");
    } else if (*this == "4.6.3") {
      return CassVersion("2.0.12-275");
    } else if (*this == "4.6.4") {
      return CassVersion("2.0.14-348");
    } else if (*this == "4.6.5") {
      return CassVersion("2.0.14-352");
    } else if (*this == "4.6.6") {
      return CassVersion("2.0.14-425");
    } else if (*this == "4.6.7") {
      return CassVersion("2.0.14-459");
    } else if (*this == "4.6.8" || *this == "4.6.9") {
      return CassVersion("2.0.16-678");
    } else if (*this == "4.6.10") {
      return CassVersion("2.0.16-762");
    } else if (*this == "4.6.11") {
      return CassVersion("2.0.17-858");
    } else if (*this == "4.6.12") {
      return CassVersion("2.0.17-903");
    } else if (*this == "4.6.13") {
      return CassVersion("2.0.17-1420");
    } else if (*this == "4.7.0") {
      return CassVersion("2.1.5-469");
    } else if (*this == "4.7.1" || *this == "4.7.2") {
      return CassVersion("2.1.8-621");
    } else if (*this == "4.7.3") {
      return CassVersion("2.1.8-689");
    } else if (*this == "4.7.4") {
      return CassVersion("2.1.11-872");
    } else if (*this == "4.7.5") {
      return CassVersion("2.1.11-908");
    } else if (*this == "4.7.6") {
      return CassVersion("2.1.11-969");
    } else if (*this == "4.7.7") {
      return CassVersion("2.1.12-1049");
    } else if (*this == "4.7.8") {
      return CassVersion("2.1.13-1218");
    } else if (*this == "4.7.9") {
      return CassVersion("2.1.15-1416");
    } else if (*this == "4.8.0") {
      return CassVersion("2.1.9-791");
    } else if (*this == "4.8.1") {
      return CassVersion("2.1.11-872");
    } else if (*this == "4.8.2") {
      return CassVersion("2.1.11-908");
    } else if (*this == "4.8.3") {
      return CassVersion("2.1.11-969");
    } else if (*this == "4.8.4") {
      return CassVersion("2.1.12-1046");
    } else if (*this == "4.8.5") {
      return CassVersion("2.1.13-1131");
    } else if (*this == "4.8.6") {
      return CassVersion("2.1.13-1218");
    } else if (*this == "4.8.7") {
      return CassVersion("2.1.14-1272");
    } else if (*this == "4.8.8") {
      return CassVersion("2.1.14-1346");
    } else if (*this == "4.8.9") {
      return CassVersion("2.1.15-1403");
    } else if (*this == "4.8.10") {
      return CassVersion("2.1.15-1423");
    } else if (*this == "4.8.11") {
      return CassVersion("2.1.17-1428");
    } else if (*this == "4.8.12") {
      return CassVersion("2.1.17-1439");
    } else if (*this == "4.8.13") {
      return CassVersion("2.1.17-1448");
    } else if (*this == "4.8.14" || *this == "4.8.15") {
      return CassVersion("2.1.18-1463");
    } else if (*this >= "4.8.16" && *this < "5.0.0") {
      if (*this > "4.8.16") {
        std::cerr << "Cassandra Version is not Defined: "
                  << "Add Cassandra version for DSE v" << this->to_string() << std::endl;
      }
      return CassVersion("2.1.19-1484");
    } else if (*this == "5.0.0") {
      return CassVersion("3.0.7.1158");
    } else if (*this == "5.0.1") {
      return CassVersion("3.0.7.1159");
    } else if (*this == "5.0.2") {
      return CassVersion("3.0.8-1293");
    } else if (*this == "5.0.3") {
      return CassVersion("3.0.9-1346");
    } else if (*this == "5.0.4") {
      return CassVersion("3.0.10-1443");
    } else if (*this == "5.0.5") {
      return CassVersion("3.0.11-1485");
    } else if (*this == "5.0.6") {
      return CassVersion("3.0.11-1564");
    } else if (*this == "5.0.7") {
      return CassVersion("3.0.12-1586");
    } else if (*this == "5.0.8") {
      return CassVersion("3.0.12-1656");
    } else if (*this == "5.0.9") {
      return CassVersion("3.0.13-1735");
    } else if (*this == "5.0.10" || *this == "5.0.11") {
      return CassVersion("3.0.14-1862");
    } else if (*this == "5.0.12" || *this == "5.0.13") {
      return CassVersion("3.0.15-2128");
    } else if (*this == "5.0.14") {
      return CassVersion("3.0.15-2269");
    } else if (*this >= "5.0.15" && *this < "5.1.0") {
      if (*this > "5.0.15") {
        std::cerr << "Cassandra Version is not Defined: "
                  << "Add Cassandra version for DSE v" << this->to_string() << std::endl;
      }
      return CassVersion("3.0.16-5015");
    } else if (*this == "5.1.0") {
      return CassVersion("3.10.0-1652");
    } else if (*this == "5.1.1") {
      return CassVersion("3.10.0-1695");
    } else if (*this == "5.1.2") {
      return CassVersion("3.11.0-1758");
    } else if (*this == "5.1.3") {
      return CassVersion("3.11.0-1855");
    } else if (*this == "5.1.4" || *this == "5.1.5") {
      return CassVersion("3.11.0-1900");
    } else if (*this == "5.1.6") {
      return CassVersion("3.11.1-2070");
    } else if (*this == "5.1.7") {
      return CassVersion("3.11.1-2130");
    } else if (*this == "5.1.8" || *this == "5.1.9") {
      return CassVersion("3.11.1-2261");
    } else if (*this == "5.1.10") {
      return CassVersion("3.11.1-2323");
    } else if (*this == "5.1.11") {
      return CassVersion("3.11.2-5111");
    } else if (*this == "5.1.12") {
      return CassVersion("3.11.3-5112");
    } else if (*this == "5.1.13") {
      return CassVersion("3.11.3-5113");
    } else if (*this == "5.1.14") {
      return CassVersion("3.11.3-5114");
    } else if (*this == "5.1.15") {
      return CassVersion("3.11.4-5115");
    } else if (*this == "5.1.16") {
      return CassVersion("3.11.4-5116");
    } else if (*this >= "5.1.17" && *this < "6.0.0") {
      return CassVersion("3.11.4");
    } else if (*this >= "6.0.0" && *this < "6.7.0") {
      return CassVersion(
          "3.11.2-5111"); // Versions before DSE 6.7 erroneously return they support Cassandra 4.0.0
    } else if (*this >= "6.7.0" && *this < "7.0.0") {
      return CassVersion("4.0.0");

      // DSE version does not correspond to a valid Cassandra version
      std::cerr << "Cassandra Version is not Defined: "
                << "Add Cassandra version for DSE v" << this->to_string() << std::endl;
      return CassVersion("0.0.0");
    }

    // DSE version does not correspond to a valid Cassandra version
    return CassVersion("0.0.0");
  }
};

} // namespace CCM

#endif // __CCM_CASS_VERSION_HPP__
