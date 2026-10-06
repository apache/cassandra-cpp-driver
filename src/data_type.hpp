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

#ifndef DATASTAX_INTERNAL_DATA_TYPE_HPP
#define DATASTAX_INTERNAL_DATA_TYPE_HPP

#include "cassandra.h"
#include "external.hpp"
#include "hash_table.hpp"
#include "macros.hpp"
#include "map.hpp"
#include "ref_counted.hpp"
#include "small_dense_hash_map.hpp"
#include "string.hpp"
#include "types.hpp"
#include "vector.hpp"

#include <algorithm>

namespace datastax { namespace internal { namespace core {

class Collection;
class Tuple;
class UserTypeValue;

inline bool is_int64_type(CassValueType value_type) {
  return value_type == CASS_VALUE_TYPE_BIGINT || value_type == CASS_VALUE_TYPE_COUNTER ||
         value_type == CASS_VALUE_TYPE_TIMESTAMP || value_type == CASS_VALUE_TYPE_TIME;
}

inline bool is_string_type(CassValueType value_type) {
  return value_type == CASS_VALUE_TYPE_ASCII || value_type == CASS_VALUE_TYPE_TEXT ||
         value_type == CASS_VALUE_TYPE_VARCHAR;
}

inline bool is_bytes_type(CassValueType value_type) {
  return value_type == CASS_VALUE_TYPE_BLOB || value_type == CASS_VALUE_TYPE_VARINT ||
         value_type == CASS_VALUE_TYPE_CUSTOM;
}

inline bool is_uuid_type(CassValueType value_type) {
  return value_type == CASS_VALUE_TYPE_TIMEUUID || value_type == CASS_VALUE_TYPE_UUID;
}

// Only compare when both arguments are not empty
inline bool equals_both_not_empty(const String& s1, const String& s2) {
  return s1.empty() || s2.empty() || s1 == s2;
}

class DataType : public RefCounted<DataType> {
public:
  typedef SharedRefPtr<DataType> Ptr;
  typedef SharedRefPtr<const DataType> ConstPtr;
  typedef Vector<ConstPtr> Vec;

  static const DataType::ConstPtr NIL;

  static ConstPtr create_by_class(StringRef name);
  static ConstPtr create_by_cql(StringRef name);

  DataType(CassValueType value_type = CASS_VALUE_TYPE_UNKNOWN, bool is_frozen = false)
      : value_type_(value_type)
      , is_frozen_(is_frozen) {}

  virtual ~DataType() {}

  CassValueType value_type() const { return value_type_; }

  bool is_collection() const {
    return value_type_ == CASS_VALUE_TYPE_LIST || value_type_ == CASS_VALUE_TYPE_MAP ||
           value_type_ == CASS_VALUE_TYPE_SET;
  }

  bool is_map() const { return value_type_ == CASS_VALUE_TYPE_MAP; }
  bool is_tuple() const { return value_type_ == CASS_VALUE_TYPE_TUPLE; }
  bool is_user_type() const { return value_type_ == CASS_VALUE_TYPE_UDT; }
  bool is_custom() const { return value_type_ == CASS_VALUE_TYPE_CUSTOM; }

  bool is_frozen() const { return is_frozen_; }

  virtual bool equals(const DataType::ConstPtr& data_type) const {
    switch (value_type_) {
      // "text" is an alias for  "varchar"
      case CASS_VALUE_TYPE_TEXT:
      case CASS_VALUE_TYPE_VARCHAR:
        return data_type->value_type_ == CASS_VALUE_TYPE_TEXT ||
               data_type->value_type_ == CASS_VALUE_TYPE_VARCHAR;
      default:
        return value_type_ == data_type->value_type_;
    }
  }

  virtual DataType::Ptr copy() const { return Ptr(new DataType(value_type_)); }

  virtual String to_string() const {
    switch (value_type_) {
#define XX_VALUE_TYPE(name, type, cql, klass) \
  case name:                                  \
    return cql;
      CASS_VALUE_TYPE_MAPPING(XX_VALUE_TYPE)
#undef XX_VALUE_TYPE
      default:
        return "";
    }
  }

private:
  CassValueType value_type_;
  bool is_frozen_;

private:
  DISALLOW_COPY_AND_ASSIGN(DataType);
};

class CustomType : public DataType {
public:
  typedef SharedRefPtr<const CustomType> ConstPtr;

