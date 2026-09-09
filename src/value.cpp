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

#include "value.hpp"

#include "collection_iterator.hpp"
#include "data_type.hpp"
#include "external.hpp"
#include "serialization.hpp"

#define CHECK_RESULT(result) \
  if (!(result)) return false;
#define CHECK_VALUE(result)                  \
  do {                                       \
    if ((result)) {                          \
      return CASS_OK;                        \
    } else {                                 \
      return CASS_ERROR_LIB_NOT_ENOUGH_DATA; \
    }                                        \
  } while (0)

using namespace datastax;
using namespace datastax::internal;
using namespace datastax::internal::core;

extern "C" {

const CassDataType* cass_value_data_type(const CassValue* value) {
  return CassDataType::to(value->data_type().get());
}

CassError cass_value_get_int8(const CassValue* value, cass_int8_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_TINY_INT) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_int8(output));
}

CassError cass_value_get_int16(const CassValue* value, cass_int16_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_SMALL_INT) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_int16(output));
}

CassError cass_value_get_int32(const CassValue* value, cass_int32_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_INT) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_int32(output));
}

CassError cass_value_get_uint32(const CassValue* value, cass_uint32_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_DATE) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_uint32(output));
}

CassError cass_value_get_int64(const CassValue* value, cass_int64_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (!is_int64_type(value->value_type())) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_int64(output));
}

CassError cass_value_get_float(const CassValue* value, cass_float_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_FLOAT) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_float(output));
}

CassError cass_value_get_double(const CassValue* value, cass_double_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_DOUBLE) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_double(output));
}

CassError cass_value_get_bool(const CassValue* value, cass_bool_t* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_BOOLEAN) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  bool decode_value = false;
  if (!value->decoder().as_bool(&decode_value)) {
    return CASS_ERROR_LIB_NOT_ENOUGH_DATA;
  }
  *output = decode_value ? cass_true : cass_false;
  return CASS_OK;
}

CassError cass_value_get_uuid(const CassValue* value, CassUuid* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (!is_uuid_type(value->value_type())) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_uuid(output));
}

CassError cass_value_get_inet(const CassValue* value, CassInet* output) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_INET) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  if (!value->decoder().as_inet(value->size(), output)) {
    return CASS_ERROR_LIB_INVALID_DATA;
  }
  return CASS_OK;
}

CassError cass_value_get_string(const CassValue* value, const char** output,
                                size_t* output_length) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  StringRef buffer = value->decoder().as_string_ref();
  *output = buffer.data();
  *output_length = buffer.size();
  return CASS_OK;
}

CassError cass_value_get_bytes(const CassValue* value, const cass_byte_t** output,
                               size_t* output_size) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  StringRef buffer = value->decoder().as_string_ref();
  *output = reinterpret_cast<const cass_byte_t*>(buffer.data());
  *output_size = buffer.size();
  return CASS_OK;
}

CassError cass_value_get_duration(const CassValue* value, cass_int32_t* months, cass_int32_t* days,
                                  cass_int64_t* nanos) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (!cass_value_is_duration(value)) return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  CHECK_VALUE(value->decoder().as_duration(months, days, nanos));
}

CassError cass_value_get_decimal(const CassValue* value, const cass_byte_t** varint,
                                 size_t* varint_size, cass_int32_t* scale) {
  if (value == NULL || value->is_null()) return CASS_ERROR_LIB_NULL_VALUE;
  if (value->value_type() != CASS_VALUE_TYPE_DECIMAL) {
    return CASS_ERROR_LIB_INVALID_VALUE_TYPE;
  }
  CHECK_VALUE(value->decoder().as_decimal(varint, varint_size, scale));
}

CassValueType cass_value_type(const CassValue* value) { return value->value_type(); }

cass_bool_t cass_value_is_null(const CassValue* value) {
  return static_cast<cass_bool_t>(value->is_null());
}

cass_bool_t cass_value_is_collection(const CassValue* value) {
  return static_cast<cass_bool_t>(value->is_collection());
}

cass_bool_t cass_value_is_duration(const CassValue* value) {
  IsValidDataType<CassDuration> is_valid;
  CassDuration dummy(0, 0, 0);
  return static_cast<cass_bool_t>(is_valid(dummy, value->data_type()));
}

size_t cass_value_item_count(const CassValue* collection) { return collection->count(); }

CassValueType cass_value_primary_sub_type(const CassValue* collection) {
  return collection->primary_value_type();
}

CassValueType cass_value_secondary_sub_type(const CassValue* collection) {
  return collection->secondary_value_type();
}

} // extern "C"

Value::Value(const DataType::ConstPtr& data_type, Decoder decoder)
    : data_type_(data_type)
    , count_(0)
    , decoder_(decoder)
    , is_null_(false) {
  assert(!data_type->is_collection());
  if (data_type->is_tuple()) {
    const CompositeType& composite_type = static_cast<const CompositeType&>(*data_type);
    count_ = composite_type.types().size();
  } else if (data_type->is_user_type()) {
    const UserType& user_type = static_cast<const UserType&>(*data_type);
    count_ = user_type.fields().size();
  }
}

bool Value::update(const Decoder& decoder) {
  decoder_ = decoder;
  is_null_ = decoder_.is_null();
  if (!is_null_ && data_type_->is_collection()) {
    return decoder_.decode_int32(count_);
  }
  return true;
}

bool Value::as_bool() const {
  assert(!is_null() && value_type() == CASS_VALUE_TYPE_BOOLEAN);
  bool value = false;
  bool result = decoder_.as_bool(&value);
  UNUSED_(result);
  assert(result);
  return value;
}

int32_t Value::as_int32() const {
  assert(!is_null() && value_type() == CASS_VALUE_TYPE_INT);
  int32_t value = 0;
  bool result = decoder_.as_int32(&value);
  UNUSED_(result);
  assert(result);
  return value;
}

CassUuid Value::as_uuid() const {
  assert(!is_null() &&
         (value_type() == CASS_VALUE_TYPE_UUID || value_type() == CASS_VALUE_TYPE_TIMEUUID));
  CassUuid value = { 0, 0 };
  bool result = decoder_.as_uuid(&value);
  UNUSED_(result);
  assert(result);
  return value;
}

StringVec Value::as_stringlist() const {
  assert(!is_null() &&
         (value_type() == CASS_VALUE_TYPE_LIST || value_type() == CASS_VALUE_TYPE_SET) &&
         primary_value_type() == CASS_VALUE_TYPE_VARCHAR);
  StringVec stringlist;
  CollectionIterator iterator(this);
  while (iterator.next()) {
    stringlist.push_back(iterator.value()->to_string());
  }
  return stringlist;
}
