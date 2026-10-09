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

#include "cassandra.h"
#include "integration.hpp"

// Name of materialized view used in this test file.
#define VIEW_NAME "my_view"

class SchemaMetadataTest : public Integration {
public:
  SchemaMetadataTest() { is_schema_metadata_ = true; }

  void SetUp() {
    CHECK_VERSION(2.2.0);
    Integration::SetUp();
    populateSchema();
    schema_meta_ = session_.schema();
  }

  void populateSchema() {
    session_.execute(format_string("CREATE TABLE %s (key text, value bigint, "
                                   "PRIMARY KEY (key))",
                                   table_name_.c_str()));

    session_.execute("CREATE FUNCTION avg_state(state tuple<int, bigint>, val int) "
                     "CALLED ON NULL INPUT RETURNS tuple<int, bigint> "
                     "LANGUAGE java AS "
                     "  'if (val != null) { "
                     "    state.setInt(0, state.getInt(0) + 1); "
                     "    state.setLong(1, state.getLong(1) + val.intValue()); "
                     "  } ;"
                     "  return state;'"
                     ";");
    session_.execute("CREATE FUNCTION avg_final (state tuple<int, bigint>) "
                     "CALLED ON NULL INPUT RETURNS double "
                     "LANGUAGE java AS "
                     "  'double r = 0; "
                     "  if (state.getInt(0) == 0) return null; "
                     "  r = state.getLong(1); "
                     "  r /= state.getInt(0); "
                     "  return Double.valueOf(r);' "
                     ";");

    session_.execute("CREATE AGGREGATE average(int) "
                     "SFUNC avg_state STYPE tuple<int, bigint> FINALFUNC avg_final "
                     "INITCOND(0, 0);");

    if (server_version_ >= "3.0.0") {
      session_.execute(format_string("CREATE MATERIALIZED VIEW %s "
                                     "AS SELECT value, key "
                                     "   FROM %s"
                                     "   WHERE value IS NOT NULL and key IS NOT NULL "
                                     "PRIMARY KEY(value, key)",
                                     VIEW_NAME, table_name_.c_str()));
    }
    session_.execute("CREATE TYPE address (street text, city text)");

    session_.execute(
        format_string("CREATE INDEX schema_meta_index ON %s (value)", table_name_.c_str()));
  }

protected:
  Schema schema_meta_;
};

CASSANDRA_INTEGRATION_TEST_F(SchemaMetadataTest, Views) {
  CHECK_VERSION(3.0.0);
  Keyspace keyspace_meta = schema_meta_.keyspace(keyspace_name_);
  Table table_meta = keyspace_meta.table(table_name_);

  // Verify that the view exists in the keyspace and table.
  const CassMaterializedViewMeta* view_from_keyspace =
      cass_keyspace_meta_materialized_view_by_name(keyspace_meta.get(), VIEW_NAME);
  EXPECT_TRUE(view_from_keyspace != NULL);

  // Now from the table, and it should be the same CassMaterializedViewMeta object.
  const CassMaterializedViewMeta* view_from_table =
      cass_table_meta_materialized_view_by_name(table_meta.get(), VIEW_NAME);
  EXPECT_EQ(view_from_keyspace, view_from_table);

  // Verify that the view's back-pointer references this table object.
  EXPECT_EQ(table_meta.get(), cass_materialized_view_meta_base_table(view_from_keyspace));

  // Alter the view, which will cause a new event, and make sure the new
  // view object is available in our metadata (in a new schema snapshot).

  session_.execute(format_string("ALTER MATERIALIZED VIEW %s "
                                 "WITH comment = 'my view rocks'",
                                 VIEW_NAME));

  Schema new_schema = session_.schema();
  Keyspace new_keyspace_meta = new_schema.keyspace(keyspace_name_);

  const CassMaterializedViewMeta* updated_view =
      cass_keyspace_meta_materialized_view_by_name(new_keyspace_meta.get(), VIEW_NAME);
  EXPECT_NE(updated_view, view_from_keyspace);
}