  CustomType()
      : DataType(CASS_VALUE_TYPE_CUSTOM) {}

  CustomType(const String& class_name)
      : DataType(CASS_VALUE_TYPE_CUSTOM)
      , class_name_(class_name) {}

  const String& class_name() const { return class_name_; }

  void set_class_name(const String& class_name) { class_name_ = class_name; }

  virtual bool equals(const DataType::ConstPtr& data_type) const {
    assert(value_type() == CASS_VALUE_TYPE_CUSTOM);
    if (data_type->value_type() != CASS_VALUE_TYPE_CUSTOM) {
      return false;
    }
    const ConstPtr& custom_type(data_type);
    return equals_both_not_empty(class_name_, custom_type->class_name_);
  }

  virtual DataType::Ptr copy() const { return DataType::Ptr(new CustomType(class_name_)); }

  virtual String to_string() const { return class_name_; }

private:
  String class_name_;
};

class CompositeType : public DataType {
public:
  CompositeType(CassValueType type, bool is_frozen)
      : DataType(type, is_frozen) {}

  CompositeType(CassValueType type, const DataType::Vec& types, bool is_frozen)
      : DataType(type, is_frozen)
      , types_(types) {}

  DataType::Vec& types() { return types_; }
  const DataType::Vec& types() const { return types_; }

  virtual String to_string() const {
    String str;
    if (is_frozen()) str.append("frozen<");
    str.append(DataType::to_string());
    str.push_back('<');
    for (DataType::Vec::const_iterator i = types_.begin(), end = types_.end(); i != end; ++i) {
      if (i != types_.begin()) str.append(", ");
      str.append((*i)->to_string());
    }
    if (is_frozen()) {
      str.append(">>");
    } else {
      str.push_back('>');
    }
    return str;
  }

protected:
  DataType::Vec types_;
};

class CollectionType : public CompositeType {
public:
  typedef SharedRefPtr<const CollectionType> ConstPtr;

  explicit CollectionType(CassValueType collection_type, bool is_frozen)
      : CompositeType(collection_type, is_frozen) {}

  explicit CollectionType(CassValueType collection_type, size_t types_count, bool is_frozen)
      : CompositeType(collection_type, is_frozen) {
    types_.reserve(types_count);
  }

  explicit CollectionType(CassValueType collection_type, const DataType::Vec& types, bool is_frozen)
      : CompositeType(collection_type, types, is_frozen) {}

  virtual bool equals(const DataType::ConstPtr& data_type) const {
    assert(value_type() == CASS_VALUE_TYPE_LIST || value_type() == CASS_VALUE_TYPE_SET ||
           value_type() == CASS_VALUE_TYPE_MAP || value_type() == CASS_VALUE_TYPE_TUPLE);

    if (value_type() != data_type->value_type()) {
      return false;
    }

    const CollectionType::ConstPtr& collection_type(data_type);

    // Only compare sub-types if both have sub-types
    if (!types_.empty() && !collection_type->types_.empty()) {
      if (types_.size() != collection_type->types_.size()) {
        return false;
      }
      for (size_t i = 0; i < types_.size(); ++i) {
        if (!types_[i]->equals(collection_type->types_[i])) {
          return false;
        }
      }
    }

    return true;
  }

  virtual DataType::Ptr copy() const {
    return DataType::Ptr(new CollectionType(value_type(), types_, is_frozen()));
  }

public:
  static DataType::ConstPtr list(DataType::ConstPtr element_type, bool is_frozen) {
    DataType::Vec types;
    types.push_back(element_type);
    return DataType::ConstPtr(new CollectionType(CASS_VALUE_TYPE_LIST, types, is_frozen));
  }

  static DataType::ConstPtr set(DataType::ConstPtr element_type, bool is_frozen) {
    DataType::Vec types;
    types.push_back(element_type);
    return DataType::ConstPtr(new CollectionType(CASS_VALUE_TYPE_SET, types, is_frozen));
  }

  static DataType::ConstPtr map(DataType::ConstPtr key_type, DataType::ConstPtr value_type,
                                bool is_frozen) {
    DataType::Vec types;
    types.push_back(key_type);
    types.push_back(value_type);
    return DataType::ConstPtr(new CollectionType(CASS_VALUE_TYPE_MAP, types, is_frozen));
  }
};

class TupleType : public CompositeType {
public:
  typedef SharedRefPtr<const TupleType> ConstPtr;

