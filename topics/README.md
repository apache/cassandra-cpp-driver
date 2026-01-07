# Getting Started

## Installation

All packages referenced below can be found on our [Artifactory server].

### Driver

Packages are available for the following platforms:

* Rocky Linux 8.8
* Rocky Linux 9.2
* Ubuntu 20.04
* Ubuntu 22.04
* Windows

### Dependencies

Packages for the various dependencies of the driver can be found in the
`dependencies` directory for each platform.  We do not provide dependencies
for Ubuntu since the default packages provided by newer versions of
Ubuntu can be used by the driver.

The driver can also be [built from source].

## Connecting

```c
#include <cassandra.h>
/* Use "#include <dse.h>" when connecting to DataStax Enterpise */
#include <stdio.h>

int main() {
  /* Setup and connect to cluster */
  CassFuture* connect_future = NULL;
  CassCluster* cluster = cass_cluster_new();
  CassSession* session = cass_session_new();

  /* Add contact points */
  cass_cluster_set_contact_points(cluster, "127.0.0.1");

  /* Provide the cluster object as configuration to connect the session */
  connect_future = cass_session_connect(session, cluster);

  /* This operation will block until the result is ready */
  CassError rc = cass_future_error_code(connect_future);

  if (rc != CASS_OK) {
    /* Display connection error message */
    const char* message;
    size_t message_length;
    cass_future_error_message(connect_future, &message, &message_length);
    fprintf(stderr, "Connect error: '%.*s'\n", (int)message_length, message);
  }

  /* Run queries... */

  cass_future_free(connect_future);
  cass_session_free(session);
  cass_cluster_free(cluster);

  return 0;
}
```

To connect a session, a [`CassCluster`] object will need to be created and
configured. The minimal configuration needed to connect is a list of contact
points. The contact points are used to initialize the driver and it will
automatically discover the rest of the nodes in your cluster.

**Perfomance Tip:** Include more than one contact point to be robust against
node failures.

## Futures

The driver is designed so that no operation will force an application to block.
Operations that would normally cause the application to block, such as
connecting to a cluster or running a query, instead return a [`CassFuture`]
object that can be waited on, polled, or used to register a callback.

**NOTE:** The API can also be used synchronously by waiting on or immediately
attempting to get the result from a future.

## Executing Queries

Queries are executed using [`CassStatement`] objects. Statements encapsulate
the query string and the query parameters. Query parameters are not supported
by earlier versions of Cassandra (1.2 and below) and values need to be inlined
in the query string itself.

```c
void execute_query(CassSession* session) {
  /* Create a statement with zero parameters */
  CassStatement* statement
    = cass_statement_new("INSERT INTO example (key, value) VALUES ('abc', 123)", 0);

  CassFuture* query_future = cass_session_execute(session, statement);

  /* Statement objects can be freed immediately after being executed */
  cass_statement_free(statement);

  /* This will block until the query has finished */
  CassError rc = cass_future_error_code(query_future);

  printf("Query result: %s\n", cass_error_desc(rc));

  cass_future_free(query_future);
}
```

## Parameterized Queries (Positional)

Cassandra 2.0+ supports the use of parameterized queries. This allows the same
query string to be executed multiple times with different values; avoiding
string manipulation in your application.

**Perfomance Tip:** If the same query is being reused multiple times,
[prepared statements] should be used to optimize performance.

```c
void execute_paramertized_query(CassSession* session) {
  /* There are two bind variables in the query string */
  CassStatement* statement
    = cass_statement_new("INSERT INTO example (key, value) VALUES (?, ?)", 2);

  /* Bind the values using the indices of the bind variables */
  cass_statement_bind_string(statement, 0, "abc");
  cass_statement_bind_int32(statement, 1, 123);

  CassFuture* query_future = cass_session_execute(session, statement);

  /* Statement objects can be freed immediately after being executed */
  cass_statement_free(statement);

  /* This will block until the query has finished */
  CassError rc = cass_future_error_code(query_future);

  printf("Query result: %s\n", cass_error_desc(rc));

  cass_future_free(query_future);
}
```

## Handling Query Results

A single row can be retrieved using the convenience function
[`cass_result_first_row()`] to get the first row. A [`CassIterator`] object may
also be used to iterate over the returned row(s).