CASSANDRA_INTEGRATION_TEST_F(SchemaMetadataTest, DropView) {
  CHECK_VERSION(3.0.0);
  Table table_meta = schema_meta_.keyspace(keyspace_name_).table(table_name_);

  // Verify that the table contains the view
  EXPECT_TRUE(cass_table_meta_materialized_view_by_name(table_meta.get(), VIEW_NAME) != NULL);

  session_.execute(format_string("DROP MATERIALIZED VIEW %s", VIEW_NAME));

  Schema new_schema = session_.schema();
  Table new_table_meta = new_schema.keyspace(keyspace_name_).table(table_name_);

  // Verify that the view has been removed from the table
  EXPECT_TRUE(cass_table_meta_materialized_view_by_name(new_table_meta.get(), VIEW_NAME) == NULL);

  // Verify that a new table metadata instance has been created
  EXPECT_NE(table_meta.get(), new_table_meta.get());
}

CASSANDRA_INTEGRATION_TEST_F(SchemaMetadataTest, RegularMetadataNotMarkedVirtual) {
  CHECK_VERSION(2.2.0);
  // Check non-virtual keyspace/table is correctly not set
  Keyspace keyspace_meta = schema_meta_.keyspace("system");
  ASSERT_TRUE(keyspace_meta);
  EXPECT_FALSE(keyspace_meta.is_virtual());

  Table table_meta = keyspace_meta.table("peers");
  ASSERT_TRUE(table_meta);
  EXPECT_FALSE(table_meta.is_virtual());
}

CASSANDRA_INTEGRATION_TEST_F(SchemaMetadataTest, VirtualMetadata) {
  CHECK_VERSION(4.0.0);

  // Check virtual keyspace/table is correctly set
  Keyspace keyspace_meta = schema_meta_.keyspace("system_views");
  ASSERT_TRUE(keyspace_meta);
  EXPECT_TRUE(keyspace_meta.is_virtual());

  Table table_meta = keyspace_meta.table("sstable_tasks");
  ASSERT_TRUE(table_meta);
  EXPECT_TRUE(table_meta.is_virtual());

  // Cassandra 4.1 added a `sstables` column and changed `task_id`'s type;
  // later 5.0.x patches add more columns to this table, so for future compatibility,
  // column count is a floor, not an exact match, from 4.1 onward
  bool is_4_1_or_later = server_version_ >= "4.1.0";

  // Verify virtual table's metadata
  EXPECT_GE(cass_table_meta_column_count(table_meta.get()), is_4_1_or_later ? 9u : 8u);
  EXPECT_EQ(cass_table_meta_index_count(table_meta.get()), 0u);
  EXPECT_EQ(cass_table_meta_materialized_view_count(table_meta.get()), 0u);

  EXPECT_EQ(cass_table_meta_partition_key_count(table_meta.get()), 1u);
  EXPECT_EQ(cass_table_meta_clustering_key_count(table_meta.get()), 2u);

  EXPECT_EQ(cass_table_meta_clustering_key_order(table_meta.get(), 0), CASS_CLUSTERING_ORDER_ASC);
  EXPECT_EQ(cass_table_meta_clustering_key_order(table_meta.get(), 1), CASS_CLUSTERING_ORDER_ASC);

  const CassColumnMeta* column_meta;

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "keyspace_name");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_TEXT);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "table_name");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_TEXT);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "task_id");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)),
            is_4_1_or_later ? CASS_VALUE_TYPE_TIMEUUID : CASS_VALUE_TYPE_UUID);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "kind");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_TEXT);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "progress");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_BIGINT);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "total");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_BIGINT);

  column_meta = cass_table_meta_column_by_name(table_meta.get(), "unit");
  ASSERT_TRUE(column_meta);
  EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_TEXT);

  if (is_4_1_or_later) {
    column_meta = cass_table_meta_column_by_name(table_meta.get(), "sstables");
    ASSERT_TRUE(column_meta);
    EXPECT_EQ(cass_data_type_type(cass_column_meta_data_type(column_meta)), CASS_VALUE_TYPE_INT);
  }
}