  TupleType(bool is_frozen)
      : CompositeType(CASS_VALUE_TYPE_TUPLE, is_frozen) {}

  TupleType(const DataType::Vec& types, bool is_frozen)
      : CompositeType(CASS_VALUE_TYPE_TUPLE, types, is_frozen) {}

  virtual bool equals(const DataType::ConstPtr& data_type) const {
    assert(value_type() == CASS_VALUE_TYPE_TUPLE);

    if (value_type() != data_type->value_type()) {
      return false;
    }

    const ConstPtr& tuple_type(data_type);

    // Only compare sub-types if both have sub-types
    if (!types_.empty() && !tuple_type->types_.empty()) {
      if (types_.size() != tuple_type->types_.size()) {
        return false;
      }
      for (size_t i = 0; i < types_.size(); ++i) {
        if (!types_[i]->equals(tuple_type->types_[i])) {
          return false;
        }
      }
    }

    return true;
  }

  virtual DataType::Ptr copy() const { return DataType::Ptr(new TupleType(types_, is_frozen())); }
};

class UserType : public DataType {
  friend class Memory;

public:
  typedef SharedRefPtr<UserType> Ptr;
  typedef SharedRefPtr<const UserType> ConstPtr;
  typedef internal::Map<String, UserType::Ptr> Map;

  struct Field : public HashTableEntry<Field> {
    Field(const String& field_name, const DataType::ConstPtr& type)
        : name(field_name)
        , type(type) {}

    String name;
    DataType::ConstPtr type;
  };

  typedef CaseInsensitiveHashTable<Field>::EntryVec FieldVec;

  explicit UserType(bool is_frozen)
      : DataType(CASS_VALUE_TYPE_UDT, is_frozen) {}

  explicit UserType(size_t field_count, bool is_frozen)
      : DataType(CASS_VALUE_TYPE_UDT, is_frozen)
      , fields_(field_count) {}

  explicit UserType(const String& keyspace, const String& type_name, bool is_frozen)
      : DataType(CASS_VALUE_TYPE_UDT, is_frozen)
      , keyspace_(keyspace)
      , type_name_(type_name) {}

  explicit UserType(const String& keyspace, const String& type_name, const FieldVec& fields,
                    bool is_frozen)
      : DataType(CASS_VALUE_TYPE_UDT, is_frozen)
      , keyspace_(keyspace)
      , type_name_(type_name)
      , fields_(fields) {}

  const String& keyspace() const { return keyspace_; }

  void set_keyspace(const String& keyspace) { keyspace_ = keyspace; }

  const String& type_name() const { return type_name_; }

  void set_type_name(const String& type_name) { type_name_ = type_name; }

  const FieldVec& fields() const { return fields_.entries(); }

  size_t get_indices(StringRef name, IndexVec* result) const {
    return fields_.get_indices(name, result);
  }

  void add_field(const String name, const DataType::ConstPtr& data_type) {
    fields_.add(Field(name, data_type));
  }

  void set_fields(const FieldVec& fields) { fields_.set_entries(fields); }

  virtual bool equals(const DataType::ConstPtr& data_type) const {
    assert(value_type() == CASS_VALUE_TYPE_UDT);
    if (data_type->value_type() != CASS_VALUE_TYPE_UDT) {
      return false;
    }

    const UserType::ConstPtr& user_type(data_type);

    if (!equals_both_not_empty(keyspace_, user_type->keyspace_)) {
      return false;
    }

    if (!equals_both_not_empty(type_name_, user_type->type_name_)) {
      return false;
    }


    // UDT's can be considered equal as long as the mutual first fields shared
    // between them are equal. UDT's are append only as far as fields go, so a
    // newer 'version' of the UDT data type after a schema change event should be
    // treated as equivalent in this scenario, by simply looking at the first N
    // mutual fields they should share.
    size_t min_fields_size = std::min(fields_.size(), user_type->fields_.size());

    for (size_t i = 0; i < min_fields_size; ++i) {
      if (fields_[i].name != user_type->fields_[i].name ||
          !fields_[i].type->equals(user_type->fields_[i].type)) {
        return false;
      }
    }

    return true;
  }

  virtual DataType::Ptr copy() const {
    return DataType::Ptr(new UserType(keyspace_, type_name_, fields_.entries(), is_frozen()));
  }

