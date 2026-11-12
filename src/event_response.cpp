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

#include "event_response.hpp"

#include "serialization.hpp"

using namespace datastax::internal::core;

bool EventResponse::decode(Decoder& decoder) {
  decoder.set_type("event");
  StringRef event_type;
  CHECK_RESULT(decoder.decode_string(&event_type));

  if (event_type == "TOPOLOGY_CHANGE") {
    event_type_ = CASS_EVENT_TOPOLOGY_CHANGE;

    StringRef topology_change;
    CHECK_RESULT(decoder.decode_string(&topology_change));
    if (topology_change == "NEW_NODE") {
      topology_change_ = NEW_NODE;
    } else if (topology_change == "REMOVED_NODE") {
      topology_change_ = REMOVED_NODE;
    } else if (topology_change == "MOVED_NODE") {
      topology_change_ = MOVED_NODE;
    } else {
      return false;
    }
    CHECK_RESULT(decoder.decode_inet(&affected_node_));
  } else if (event_type == "STATUS_CHANGE") {
    event_type_ = CASS_EVENT_STATUS_CHANGE;

    StringRef status_change;
    CHECK_RESULT(decoder.decode_string(&status_change));
    if (status_change == "UP") {
      status_change_ = UP;
    } else if (status_change == "DOWN") {
      status_change_ = DOWN;
    } else {
      return false;
    }
    CHECK_RESULT(decoder.decode_inet(&affected_node_));
  } else if (event_type == "GRACEFUL_DISCONNECT") {
    // The GRACEFUL_DISCONNECT event (CEP-59) has no body; the type string is
    // enough.
    event_type_ = CASS_EVENT_GRACEFUL_DISCONNECT;
  } else if (event_type == "SCHEMA_CHANGE") {
    event_type_ = CASS_EVENT_SCHEMA_CHANGE;

    // Version 1+: <change>... (all start with change type)
    StringRef schema_change;
    CHECK_RESULT(decoder.decode_string(&schema_change));
    if (schema_change == "CREATED") {
      schema_change_ = CREATED;
    } else if (schema_change == "UPDATED") {
      schema_change_ = UPDATED;
    } else if (schema_change == "DROPPED") {
      schema_change_ = DROPPED;
    } else {
      return false;
    }

    // Version 3+: ...<target><options>
    // <target> = [string]
    // <options> = [string] OR [string][string]

    StringRef target;
    CHECK_RESULT(decoder.decode_string(&target));
    if (target == "KEYSPACE") {
      schema_change_target_ = KEYSPACE;
    } else if (target == "TABLE") {
      schema_change_target_ = TABLE;
    } else if (target == "TYPE") {
      schema_change_target_ = TYPE;
    } else if (target == "FUNCTION") {
      schema_change_target_ = FUNCTION;
    } else if (target == "AGGREGATE") {
      schema_change_target_ = AGGREGATE;
    } else {
      return false;
    }

    CHECK_RESULT(decoder.decode_string(&keyspace_));

    if (schema_change_target_ == TABLE || schema_change_target_ == TYPE) {
      CHECK_RESULT(decoder.decode_string(&target_));
    } else if (schema_change_target_ == FUNCTION || schema_change_target_ == AGGREGATE) {
      CHECK_RESULT(decoder.decode_string(&target_));
      CHECK_RESULT(decoder.decode_stringlist(arg_types_));
    }
  } else {
    return false;
  }

  decoder.maybe_log_remaining();
  return true;
}
