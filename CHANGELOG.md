2.18.0
===========

Features
--------
* [CPP-1000] Native protocol v5 support. Framing (CRC24 header, CRC32 trailer,
  split frames up to 131071 bytes), per-request keyspace for QUERY/EXECUTE and
  BATCH, and the Now_in_seconds query flag. Uncompressed framing only.

Compatibility
-------------
* This version negotiates protocol v5 by default when the server supports it.
  Set `cass_cluster_set_protocol_version(cluster, CASS_PROTOCOL_VERSION_V4)` to
  pin v4, or `CASS_PROTOCOL_VERSION_DSEV2` for DataStax Enterprise.
* Verified against Apache Cassandra 5.0.9 over both v5 and v4.

Known Limitations
-----------------
* Compressed frames are not implemented. A server that negotiates frame
  compression is not supported.
* `Now_in_seconds` has no public C API; it is currently internal only.
* The full unit suite requires exclusive use of port 9042.

2.17.1
===========

Bug Fixes
--------
* [CPP-991] Add support for extracting version info from OpenSSL 3.x in build output
* [CPP-987] cass_future_wait does not respect registered callback
* [CPP-990] Be clearer about installed dependencies, specifically around OpenSSL 3.0
* [CPP-988] cass_uuid_gen_time race condition generates duplicate uuidv1 keys
* [CPP-964] Add refresh-interval support for histogram metrics

