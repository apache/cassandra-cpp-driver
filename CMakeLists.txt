cmake_minimum_required(VERSION 2.8.12)

set(ROOT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CMAKE_TOOLCHAIN_FILE ${ROOT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake)

project(cassandra C CXX)

set(CASS_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(CASS_SRC_DIR "${CASS_ROOT_DIR}/src")
set(CASS_INCLUDE_DIR "${CASS_ROOT_DIR}/include")

# Ensure functions/modules are available
list(APPEND CMAKE_MODULE_PATH ${CASS_ROOT_DIR}/cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

#---------------
# Policies
#---------------

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

if (POLICY CMP0054)
  cmake_policy(SET CMP0054 NEW)
endif()

#---------------
# Options
#---------------

option(CASS_BUILD_EXAMPLES "Build examples" OFF)
option(CASS_BUILD_INTEGRATION_TESTS "Build integration tests" OFF)
option(CASS_BUILD_SHARED "Build shared library" ON)
option(CASS_BUILD_STATIC "Build static library" OFF)
option(CASS_BUILD_TESTS "Build tests" OFF)
option(CASS_BUILD_UNIT_TESTS "Build unit tests" OFF)
option(CASS_DEBUG_CUSTOM_ALLOC "Debug custom allocator" OFF)
option(CASS_INSTALL_HEADER "Install header file" ON)
option(CASS_INSTALL_HEADER_IN_SUBDIR "Install header file under 'include/cassandra'" OFF)
option(CASS_INSTALL_PKG_CONFIG "Install pkg-config file(s)" ON)
option(CASS_MULTICORE_COMPILATION "Enable multicore compilation" ON)
option(CASS_USE_BOOST_ATOMIC "Use Boost atomics library" OFF)
option(CASS_USE_KERBEROS "Use Kerberos" OFF)
option(CASS_USE_LIBSSH2 "Use libssh2 for integration tests" OFF)
option(CASS_USE_OPENSSL "Use OpenSSL" ON)
option(CASS_USE_STATIC_LIBS "Link static libraries when building executables" OFF)
option(CASS_USE_STD_ATOMIC "Use C++11 atomics library" OFF)
option(CASS_USE_ZLIB "Use zlib" ON)
option(CASS_USE_TIMERFD "Use timerfd (Linux only)" ON)

# Handle testing dependencies
if(CASS_BUILD_TESTS)
  # Enable integration and unit tests
  set(CASS_BUILD_INTEGRATION_TESTS ON)
  set(CASS_BUILD_UNIT_TESTS ON)
endif()

if(CASS_BUILD_INTEGRATION_TESTS OR CASS_BUILD_UNIT_TESTS)
  set(CASS_USE_OPENSSL ON) # Required for tests
  set(CASS_USE_KERBEROS ON) # Required for tests
endif()

# Determine which driver target should be used as a dependency
set(PROJECT_LIB_NAME_TARGET cassandra)
if(CASS_USE_STATIC_LIBS OR
   (WIN32 AND (CASS_BUILD_INTEGRATION_TESTS OR CASS_BUILD_UNIT_TESTS)))
  set(CASS_USE_STATIC_LIBS ON) # Not all driver internals are exported for test executable (e.g. CASS_EXPORT)
  set(CASS_BUILD_STATIC ON)
  set(PROJECT_LIB_NAME_TARGET cassandra_static)
endif()

# Ensure the driver is configured to build
if(NOT CASS_BUILD_SHARED AND NOT CASS_BUILD_STATIC)
  message(FATAL_ERROR "Driver is not Configured to Build: Ensure shared and/or static library is enabled")
endif()

if(CASS_DEBUG_CUSTOM_ALLOC AND CASS_USE_STATIC_LIBS)
  message(WARNING "Debugging the custom allocator while static linking the library can cause your application to fail")
endif()

#------------------------
# Dependencies
#------------------------

include(Dependencies)
include(ClangFormat)

#------------------------
# Project Version
#------------------------

file(STRINGS "${CASS_INCLUDE_DIR}/cassandra.h" _VERSION_PARTS
  REGEX "^#define[ \t]+CASS_VERSION_(MAJOR|MINOR|PATCH|SUFFIX)[ \t]+([0-9]+|\"([^\"]+)\")$")

foreach(part MAJOR MINOR PATCH SUFFIX)
  string(REGEX MATCH "CASS_VERSION_${part}[ \t]+([0-9]+|\"([^\"]+)\")"
    PROJECT_VERSION_${part} ${_VERSION_PARTS})
  # Extract version numbers
  if (PROJECT_VERSION_${part})
    string(REGEX REPLACE "CASS_VERSION_${part}[ \t]+([0-9]+|\"([^\"]+)\")" "\\1"
      PROJECT_VERSION_${part} ${PROJECT_VERSION_${part}})
  endif()
endforeach()

# Verify version parts
if(NOT PROJECT_VERSION_MAJOR AND NOT PROJECT_VERSION_MINOR)
  message(FATAL_ERROR "Unable to retrieve driver version from ${version_header_file}")
endif()

set(PROJECT_VERSION_STRING
  ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR})
if(NOT PROJECT_VERSION_PATCH STREQUAL "")
  set(PROJECT_VERSION_STRING
    "${PROJECT_VERSION_STRING}.${PROJECT_VERSION_PATCH}")
endif()
if(NOT PROJECT_VERSION_SUFFIX STREQUAL "")
  string(REPLACE "\"" ""
    PROJECT_VERSION_SUFFIX ${PROJECT_VERSION_SUFFIX})
  set(PROJECT_VERSION_STRING
    "${PROJECT_VERSION_STRING}-${PROJECT_VERSION_SUFFIX}")
endif()

message(STATUS "Driver version: ${PROJECT_VERSION_STRING}")

#------------------------
# Determine atomic implementation
#------------------------

# Determine if std::atomic can be used for GCC, Clang, or MSVC
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
    # Version determined from: https://gcc.gnu.org/wiki/Atomic/GCCMM
    if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "4.7" OR
        CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "4.7")
      set(CASS_USE_STD_ATOMIC ON)
    endif()
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
  # Version determined from: http://clang.llvm.org/cxx_status.html
  # 3.2 includes the full C++11 memory model, but 3.1 had atomic
  # support.
  if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "3.1" OR
      CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "3.1")
    set(CASS_USE_STD_ATOMIC ON)
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  # Version determined from https://msdn.microsoft.com/en-us/library/hh874894
  # VS2012+/VS 11.0+/WindowsSDK v8.0+
  if(MSVC_VERSION GREATER 1700 OR
      MSVC_VERSION EQUAL 1700)
    set(CASS_USE_STD_ATOMIC ON)
  endif()
