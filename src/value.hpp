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

#ifndef DATASTAX_INTERNAL_VALUE_HPP
#define DATASTAX_INTERNAL_VALUE_HPP

#include "cassandra.h"
#include "decoder.hpp"
#include "external.hpp"
#include "result_metadata.hpp"
#include "string_ref.hpp"

namespace datastax { namespace internal { namespace core {

class Value {
public:
  Value()
      : count_(0)
      , is_null_(false) {}

  // Used for "null" values
  Value(const DataType::ConstPtr& data_type)
      : data_type_(data_type)
      , count_(0)
      , is_null_(true) {}

  // Used for regular values, tuples, and UDTs
  Value(const DataType::ConstPtr& data_type, Decoder decoder);

  // Used for collections and schema metadata collections (converted from JSON)
  Value(const DataType::ConstPtr& data_type, int32_t count, Decoder decoder)
      : data_type_(data_type)
      , count_(count)
      , decoder_(decoder)
      , is_null_(false) {}

  Decoder decoder() const { return decoder_; }
  ProtocolVersion protocol_version() const { return decoder_.protocol_version(); }
  int64_t size() const { return (is_null_ ? -1 : decoder_.remaining()); }

  bool is_valid() const { return !!data_type_; }

  CassValueType value_type() const {
    if (!data_type_) {
      return CASS_VALUE_TYPE_UNKNOWN;
    }
    return data_type_->value_type();
  }

  const DataType::ConstPtr& data_type() const { return data_type_; }

  CassValueType primary_value_type() const {
    const DataType::ConstPtr& primary(primary_data_type());
    if (!primary) {
      return CASS_VALUE_TYPE_UNKNOWN;
    }
    return primary->value_type();
  }

  const DataType::ConstPtr& primary_data_type() const {
    if (!data_type_ || !data_type_->is_collection()) {
      return DataType::NIL;
    }
    const CollectionType::ConstPtr& collection_type(data_type_);
    if (collection_type->types().size() < 1) {
      return DataType::NIL;
    }
    return collection_type->types()[0];
  }

  CassValueType secondary_value_type() const {
    const DataType::ConstPtr& secondary(secondary_data_type());
    if (!secondary) {
      return CASS_VALUE_TYPE_UNKNOWN;
    }
    return secondary->value_type();
  }

  const DataType::ConstPtr& secondary_data_type() const {
    if (!data_type_ || !data_type_->is_map()) {
      return DataType::NIL;
    }
    const CollectionType::ConstPtr& collection_type(data_type_);
    if (collection_type->types().size() < 2) {
      return DataType::NIL;
    }
    return collection_type->types()[1];
  }

  bool is_null() const { return is_null_; }

  bool is_collection() const { return data_type_ && data_type_->is_collection(); }

  bool is_map() const { return data_type_ && data_type_->is_map(); }

  bool is_tuple() const { return data_type_ && data_type_->is_tuple(); }

  bool is_user_type() const { return data_type_ && data_type_->is_user_type(); }

  int32_t count() const { return count_ * !is_null_; }

  StringRef to_string_ref() const {
    if (is_null()) return StringRef();
    return decoder_.as_string_ref();
  }

  String to_string() const { return to_string_ref().to_string(); }

  bool as_bool() const;
  int32_t as_int32() const;
  CassUuid as_uuid() const;
  StringVec as_stringlist() const;

private:
  friend class Decoder;
  bool update(const Decoder& decoder);

private:
  DataType::ConstPtr data_type_;
  int32_t count_;
  Decoder decoder_;
  bool is_null_;
};

typedef Vector<Value> OutputValueVec;

}}} // namespace datastax::internal::core

EXTERNAL_TYPE(datastax::internal::core::Value, CassValue)

#endif