Community
--------
* [PR #538] Prioritize ${OPENSSL_ROOT_DIR} over system paths (diku89)
* [PR #535] Remove unreachable code (SeverinLeonhardt)
* [PR #534] Fix signed/unsigned mismatch (SeverinLeonhardt)
* [PR #533] Adapt MemoryOrder definition for C++ 20 (SeverinLeonhardt)

2.17.0
===========

Bug Fixes
--------
* [CPP-942] Add docs on possibility of MITM attacks with cass_cluster_set_use_hostname_resolution()
* [CPP-955] Unable to use different memory allocators and profilers when using the cassandra driver
* [CPP-957] Update build/test platforms
* [CPP-919] CMake 3.16 deprecation warnings

Community
--------
* [PR #522] Iterate over all certificates in a trusted cert BIO, not just the first (kw217)
* [PR #528] Add support for newer versions of LibreSSL (gahr)
* [PR #525] Allow users to request TLS client-side enforcement (FalacerSelene)

2.16.2
===========

Bug Fixes
--------
* [CPP-946] Core dump on unclean event loop shutdown
* [PR #513] Fix SNI events

Community
--------
* [PR #518] Replace deprecated function for OpenSSL >= 3.0 (pjgeorg)

2.16.1
===========

Bug Fixes
--------
* [CPP-935] Latency aware policy never activates because minimum latency isn't updated for request processors

2.16.0
===========

Features
--------
* [PR #489] Add API to get/set coordinator node

Bug Fixes
--------
* [CPP-924] Pure virtual function called when Session object is being destructed
* [PR #488] Only using a single resolved IP when connecting with hostname
* [PR #494] Use correct `Host:` header when calling metadata service (Astra)

Other
---------
* [CPP-923] Reduce the log severity of protocol negotiation errors/warnings

2.15.3
===========

Bug Fixes
--------
* [CPP-922] Limit to TLS 1.2

2.15.2
===========

Bug Fixes
--------
* [CPP-917] Infinite loop in token map calculation when using SimpleStrategy and RF > number of
  nodes

Community
--------
* Fix compatibility with OpenSSL 1.1 (SeverinLeonhardt)

2.15.1
===========

Bug Fixes
--------
* [CPP-747] Cannot connect to keyspace with uppercase characters
* [CPP-897] Simplify CMake build
* [CPP-913] Possible for a token map replica set for a given range to have duplicates
* [CPP-914] Possible for a WaitForHandler's on_set() method to be called after timeout/error

Other
--------
* [CPP-847] Add CentOS 8 support
* [CPP-889] Duplicated entry for the DSE features documentation

Community
--------
* Fix cflags when build with CASS_INSTALL_HEADER_IN_SUBDIR on (remicollet)
* Fix typo in RequestProcessorInitializer::internal_initialize (m8mble)

2.15.0
===========

Features
--------
* [CPP-865] Unified driver
  * We have open sourced and merged DataStax Enterprise (DSE) driver features
    into a single, open source driver that supports both Apache Cassandra and
    DataStax products. *Note:* DSE-specific driver features still require DSE.
    * Support for the DSE authentication mechanisms including plaintext and LDAP
      (via GSSAPI)
    * Support for geospatial types including `POINT`, `LINESTRING`, and `POLYGON`
  * Supporting JIRA issues
    * [CPP-864] Merge DSE into core driver 
    * [CPP-867] Merge DSE docs into core driver
    * [CPP-866] Move DSE uses of external authentication provider to internal interfaces
    * [CPP-861] Add `CASS_USE_KERBEROS` option and return errors from dependent auth API functions

Bug Fixes
--------
* [CPP-885] Fix filtering load balancing policies (and remove duplicated load balancing policy chain)
* [CPP-884] Deprecate pending request timeouts metric and remove unused internal logic
* [CPP-871] Fix SSL cleanup on error conditions in mockssandra
* [CPP-855] Fix C\*/DSE protocol lowering attempts when `cass_cluster_set_use_beta_protocol_version()` is true

Other
--------
* [CPP-220] Remove use of external Boost from unit and integration tests

2.14.1
===========

Bug Fixes
--------
* [CPP-849] Error result doesn't allow access to keyspace, table, and function
  data
* [CPP-851] Disable deprecated warnings for std::ptr_fun
* [CPP-879] Allow remote hosts to come back up even if policy ignores down hosts
  (community PR from kw217)

Other
--------
* [CPP-220] Remove use of external Boost from unit and integration tests. 
  * We ported all integration tests from Boost to Google test. This includes
    several JIRA issues included in the [CPP-220] epic.
* [CPP-853] Correct linking libraries for unix based OS when using
  CASS_USE_STATIC_LIBS=On 
* [CPP-859] Remove vc_build.bat scripts and update building documentation
* [CPP-872] Fix GCC 9.2+ build
* [CPP-878] Correct compile flags for libraries and executable
* [CPP-882] Correct pthread compiler flag for older CMake versions

Community
--------
* Fix build error when compiling without an SSL implementation (kmaragon)

2.14.0
===========

Bug Fixes
--------
* [CPP-819] Ensure port is updated on already assigned contact points
* [CPP-825] Cloud should be verifying the peer certificates CN

2.14.0-alpha2
===========

Features
--------
* [CPP-812] Enable warnings for implicit casts and fix problems
* [CPP-813] Detect CaaS and change consistency default
* [CPP-817] Provide error if mixed usage of secure connect bundle and contact points/ssl context

Bug Fixes
--------
* [CPP-802] Handle prepared id mismatch when repreparing on the fly
* [CPP-815] Schema agreement fails with SNI
* [CPP-811] Requests won't complete if they exceed the number of streams on a connection

2.14.0-alpha
===========

Features
--------
* [CPP-787] DataStax cloud platform
  * [CPP-788] Support SNI at connection level using `host_id` as host name
    * [CPP-793] Add SNI support to `SocketConnector` and SSL backend
    * [CPP-794] Add domain name resolution to `SocketConnector`
    * [CPP-795] Replace `Address` with endpoint or host type on connection path
    * [CPP-797] Events need to map from affected node address to `host_id`
    * [CPP-800] Node discovery should use the `host_id` (and endpoint address) instead of the
      node's rpc_address
  * [CPP-790] Configuration API for DBaaS
    * [CPP-791] Add creds.zip support for automatic configuration
    * [CPP-798] Configure authentication and SSL from secure connection bundle configuration
    * [CPP-799] Use metadata service to determine contact points
    * [CPP-788] Support SNI at connection level using `host_id` as host name
    * [CPP-803] Propagate `local_dc` from `CloudClusterMetadataResolver` to load balancing policies

Bug Fixes
--------
* [CPP-786] Fix TLS 1.3 support
* [CPP-806] Fix handling of no contact points

Other
--------
* [CPP-796] Correct compiler flags for mixed C and C++ projects

Community
--------
* [CPP-754] Broken build with GCC 9 (eevans)
* Add openssl to the required library list in pkg_config file (accelerated)
* Allow random to work with 0 (joeyhub)

2.13.0
===========

Features
--------
* [CPP-745] Exponential reconnection policy with jitter
* [CPP-769] Use `clang-format` to fix up formatting
* [CPP-775] Add `clang-format` to Windows

Other
--------
* [CPP-743] Move internal components from `namespace cass` to `namespace datastax::internal`
* [CPP-764] Rename namespace from `cass` to `datastax`
* [CPP-766] Allow RPM packaging script to build RPM packages for Amazon Linux
* [CPP-770] Fix header files include guard (\_\_CASS_XXX to DATASTAX_XXX)
* [CPP-772] Remove per cpp file LOG_FILE attribute to speed up Windows builds

Community
--------
* Fixed the location of the '[in]' information for Doxygen. (AlexisWilke)
* Added header subdirectory installation capability. (accelerated)
* Changed pkg_config templates to use the library variable name. (accelerated)
* Fix generation of shlib deps file for debian packages. (jirkasilhan)

2.12.0
===========

Features
--------
* [CPP-751] Call host listener callback for the initial set of hosts

Bug Fixes
--------
* [CPP-755] UDT metadata not being properly populated/updated

Other
--------
* [CPP-705] Deprecate DC-aware multi-DC settings (`used_hosts_per_remote_dc`
            and `allowRemoteDCsForLocalConsistencyLevel `)
* [CPP-720] Streamline custom allocator

2.11.0
===========

Features
--------
* [CPP-108] Add tracing support
* [CPP-402] Add support for host event callback
* [CPP-524] Add client configuration information to STARTUP message
* [CPP-597] Provide a means of sending query to a specific node to facilitate virtual table queries

Bug Fixes
--------
* [CPP-632] It's possible for RequestProcessor to miss add/remove event during initialization
* [CPP-701] Don't use Host::is_up() flag inside of load balancing policies
* [CPP-711] Memory leak in `WaitForHandler`/`SchemaChangeHandler` (reference cycle)

Other
--------
* [CPP-183] Concurrent Control Connection Establishment
* [CPP-405] Namespace rapidjson to avoid conflicts with other rapidjson usages
* [CPP-413] Enable Client Timestamps by Default
* [CPP-510] Try all contact points until one succeeds
* [CPP-585] Deprecate DowngradingConsistencyRetryPolicy
* [CPP-686] Pull in the latest rapidjson changes from master
* [CPP-688] Use plugin architecture for protocol version handling
* Added ${CMAKE_LIBRARY_ARCHITECTURE} path suffix for libuv search

2.10.0
===========

Features
--------
* [CPP-657] Support virtual tables (aka system views)

Bug Fixes
--------
* [CPP-648] Attempting to get the metrics before the session is connected will crash (existing issue)
* [CPP-654] Use Boost atomics when building drivers for Visual Studio 2012 x86
* [CPP-666] Metadata crash in schema metadata null tests
* [CPP-667] Timer handles still referenced
* [CPP-668] Unit test `ClusterUnitTest.ReconnectUpdateHosts` can hang
* [CPP-669] Result response's metadata can reference a prepared response that's already been freed
* [CPP-670] Unresolved references when built against libressl
* [CPP-672] Fix memory issues found via `-fstanitize=address`
* [CPP-673] Cluster can hang attempting to close during reconnection
* [CPP-674] C/C++ driver 2.10.0-beta1 build failure (GCC 8)
* [CPP-675] Chained callback should only call a single callback once
* [CPP-678] Fix Cassandra version for DSE 6.X releases
* [CPP-687] Fixing recovery of remote DC host
* [CPP-692] Fix C++98 compiler issues
* [CPP-693] Ensure materialized view metadata is valid before retrieving base table

Other
--------
* [CPP-611] Reduce coalesce delay to better support latency workloads
* [CPP-615] Remove support for protocols v1 and v2
* [CPP-652] Decouple listeners from connection/initialization
* [CPP-653] Synchronously propagate keyspace updates

2.10.0-beta1
===========

Features
--------
* [CPP-360] Added the ability to specify custom memory allocators using
  `cass_alloc_set_functions()`
* [CPP-417] Added speculative execution metrics which can be obtained using
  `cass_session_get_speculative_execution_metrics()`
* [CPP-404] Performance: Shared-nothing I/O workers (new internal architecture)
* [CPP-441] Move IO thread request scheduling to a "pull" model
* [CPP-453] Move token map calculation off the session thread
* [CPP-466] Token aware routing replicas are now randomly shuffled by default
  instead of using a random index
* [CPP-492] Support execution profiles
* [CPP-499] Allow binding local address of connections
* [CPP-515] Remove support for libuv v0.10.x
* [CPP-518] Thread and connection pool refactor
* [CPP-616] Handle libuv v1.20.4+ version file updates

Bug Fixes
--------
* [CPP-437] Fixed batch encoding performance regression
* [CPP-477] Native protocol "support" messages are now properly decoded, but
  still remain unused
* [CPP-589] Pathological hashing behavior in stream manager
* [CPP-590] Execution profiles are copied for every request (expensive to copy)
* [CPP-662] The timerfd version of MicroTimer doesn't handle zero timeout
* [CPP-663] Mutex in SessionBase can be destroyed while it's still locked
* [CPP-665] Memory issue when socket fails to initialize

Other
--------
* [CPP-136] Decoding now verifies buffer sizes when processing server responses
* [CPP-440] Improved encoding performance by preallocating buffers
* [CPP-444] Fixed const correctness of serialization functions
* [CPP-476] Replaced `strlen()` with `sizeof()` for static strings

2.9.0
===========

Features
--------
* [CPP-578] Add NO_COMPACT startup option

Other
--------
* [CPP-566] Ignore control connection DOWN events if there are open connections
* [CPP-584] win: Message for Boost v1.66.0+ using CMake v3.10.x or lower

2.8.1
===========

Bug Fixes
--------
* [CPP-572] DC aware policy plan returns hosts that are not connected

Other
--------
* [CPP-563] Re-add randomization determination during configuration

2.8.0
===========

Features
--------
* [CPP-393] Allow prepared statements to be prepared on all nodes
* [CPP-394] Prepare statements on UP/ADD events
* [CPP-439] Support OpenSSL 1.1
* [CPP-512] Include hash of result set metadata in prepared statement id
* [CPP-528] Per-query (and per-batch) keyspace support

Bug Fixes
--------
* [CPP-368] API Functions that take strings now check for NULL and replace it
  with an empty string
* [CPP-438] Broken build with GCC 7 and OpenSSL 1.1
* [CPP-501] Schema metadata is corrupted when views and indexes exist for a
  table
* [CPP-503] Schema meta race condition when a view is dropped from a table
* [CPP-535] Schema metadata views are not properly updated during drop event
* [CPP-540] NULL columns in table/view metadata caused by side-effect in a
  compiled out assertion
* [CPP-543] Fix dllimport/dllexport
* [CPP-546] Result metadata callback casting requests to invalid types
* [CPP-547] Pool request callback attempting to return a deleted connection

Other
-------
* [CPP-432] Removed dense table 'empty' columns from metadata
* [CPP-449] Update vc_build.bat to include automated build for Visual Studio
  2017
* [CPP-478] Renamed the class `SpeculativeExecution` to `RequestExection` to
  reduce confusion when it appears in driver logs
* [CPP-523] Use arc4random or getrandom instead of /dev/urandom (if available)
* [CPP-526] Add a mutable wrapper around the request object
* [CPP-538] Deprecate watermarks

2.7.1
===========

Bug Fixes
--------
* [CPP-473] Missing symbol cass_cluster_set_queue_size_log
* [CPP-486] Compiler warning 'implicit-fallthrough' causing build errors
* [CPP-491] Unintentional switch case fallthrough in `RequestCallback`
* [CPP-496] CassWriteType CDC and VIEW missing
* [CPP-502] Incorrectly exported symbols cass_error_result_responses_received / required
* [CPP-513] Copy-on-write keyspace logic is incorrect
* [CPP-514] Prepared Statement Crash

2.7.0
===========

Features
--------
* Added beta support for protocol v5 which can be enabled using
  `cass_cluster_set_use_beta_protocol_version()`

Other
--------
* Duration type needs to use `int64_t` for the nanos component (CPP-454)
* Heavy load when updating token maps on node addition (CPP-460)
* Adding a node breaks token map reconstruction (and can cause long delays)
  (CPP-464)

2.6.0
===========

Features
--------
* Added duration type support via `cass_statement_bind_duration[by_name]()`,
  `cass_collection_append_duration()`, `cass_tuple_set_duration()`,
  `cass_user_type_set_duration[by_name]()` and `cass_value_get_duration()`

Other
--------
* Added process information (pid) to UUID node hash (CPP-354)
* Fixed documentation mismatch with default consistency (CPP-383)
* Fixed issue where an invalid keyspace could cause a session to hang
  indefinitely during the connection process (CPP-384)
* Fixed issue where an invalid local datacenter (DC-aware policy) could cause a
  session to hang indefinitely during the connection process (CPP-398)
* Fixed issue where SSL errors would prevent connections from reconnecting (CPP-408)
* Made the client-side timestamp generator fully monotonic (CPP-412)
* Fixed issue where an invalid value type, `CASS_VALUE_TYPE_INT`, was returned
  for `org.apache.cassandra.db.marshal.IntegerType` instead of
  `CASS_VALUE_TYPE_VARINT` (CPP-419)
* Fixed issue where single quote custom types (e.g.
  'org.apache.cassandra.db.marshal.LexicalUUIDType') would be incorrectly
  returned as a UDT data type instead of as a custom data type (CPP-431)

2.5.0
===========
October 20, 2016

Features
--------
* Added constant speculative execution which preemptively runs multiple of the
  same query in an effort to smooth out latency spikes.
* Added idempotent queries to support speculative execution and allow automatic
  retries. Use `cass_statement_set_is_idempotent()` or
  `cass_batch_set_is_idempotent()`.
* SSL can be now be used without initializing the underlying implementation
  (e.g. OpenSSL). Use `cass_ssl_new_no_lib_init()`.

Other
--------
* Fixed an issue where a connection is incorrectly marked READY after it''s
  already been marked DEFUNCT (CPP-401)
* Fixed pointer aliasing issues in `cass::Address` (CPP-406)
* Fixed an issue where random partitioner hash values were being incorrectly
  generated (CPP-403)

2.4.3
===========
August 22, 2016

Features
--------
* Contact points are now randomized by default (CPP-193)
* Token-aware routing can be used without enabling schema metadata (CPP-387)
* Multiple IP addresses can be resolved from a single domain (CPP-364)

Other
--------
* Fixed issue that would cause quadratic ring processing with invalid
  replication factors (CPP-298)
* Fixed issue where schema change handler could hang if an error is returned by
  Cassandra (CPP-381)
* Fixed crash caused by connecting separate sessions in multiple threads
  (CPP-385)
* Fixed issue where the control connection could timeout as a result of schema
  or token map processing (CPP-388)
* Greatly improved the performance of building the token map for token aware
  routing (CPP-389)
* Fixed issue where heartbeats were restarted on unresponsive connections and
  prevented the connection from being terminated (CPP-392)

2.4.2
===========
June 24, 2016

Features
--------
* Added the per-request timeouts to statement and batch
* Added the ability to remove custom payload items

Other
--------
* Fixed issue where cass_date_time_to_epoch() unable to handle dates before
  Cassandra date epoch (value: 2147483648)

2.4.1
===========
June 9, 2016

Other
--------
* Fixed issue where `cass_future_get_result()` and similar methods would
  dereference a NULL pointer if a future is set to an error.

2.4.0
===========
June 1, 2016

Features
--------
* Added the ability to add a custom SASL authenticator
* Added reverse DNS lookup for IP addresses which can be enabled using
 `cass_cluster_set_use_hostname_resolution()`.
* Added a DNS resolve timeout that can be set using
  `cass_cluster_set_resolve_timeout()`.
* Added the ability for SSL peer identity to be verified by hostname.
  (`CASS_SSL_VERIFY_PEER_IDENTITY_DNS`)
* Added the ability to bind/set/append custom types with a class name.
* Added `CassStatement` function to clear/resize parameters
  `cass_statement_reset_parameters()`.

Other
--------
* Fixed crash caused by tables with the same name in adjacent keyspaces.
* Fixed an invalid memory read in processing strategy options metadata.
* Fixed issue where `null` values didn''t have proper datatypes.
* Fixed issue where an "UP" event could cause the session connection process
  to hang.
* Reduced reconnection log chatter at the `CASS_LOG_ERROR` level. A failed
  reconnection attempt is only logged one time as an error.

2.3.0
===========
March 14, 2016

Features
--------
* Added support for materialized view schema metadata.
* Added support for secondary index schema metadata.
* Added cluster key order to table schema metadata via
  `cass_table_meta_clustering_key_order()`.
* Added frozen flag to `CassDataType` via `cass_data_type_is_frozen()`.
* Added support getting the connected cluster''s Cassandra version via
  `cass_schema_meta_version()`.
* Added blacklist, whitelist DC, and blacklist DC load balancing policies.
  Thanks to Walid Aitamer (waitamer).

Other
--------
* Fixed the control connection to only become "ready" during the initial
  connection process.
* Fixed assertion in data type comparison for collection/tuple types.
* Fixed reference count issue in `cass_collection_new_from_data_type()`
* Fixed retry consistency levels for batches. Batches now correctly use the
  downgraded consistency levels supplied from retry policies.
* Fixed function name typo: `cass_data_sub_type_count()` to
  `cass_data_type_sub_type_count()`. The old spelling is still present, but
  deprecated.
* Fixed `cass::VersionNumber` to handle tick-tock release version where only
  "major.minor" are provided.
* Fixed function `cass_session_connect_keyspace()` to correctly return an error
  when the keyspace is invalid.
* Fixed invalid logic in schema metadata swap method which could cause unnecessary
  copy-on-writes.
* Fixed bottleneck in `cass::Session` by using copy-on-write instead of a
  highly contended lock to track the current keyspace.
* Changed "major/minor" to "major_version/minor_version" in `cass::VersionNumber` to
  avoid naming conflicts on some platforms.

2.2.2
===========
December 14, 2015

Features
--------
* Added basic support for Cassandra 3.0. The driver supports Cassandra 3.0
  schema metadata for keyspaces, tables, columns, user types, functions, and
  aggregates.

Other
--------
* Changed the default consistency from `CASS_CONSISTENCY_LOCAL_QUORUM` to
  `CASS_CONSISTENCY_LOCAL_ONE`

2.2.1
===========
November 19, 2015

Other
--------
* Fixed issue where cass_data_type_sub_data_type() was always referencing a
  NULL pointer
* Fixed cass::Atomic (intrinsic) for 32-bit MSVC

2.2.0
===========
October 29, 2015

Features
--------
* Refactored the schema metadata API (`CassSchema`) to use concrete types
  (`CassKeyspaceMeta`, `CassTableMeta`, etc.) instead of the single
  `CassSchemaMeta` type.
* Added support for UDF/UDA schema metadata
* Added support for whitelist filtering i.e. whitelist load balancing policy

Other
--------
* Fixed `Address::compare()` issue that cause invalid comparisons for IPv4
* Fixed issue that caused `StreamManager` to generate invalid stream IDs in
  Windows release builds.
* Fixed issue where `cass_cluster_set_protocol_version()` would return an error
  for protocol versions higher than v3
* Changed the default consistency from `CASS_CONSISTENCY_ONE` to
  `CASS_CONSISTENCY_LOCAL_QUORUM`
* When using prepared statements with protocol v4 (and higher) the driver will
  use partition key metadata returned in the prepared response instead of using
  schema metadata.
* Improved the performance of UUID to string and UUID from string conversions

2.2.0-beta1
===========
September 21, 2015

Features
--------
* Added support for Cassandra 2.2 data types `tinyint` and `smallint`
  (`cass_int8_t` and `cass_int16_t`)
* Added `cass_uint32_t` support for the Cassandra 2.2 `date` data type
* Added functions to convert from Unix Epoch time (in seconds) to and from the
  Cassandra `date` and `time` types
* Added support for sending and recieving custom payloads to and from Cassandra
* Added support for server-side warnings
* Added new error response functions to handle the 'Read_failure', 'Write_failure',
  'Function_failure', and 'Already_exists' errors

Other
--------
* Fixed SSL ring buffer memory leak
* Fixed potential memory issue caused by using named parameters
* Fixed hanging issue when calling `cass_session_get_metrics()`
* Fixed hanging issue caused by `cass::MPMCQueue` memory race
* Fixed invalid state assertion in `cass::Handler`
* Fixed UDTs with `text` fields
* A value of `null` is no longer implicitly used for unbound statement
  parameters. An error is returned (`CASS_ERROR_LIB_PARAMETER_UNSET`) for
  Cassandra 2.1 and earlier or the UNSET value is used for Cassandra 2.2 and
  later

2.1.0
===========
August 11, 2015

Features
--------
* Added support for retry policies
* Added support for client-side timestamps
* Exposed raw paging state token via `cass_result_paging_state_token()`
  and it can be added to a statement using
  `cass_statement_set_paging_state_token()`
* Added support for enabling/disabling schema metadata using
  `cass_cluster_set_use_schema()`

Other
--------
* Connections that timeout will not disconnect an entire connection pool
* `TCP_NODELAY` is enabled by default
* Connections now support using 32,767 stream IDs when using the CQL Native
  Protocol v3
* `cass_uuid_min_from_time()` and `cass_uuid_max_from_time()` now correctly
  considers the time parameter as epoch time

2.1.0-beta
===========
July 7, 2015

Features
--------
* Added support for tuples and UDTs
* Added support for nested collections
* Added support for named parameters when using simple queries (non-prepared)
* Added support for protocol version 3 (and schema changes)

Other
--------
* Fixed SSL error handling
* Added libuv read buffer caching in cass::Connection

2.0.1
===========
May 15, 2015

Other
--------
* Removed logging thread. `cass_log_cleanup()` and `cass_log_set_queue_size()`
  are now deprecated. A logging callback registered with
  `cass_log_set_callback()` will now be called on mulitple threads concurrently.
* Fixed an issue where the driver might crash/hang when `system.local` contains
  no entries
* Fixed an issue where the driver might hang under high-load due to missing
  memory fence before `uv_async_send()` in `cass::AsyncQueue::enqueue()`

2.0.0
===========
April 23, 2015

Features
--------
* Added latency-aware load balancing. To enable use:
  `cass_cluster_set_latency_aware_routing()`
* Added performance metrics. To get metrics use:
  `cass_session_get_metrics()`
* Added `CassInet` string conversion functions:
  `cass_inet_string()` and `cass_inet_to_string()`

Other
--------
* Removed internal dependency on Boost. Optional: Boost Atomic can still be used
  as an implementation for internal atomic operations and memory fences.
* Removed wrapper types `CassString`, `CassBytes` and `CassDecimal`
* Added string length functions (functions with the suffix "_n") for all
  functions that previously took a null-terminated or `CassString` parameter.
* Added missing `cass_statement_bind_null_by_name()` function
* Fixed issue where time-based UUIDs (`CassUuid` v4) had the incorrect time on
  32-bit systems

1.0.1
===========
Mar 2, 2015

Other
--------
* Added testing documentation
* Added documentation for the `varint` data type
* Added a contributing document
* Added version defines to `cassandra.h`
* Fixed an issue where connections were attempted against decommissioned nodes
* Fixed an issue where an invalid key index was used for a single entry
  token-aware routing key
* Improved build error output when an incompatible version of Boost is used to
  build the driver

1.0.0
===========
Feb 2, 2015

Features
--------
* Libuv 1.x supported

Other
--------
* `cass_cluster_set_num_threads_io()` now returns an error code
* Fixed an issue where a node in a remote DC could reconnect even if it
  was supposed to be ignored.
* Reduced the defaults for IO worker threads (1), core connections (1), and
  max connections (2).
* Fixed crash caused by a null token-aware key value.

1.0.0-rc1
===========
Dec 22, 2014

Features
--------
* Added global logging. Logging is no longer configured per session.
* DC-aware load balancing policy is now the default and uses the DC of the first
  connected contact point. The DC-aware policy now has settings to control the
  number of remote hosts used in each DC (after exhausting the hosts in the
  local DC) and whether to ignore local consistency levels.
* Added socket options for TCP nodelay and keepalive:
  `cass_cluster_set_tcp_nodelay()` and `cass_cluster_set_tcp_keepalive()`.

Other
--------
* `CassSession` objects must now be explicitly allocated/freed using
  `cass_session_new()` and `cass_session_free()`. `cass_cluster_connect()` has been
  removed and replaced by `cass_session_connect()`.
* SIGPIPE is ignored on driver threads
* Connection timeout now covers the entire connection handshake instead of just
  the socket connection.
* UUID API has been updated to use explict state via `CassUuidGen` instead of
  using internal global state. See `cass_uuid_gen_new()` and corresponding functions.

1.0.0-beta5
===========
Nov 20, 2014

Features
--------
* Added SSL support
* Added token-aware load balancing. It''s enabled by default. It can
  be disable using `cass_cluster_set_token_aware_routing()`.
* Added functions to get schema metadata, `cass_session_get_schema()` can
  be use to get a snapshot of the schema metadata and `cass_meta_*()` functions
  can be used to retrieve detailed information about keyspaces, tables, and columns.

Other
--------
* Significantly improved request throughput by batching socket writes.
  Batch sizes can be configured using the function
  `cass_cluster_set_max_requests_per_flush()`. The default is 128 because
  the maximum write size is bounded by the number of pipelined requests
  per connection (for version 1 and 2 of the native protocol).
* Added backpressure mechanism that can be configured for by the
  number of outstanding bytes and the number of requests on a connection. These can
  be configured using `cass_cluster_set_write_bytes_high_water_mark()`,
  `cass_cluster_set_write_bytes_low_water_mark()`,
  `cass_cluster_set_pending_requests_high_water_mark()`, and
  `cass_cluster_set_pending_requests_low_water_mark()`.
* Fixed a crash caused by a request timing out followed by a
  call to the connection read callback then the write callback.
* Fixed an issue that prevented the driver from recovering from
  a full cluster outage.
* Fixed an issue that allowed `uv_queue_work()` to be called from an
  application thread.
* Added logic to prevent redundant node refreshes when establishing
  a new connection from multiple IO workers.
* Fixed a crash caused by Cassandra 2.1.0 not returning metadata on
  execute requests.
* Fixed a memory leak that could leak request handlers if the control
  connection ran out of stream ids.
* Fixed a crash caused by calling an invalid timeout callback
  when waiting for connection to become available in a pool.
* Build will use an external version of Boost if present.

1.0.0-beta4
===========
Sep 11, 2014

Features
--------
* Added node discovery and now handle topology/status change events
* Queries that modify schema wait for cluster schema agreement
* Added DC aware load balancing policy. The default policy is set to
  round robin.
* Added new methods to configure the load balancing policy,
  `cass_cluster_set_load_balance_round_robin()` and
  `cass_cluster_set_load_balance_dc_aware()`
* It is now possible for a session future to return an error when
  connecting to a cluster. This can occur when the driver is unable to connect
  to any contact points or the provided authentication credentials are invalid.

1.0.0-beta3
===========
Aug 13, 2014

Features
--------
* Added callbacks to futures, `cass_future_set_callback()`
* Added plaintext authentication support, `cass_cluster_set_credentials()`
* Added map iterator, `cass_iterator_from_map()`, key and value pairs can be
  retrieved using `cass_iterator_get_map_key()` and `cass_iterator_get_map_value()`
* Values can be bound by name (statements created from prepared statements only)
* Values can be retrieved by name from result sets

Other
---------
* Reduced overhead of prepared statements
* Small performance improvement for all queries via reduced allocations

1.0.0-beta2
===========
Jul 17, 2014

Features
--------
* Removed C++1x dependency
* Support for Cassandra 1.2 (native protocol version 1)
* Support for automatic paging
* Added method to set serial consistency on statement
* Added batch type to batches
* Exposed options `cass_cluster_set_max_pending_requests()`,
  `cass_cluster_set_max_simultaneous_creation()`, and
  `cass_cluster_set_max_simultaneous_requests_threshold()`
* Added `cass_result_column_name()` to get column name from a result set

Bug Fixes
---------
* Fixed `StreamManager` so it no longer causes stack corruption
* Fixed issue where connections would log "'Timed out during startup' error on startup for ..."
  when a Session was closed while a connection was during its startup process
* Fixed Row and collections iterator so they return the first and last item properly
* Improved logic for creating new connections so it''s based on request throughput

Other
---------
* Removed the `setopt` method added set methods for all options
* Removed the `getopt` method
* `cass_batch_new()` and `cass_collection_new()` no longer take a consistency parameter
* `cass_collection_new()` now requires a collection type parameter
* `is_map` parameter removed from `cass_statement_bind_collection()`

1.0.0-beta1
===========
Jun 16, 2014

Features
--------
* Support for Cassandra 2.0 (native protocol version 2)
* Support for prepared and batch statements
* Support for collections
