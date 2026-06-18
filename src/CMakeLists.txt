include(CheckSymbolExists)
include(CheckCXXSourceCompiles)

file(GLOB SOURCES *.cpp *.hpp)

if(APPLE)
  list(REMOVE_ITEM SOURCES ${CASS_SRC_DIR}/get_time-unix.cpp ${CASS_SRC_DIR}/get_time-win.cpp)
elseif(UNIX)
  list(REMOVE_ITEM SOURCES ${CASS_SRC_DIR}/get_time-mac.cpp ${CASS_SRC_DIR}/get_time-win.cpp)
elseif(WIN32)
  list(REMOVE_ITEM SOURCES ${CASS_SRC_DIR}/get_time-mac.cpp ${CASS_SRC_DIR}/get_time-unix.cpp)
endif()

if(CASS_USE_OPENSSL)
  list(APPEND SOURCES ssl/ssl_openssl_impl.cpp ssl/ring_buffer_bio.cpp)
else()
  list(APPEND SOURCES ssl/ssl_no_impl.cpp)
endif()

if(CASS_USE_KERBEROS)
  list(APPEND INCLUDE_DIRS gssapi)
  list(APPEND SOURCES gssapi/dse_auth_gssapi.cpp gssapi/dse_auth_gssapi.hpp)
endif()

# Determine atomic library to include
if(CASS_USE_BOOST_ATOMIC)
  list(APPEND SOURCES atomic/atomic_boost.hpp)
elseif(CASS_USE_STD_ATOMIC)
  list(APPEND SOURCES atomic/atomic_std.hpp)
else()
  list(APPEND SOURCES atomic/atomic_intrinsics.hpp)
  if(WIN32)
    list(APPEND SOURCES atomic/atomic_intrinsics_msvc.hpp)
  else()
    list(APPEND SOURCES atomic/atomic_intrinsics_gcc.hpp)
  endif()
endif()

add_subdirectory(third_party/curl)
add_subdirectory(third_party/hdr_histogram)
add_subdirectory(third_party/http-parser)
add_subdirectory(third_party/minizip)
add_subdirectory(third_party/sparsehash)

list(APPEND INCLUDE_DIRS 
  third_party/curl
  third_party/hdr_histogram
  third_party/http-parser
  third_party/minizip
  third_party/mt19937_64
  third_party/rapidjson/rapidjson
  third_party/sparsehash/src)

list(APPEND INCLUDE_DIRS ${CASS_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

#------------------------------
# Compiler flags
#------------------------------

# Ensure Windows platform is supported
if(WIN32)
  if(CMAKE_SYSTEM_VERSION GREATER 5.2 OR
      CMAKE_SYSTEM_VERSION EQUAL 5.2)
    add_definitions(-D_WIN32_WINNT=0x502)
  else()
    string(REPLACE "." "" WINDOWS_VERSION ${CMAKE_SYSTEM_VERSION})
    string(REGEX REPLACE "([0-9])" "0\\1" WINDOWS_VERSION ${WINDOWS_VERSION})
    message(FATAL_ERROR "Unable to build driver: Unsupported Windows platform 0x${WINDOWS_VERSION}")
  endif()
endif()

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang$")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wconversion -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-undefined-var-template -Werror")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-implicit-int-float-conversion")
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU") # To many superfluous warnings generated with GCC when using -Wconversion (see: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=40752)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")
endif()

#------------------------------
# Build configured header
#------------------------------

# Determine random availability
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
  check_symbol_exists(GRND_NONBLOCK "linux/random.h" HAVE_GETRANDOM)
  if(CASS_USE_TIMERFD)
    check_symbol_exists(timerfd_create "sys/timerfd.h" HAVE_TIMERFD)
  endif()
else()
  check_symbol_exists(arc4random_buf "stdlib.h" HAVE_ARC4RANDOM)
endif()

# Determine if sigpipe is available
check_symbol_exists(SO_NOSIGPIPE "sys/socket.h;sys/types.h" HAVE_NOSIGPIPE)
check_symbol_exists(sigtimedwait "signal.h" HAVE_SIGTIMEDWAIT)
if (NOT WIN32 AND NOT HAVE_NOSIGPIPE AND NOT HAVE_SIGTIMEDWAIT)
  message(WARNING "Unable to handle SIGPIPE on your platform")
endif()

# Determine if hash is in the tr1 namespace
string(REPLACE "::" ";" HASH_NAMESPACE_LIST ${HASH_NAMESPACE})
foreach(NAMESPACE ${HASH_NAMESPACE_LIST})
  if(NAMESPACE STREQUAL "tr1")
    set(HASH_IN_TR1 1)
  endif()
