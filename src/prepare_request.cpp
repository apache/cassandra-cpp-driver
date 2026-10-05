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

#include "prepare_request.hpp"

#include "protocol.hpp"
#include "serialization.hpp"
#include "utils.hpp"

using namespace datastax::internal::core;

int PrepareRequest::encode(ProtocolVersion version, RequestCallback* callback,
                           BufferVec* bufs) const {
  // <query> [long string]
  size_t length = sizeof(int32_t) + query_.size();
  bufs->push_back(Buffer(length));
  bufs->back().encode_long_string(0, query_.data(), query().size());

  if (version.supports_set_keyspace()) {
    // <flags> [int] [<keyspace> [string]]
    int32_t flags = 0;
    size_t flags_keyspace_buf_size = sizeof(int32_t); // <flags> [int]
    // v5 carries the keyspace name itself, not a CQL identifier, so any quoting
    // the caller supplied has to be removed.
    String keyspace(unescape_id(this->keyspace()));

    if (!keyspace.empty()) {
      flags |= CASS_PREPARE_FLAG_WITH_KEYSPACE;
      flags_keyspace_buf_size += sizeof(uint16_t) + keyspace.size(); // <keyspace> [string]
    }

    bufs->push_back(Buffer(flags_keyspace_buf_size));
    length += flags_keyspace_buf_size;

    Buffer& buf = bufs->back();
    size_t pos = buf.encode_int32(0, flags);

    if (!keyspace.empty()) {
      buf.encode_string(pos, keyspace.data(), static_cast<uint16_t>(keyspace.size()));
    }
  }
  return length;
}