```c
void handle_query_result(CassFuture* future) {
  /* This will also block until the query returns */
  const CassResult* result = cass_future_get_result(future);

  /* If there was an error then the result won't be available */
  if (result == NULL) {

    /* Handle error */

    cass_future_free(future);
    return;
  }

  /* The future can be freed immediately after getting the result object */
  cass_future_free(future);

  /* This can be used to retrieve the first row of the result */
  const CassRow* row = cass_result_first_row(result);

  /* Now we can retrieve the column values from the row */
  const char* key;
  size_t key_length;
  /* Get the column value of "key" by name */
  cass_value_get_string(cass_row_get_column_by_name(row, "key"), &key, &key_length);

  cass_int32_t value;
  /* Get the column value of "value" by name */
  cass_value_get_int32(cass_row_get_column_by_name(row, "value"), &value);


  /* This will free the result as well as the string pointed to by 'key' */
  cass_result_free(result);
}
```

# Architecture

## Cluster

The [`CassCluster`] object describes a Cassandra cluster’s configuration. The
default cluster object is good for most clusters and only requires a single or
multiple list of contact points in order to establish a session connection.
Once a session is connected using a cluster object its configuration is
constant. Modifying the cluster object configuration once a session is
established does not alter the session's configuration.

## Session

The [`CassSession`] object is used for query execution. Internally, a session
object also manages a pool of client connections to Cassandra and uses a load
balancing policy to distribute requests across those connections. An
application should create a single session object per keyspace as a session
object is designed to be created once, reused, and shared by multiple threads
within the application. The throughput of a session can be scaled by
increasing the number of I/O threads. An I/O thread is used to handle reading
and writing query request data to and from Cassandra. The number of I/O threads
defaults to one per CPU core, but it can be configured using
[`cass_cluster_set_num_threads_io()`]. It’s generally better to create a single
session with more I/O threads than multiple sessions with a smaller number of
I/O threads. More DataStax driver best practices can be found in this [post].

## Asynchronous I/O

Each I/O thread maintains a small number of connections for each node in the
Cassandra cluster and each of those connections can handle several simultaneous
requests using pipelining. Asynchronous I/O and pipelining together allow each
connection to handle several (up to 32k with protocol v3/v4) in-flight requests
concurrently. This significantly reduces the number of connections required to
be open to Cassandra and allows the driver to batch requests destined for the
same node.

## Thread safety

A [`CassSession`] is designed to be used concurrently from multiple threads.
[`CassFuture`] is also thread safe. Other than these exclusions, in general,
functions that might modify an object's state are **NOT** thread safe. Objects
that are immutable (marked 'const') can be read safely by multiple threads.

**NOTE:** The object/resource free-ing functions (e.g. `cass_cluster_free`,
`cass_session_free`, ... `cass_*_free`) cannot be called concurrently on the
same instance of an object.

## Memory handling

Values such as strings (`const char*`), bytes and decimals
(`const cass_bytes_t*`) point to memory held by the result object. The
lifetimes of these values are valid as long as the result object isn’t freed.
These values **must** be copied into application memory if they need to live
longer than the result object’s lifetime. Primitive types such as
[`cass_int32_t`] are copied by the driver because it can be done cheaply
without incurring extra allocations.

**NOTE:** Advancing an iterator invalidates the value it previously returned.

## TODO

Here are some features that are missing from the C/C++ driver, but are included
with other drivers. The schedule for these features can be found on the
[CASSCPP project] of the ASF JIRA.

- Compression
- Schema event registration and notification
- Callback interfaces for load balancing, authentication, reconnection and retry

[Artifactory server]: https://datastax.jfrog.io/artifactory/cpp-php-drivers/cpp-driver/builds
[built from source]: https://docs.datastax.com/en/developer/cpp-driver/latest/topics/building/
[prepared statements]: https://docs.datastax.com/en/developer/cpp-driver/latest/topics/basics/prepared_statements/
[`cass_int32_t`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/cassandra.h/#cass-int32-t
[`cass_result_first_row()`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassResult/#cass-result-first-row
[`cass_cluster_set_num_threads_io()`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassCluster/#function-cass_cluster_set_num_threads_io
[`CassCluster`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassCluster/
[`CassFuture`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassFuture/
[`CassStatement`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassStatement/
[`CassIterator`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassIterator/
[`CassSession`]: https://docs.datastax.com/en/developer/cpp-driver/latest/api/struct.CassSession/
[post]: http://www.datastax.com/dev/blog/4-simple-rules-when-using-the-datastax-drivers-for-cassandra
[CASSCPP project]: https://issues.apache.org/jira/issues/?jql=project%20%3D%20CASSCPP%20ORDER%20BY%20key%20DESC