endif()

if(CASS_USE_BOOST_ATOMIC)
  message(STATUS "Using boost::atomic implementation for atomic operations")
elseif(CASS_USE_STD_ATOMIC)
  message(STATUS "Using std::atomic implementation for atomic operations")
endif()

#------------------------
# Top-level compiler flags
#------------------------

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR
   "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
  # Enable C++11 support to use std::atomic
  if(CASS_USE_STD_ATOMIC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
  endif()

  # OpenSSL is deprecated on later versions of Mac OS X. The long-term solution
  # is to provide a CommonCryto implementation.
  if (APPLE)
    set(CMAKE_MACOSX_RPATH 1)
    if(CASS_USE_OPENSSL)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
    endif()
  endif()

  if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang") 
    # Clang/Intel specific compiler options
    # I disabled long-long warning because boost generates about 50 such warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-long-long -Wno-unused-parameter")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-variadic-macros -Wno-zero-length-array")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedef -Wno-unknown-warning-option")
  else() 
    # GCC specific compiler options
    # I disabled long-long warning because boost generates about 50 such warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pedantic -Wextra -Wno-long-long -Wno-unused-parameter -Wno-variadic-macros")

    if(CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "4.8" OR
        CMAKE_CXX_COMPILER_VERSION VERSION_GREATER "4.8")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs")
    endif()
  endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
  add_definitions(/we4800)

  # Determine if multicore compilation should be enabled
  if(CASS_MULTICORE_COMPILATION)
    # Default multicore compilation with effective processors (see https://msdn.microsoft.com/en-us/library/bb385193.aspx)
    add_definitions("/MP")
  endif()

  # On Visual C++ -pedantic flag is not used,
  # -fPIC is not used on Windows platform (all DLLs are
  # relocable), -Wall generates about 30k stupid warnings
  # that can hide useful ones.
  # Create specific warning disable compiler flags
  # TODO(mpenick): Fix these "possible loss of data" warnings
  add_definitions(/wd4244)
  add_definitions(/wd4267)

  # Add preprocessor definitions for proper compilation
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)  # Remove warnings for not using safe functions (TODO: Fix codebase to be more secure for Visual Studio)
  add_definitions(-DNOMINMAX)                 # Does not define min/max macros
  add_definitions(-D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING) # Remove warnings for TR1 deprecation (Visual Studio 15 2017); caused by sparsehash
else()
  message(FATAL_ERROR "Unsupported compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

#------------------------
# Subdirectories
#------------------------

add_subdirectory(src)

if(CASS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(CASS_BUILD_INTEGRATION_TESTS OR CASS_BUILD_UNIT_TESTS)
  add_subdirectory(tests)
endif()