  virtual String to_string() const {
    String str;
    if (is_frozen()) str.append("frozen<");
    str.append(type_name_);
    if (is_frozen()) str.push_back('>');
    return str;
  }

private:
  String keyspace_;
  String type_name_;
  CaseInsensitiveHashTable<Field> fields_;
};

class ValueTypes {
public:
  ValueTypes();

  static CassValueType by_class(StringRef name);
  static CassValueType by_cql(StringRef name);

private:
  typedef SmallDenseHashMap<StringRef, CassValueType,
                            CASS_VALUE_TYPE_LAST_ENTRY, // Max size
                            StringRefIHash, StringRefIEquals>
      HashMap;

  static HashMap value_types_by_class_;
  static HashMap value_types_by_cql_;
};

class SimpleDataTypeCache {
public:
  const DataType::ConstPtr& by_class(StringRef name) {
    return by_value_type(ValueTypes::by_class(name));
  }

  const DataType::ConstPtr& by_cql(StringRef name) {
    return by_value_type(ValueTypes::by_cql(name));
  }

  const DataType::ConstPtr& by_value_type(uint16_t value_type);

private:
  DataType::ConstPtr cache_[CASS_VALUE_TYPE_LAST_ENTRY];
};

template <class T>
struct IsValidDataType;

template <>
struct IsValidDataType<CassNull> {
  bool operator()(CassNull, const DataType::ConstPtr& data_type) const { return true; }
};

template <>
struct IsValidDataType<cass_int8_t> {
  bool operator()(cass_int8_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_TINY_INT;
  }
};

template <>
struct IsValidDataType<cass_int16_t> {
  bool operator()(cass_int16_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_SMALL_INT;
  }
};

template <>
struct IsValidDataType<cass_int32_t> {
  bool operator()(cass_int32_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_INT;
  }
};

template <>
struct IsValidDataType<cass_uint32_t> {
  bool operator()(cass_uint32_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_DATE;
  }
};

template <>
struct IsValidDataType<cass_int64_t> {
  bool operator()(cass_int64_t, const DataType::ConstPtr& data_type) const {
    return is_int64_type(data_type->value_type());
  }
};

template <>
struct IsValidDataType<cass_float_t> {
  bool operator()(cass_float_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_FLOAT;
  }
};

template <>
struct IsValidDataType<cass_double_t> {
  bool operator()(cass_double_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_DOUBLE;
  }
};

template <>
struct IsValidDataType<cass_bool_t> {
  bool operator()(cass_bool_t, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_BOOLEAN;
  }
};

template <>
struct IsValidDataType<CassString> {
  bool operator()(CassString, const DataType::ConstPtr& data_type) const {
    // Also allow "bytes" types to be bound as "string" types
    return is_string_type(data_type->value_type()) || is_bytes_type(data_type->value_type());
  }
};

template <>
struct IsValidDataType<CassBytes> {
  bool operator()(CassBytes, const DataType::ConstPtr& data_type) const {
    return is_bytes_type(data_type->value_type());
  }
};

template <>
struct IsValidDataType<CassCustom> {
  bool operator()(const CassCustom& custom, const DataType::ConstPtr& data_type) const {
    if (!data_type->is_custom()) return false;
    CustomType::ConstPtr custom_type(data_type);
    return custom.class_name == custom_type->class_name();
  }
};

template <>
struct IsValidDataType<CassUuid> {
  bool operator()(CassUuid, const DataType::ConstPtr& data_type) const {
    return is_uuid_type(data_type->value_type());
  }
};

template <>
struct IsValidDataType<CassInet> {
  bool operator()(CassInet, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_INET;
  }
};

template <>
struct IsValidDataType<CassDecimal> {
  bool operator()(CassDecimal, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_DECIMAL;
  }
};

template <>
struct IsValidDataType<CassDuration> {
  bool operator()(CassDuration, const DataType::ConstPtr& data_type) const {
    return data_type->value_type() == CASS_VALUE_TYPE_DURATION;
  }
};

template <>
struct IsValidDataType<const Collection*> {
  bool operator()(const Collection* value, const DataType::ConstPtr& data_type) const;
};

template <>
struct IsValidDataType<const Tuple*> {
  bool operator()(const Tuple* value, const DataType::ConstPtr& data_type) const;
};

template <>
struct IsValidDataType<const UserTypeValue*> {
  bool operator()(const UserTypeValue* value, const DataType::ConstPtr& data_type) const;
};

}}} // namespace datastax::internal::core

EXTERNAL_TYPE(datastax::internal::core::DataType, CassDataType)

#endif