endforeach()

# Check for GCC compiler builtins
if(NOT "${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  check_cxx_source_compiles("int main() { return __builtin_bswap32(42); }" HAVE_BUILTIN_BSWAP32)
  check_cxx_source_compiles("int main() { return __builtin_bswap64(42); }" HAVE_BUILTIN_BSWAP64)
endif()

set(HAVE_BOOST_ATOMIC ${CASS_USE_BOOST_ATOMIC})
set(HAVE_STD_ATOMIC ${CASS_USE_STD_ATOMIC})
set(HAVE_KERBEROS ${CASS_USE_KERBEROS})
set(HAVE_OPENSSL ${CASS_USE_OPENSSL})
set(HAVE_ZLIB ${CASS_USE_ZLIB})

# Generate the driver_config.hpp file
configure_file(
  ${CASS_ROOT_DIR}/driver_config.hpp.in 
  ${CMAKE_CURRENT_SOURCE_DIR}/driver_config.hpp)


#------------------------------
# Targets
#------------------------------

if(CASS_BUILD_SHARED)
  add_library(cassandra SHARED 
    ${SOURCES}
    $<TARGET_OBJECTS:curl_hostcheck>
    $<TARGET_OBJECTS:hdr_histogram>
    $<TARGET_OBJECTS:http-parser>
    $<TARGET_OBJECTS:minizip>)
  target_link_libraries(cassandra ${CASS_LIBS})
  target_include_directories(cassandra PRIVATE ${INCLUDE_DIRS} ${CASS_INCLUDES})

  set_target_properties(cassandra PROPERTIES OUTPUT_NAME cassandra)
  set_target_properties(cassandra PROPERTIES VERSION ${PROJECT_VERSION_STRING} SOVERSION ${PROJECT_VERSION_MAJOR})
  set_target_properties(cassandra PROPERTIES
      COMPILE_PDB_NAME "cassandra"
      COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_target_properties(cassandra PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set_target_properties(cassandra PROPERTIES FOLDER "Driver")

  target_compile_definitions(cassandra PRIVATE CASS_BUILDING)

  if(CASS_USE_BOOST_ATOMIC AND BOOST_LIBRARY_NAME)
    add_dependencies(cassandra ${BOOST_LIBRARY_NAME})
  endif()
  if(LIBUV_LIBRARY_NAME)
    add_dependencies(cassandra ${LIBUV_LIBRARY_NAME})
  endif()
  if(OPENSSL_LIBRARY_NAME)
    add_dependencies(cassandra ${OPENSSL_LIBRARY_NAME})
  endif()
  if(ZLIB_LIBRARY_NAME)
    add_dependencies(cassandra ${ZLIB_LIBRARY_NAME})
  endif()
endif()

if(CASS_BUILD_STATIC)
  add_library(cassandra_static STATIC
    ${SOURCES}
    $<TARGET_OBJECTS:curl_hostcheck_static>
    $<TARGET_OBJECTS:hdr_histogram_static>
    $<TARGET_OBJECTS:http-parser_static>
    $<TARGET_OBJECTS:minizip_static>)
  target_link_libraries(cassandra_static ${CASS_LIBS})
  target_include_directories(cassandra_static PRIVATE ${INCLUDE_DIRS} ${CASS_INCLUDES})

  set_target_properties(cassandra_static PROPERTIES OUTPUT_NAME cassandra_static)
  set_target_properties(cassandra_static PROPERTIES VERSION ${PROJECT_VERSION_STRING} SOVERSION ${PROJECT_VERSION_MAJOR})
  set_target_properties(cassandra_static PROPERTIES
      COMPILE_PDB_NAME "cassandra_static"
      COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_target_properties(cassandra_static PROPERTIES
      ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
  set_target_properties(cassandra_static PROPERTIES FOLDER "Driver")

  target_compile_definitions(cassandra_static PRIVATE CASS_STATIC)

  if(CASS_USE_BOOST_ATOMIC AND BOOST_LIBRARY_NAME)
    add_dependencies(cassandra_static ${BOOST_LIBRARY_NAME})
  endif()
  if(LIBUV_LIBRARY_NAME)
    add_dependencies(cassandra_static ${LIBUV_LIBRARY_NAME})
  endif()
  if(OPENSSL_LIBRARY_NAME)
    add_dependencies(cassandra_static ${OPENSSL_LIBRARY_NAME})
  endif()
  if(ZLIB_LIBRARY_NAME)
    add_dependencies(cassandra_static ${ZLIB_LIBRARY_NAME})
  endif()
endif()

#-------------------------------------
# Installation
#-------------------------------------

# Determine if the library directory needs to be determined
if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  if ("${CMAKE_SYSTEM_NAME}" MATCHES "Linux" AND ("${CMAKE_INSTALL_PREFIX}" STREQUAL "/usr" OR
    "${CMAKE_INSTALL_PREFIX}" STREQUAL "/usr/local"))
    if(EXISTS "/etc/debian_version")
      set (CMAKE_INSTALL_LIBDIR "lib/${CMAKE_LIBRARY_ARCHITECTURE}")
    elseif(EXISTS "/etc/redhat-release" OR EXISTS "/etc/fedora-release" OR
        EXISTS "/etc/slackware-version" OR EXISTS "/etc/gentoo-release")
      if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set (CMAKE_INSTALL_LIBDIR "lib64")
      else()
        set (CMAKE_INSTALL_LIBDIR "lib")
      endif()
    else()
      set (CMAKE_INSTALL_LIBDIR "lib")
    endif()
  else()
    set (CMAKE_INSTALL_LIBDIR "lib")
  endif()
endif()

# Create a binary directory executable and DLLs (windows only)
set(INSTALL_DLL_EXE_DIR "bin") # Determine the header install dir
if (CASS_INSTALL_HEADER_IN_SUBDIR)
  if (CASS_INSTALL_HEADER_SUBDIR_NAME)
    # User-specified include sub-dir
    set(INSTALL_HEADER_DIR "include/${CASS_INSTALL_HEADER_SUBDIR_NAME}")
  else()
    # Default subdir location is 'include/cassandra'
    set(INSTALL_HEADER_DIR "include/cassandra")
  endif()
else()
  # Default header install location is 'include'
  set(INSTALL_HEADER_DIR "include")
endif()

if(CASS_INSTALL_PKG_CONFIG)
  if(NOT WIN32)
    find_package(PkgConfig)
    if(PKG_CONFIG_FOUND)
      set(prefix ${CMAKE_INSTALL_PREFIX})
      set(exec_prefix ${CMAKE_INSTALL_PREFIX})
      set(libdir ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
      set(includedir ${CMAKE_INSTALL_PREFIX}/${INSTALL_HEADER_DIR})
      set(version ${PROJECT_VERSION_STRING})
    endif()
  endif()
endif()

# Determine if the header should be installed
if(CASS_INSTALL_HEADER)
  file(GLOB CASS_API_HEADER_FILES ${CASS_INCLUDE_DIR}/*.h)
  install(FILES ${CASS_API_HEADER_FILES} DESTINATION ${INSTALL_HEADER_DIR})
endif()

# Install the dynamic/shared library
if(CASS_BUILD_SHARED)
  install(TARGETS cassandra
    RUNTIME DESTINATION ${INSTALL_DLL_EXE_DIR}  # for dll/executable/pdb files
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # for shared library
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}) # for static library
  if(CASS_INSTALL_PKG_CONFIG)
    if(NOT WIN32)
      if(PKG_CONFIG_FOUND)
        configure_file("${PROJECT_SOURCE_DIR}/packaging/cassandra.pc.in" "cassandra.pc" @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cassandra.pc"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
      endif()
    endif()
  endif()
  if(WIN32)
    install(FILES $<TARGET_PDB_FILE:cassandra>
      DESTINATION "${INSTALL_DLL_EXE_DIR}"
      OPTIONAL)
  endif()
endif()

if(CASS_BUILD_STATIC)
  install(TARGETS cassandra_static
    RUNTIME DESTINATION ${INSTALL_DLL_EXE_DIR}  # for dll/executable/pdb files
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}  # for shared library
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}) # for static library
  if(CASS_INSTALL_PKG_CONFIG)
    if(NOT WIN32)
      if(PKG_CONFIG_FOUND)
        configure_file("${PROJECT_SOURCE_DIR}/packaging/cassandra_static.pc.in" "cassandra_static.pc" @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/cassandra_static.pc"
          DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
      endif()
    endif()
  endif()
endif()

#-------------------
# Uninstall target
#-------------------

configure_file(
  "${CASS_ROOT_DIR}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

# FIXME
# Determine if all GNU extensions should be enabled
#if("${CMAKE_SYSTEM_NAME}" MATCHES "Linux")
#  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
#endif()
