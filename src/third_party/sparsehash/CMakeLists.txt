cmake_minimum_required(VERSION 3.5)

include(CheckCXXSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckTypeSize)

include(TestCXXAcceptsFlag)

set(HASH_NAME hash)

set(SAVED_CMAKE_REQUIRED_QUIET ${CMAKE_REQUIRED_FLAGS_QUIET})
set(SAVED_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})

set(CMAKE_REQUIRED_QUIET TRUE)

check_cxx_accepts_flag("-std=c++11" HAVE_CXX11)
if(HAVE_CXX11)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -std=c++11")
endif()

foreach(LOCATION unordered_map;tr1/unordered_map)
  if (HASH_NAMESPACE)
    break()
  endif()
  foreach(NAMESPACE std;std::tr1)
    unset(HAVE_UNORDERED_MAP CACHE)
    check_cxx_source_compiles(
      "
      #include <${LOCATION}>
      int main() {
        ${NAMESPACE}::unordered_map<int,int> m;
        return m.find(42) == m.end();
      }
      "
      HAVE_UNORDERED_MAP)
    if(${HAVE_UNORDERED_MAP})
      set(HASH_NAMESPACE ${NAMESPACE})
    endif()
  endforeach(NAMESPACE)
endforeach(LOCATION)

foreach(LOCATION hash_map;ext/hash_map)
  if (HASH_NAMESPACE)
    break()
  endif()
  foreach(NAMESPACE std;stdext)
    if (HASH_NAMESPACE)
      break()
    endif()
    unset(HAVE_HASH_MAP CACHE)
    check_cxx_source_compiles(
      "
      #include <${LOCATION}>
      int main() {
        ${NAMESPACE}::unordered_map<int,int> m;
        return m.find(42) == m.end();
      }
      "
      HAVE_HASH_MAP)
    if(${HAVE_HASH_MAP})
      set(HASH_NAMESPACE ${NAMESPACE})
    endif()
  endforeach(NAMESPACE)
endforeach(LOCATION)

if(NOT HASH_NAMESPACE)
  message(FATAL_ERROR "Unable to determine the standard hash namespace")
endif()

foreach(LOCATION functional;tr1/functional;ext/hash_fun.h;ext/stl_hash_fun.h;hash_fun.h;stl_hash_fun.h;stl/_hash_fun.h)
  if (HASH_FUN_H)
    break()
  endif()
  unset(HAVE_HASH_FUNC_HEADER CACHE)
  check_cxx_source_compiles(
    "
    #include <${LOCATION}>
    int main() { int x = ${HASH_NAMESPACE}::hash<int>()(42); }
    "
    HAVE_HASH_FUNC_HEADER)
  if (${HAVE_HASH_FUNC_HEADER})
    set(HASH_FUN_H ${LOCATION})
  endif()
endforeach(LOCATION)

if(NOT HASH_FUN_H)
  message(FATAL_ERROR "Unable to find standard hash header file")
endif()

message(STATUS "Using hash header <${HASH_FUN_H}> and namespace \"${HASH_NAMESPACE}\"")
set(HASH_NAMESPACE ${HASH_NAMESPACE} PARENT_SCOPE)
set(HASH_FUN_H ${HASH_FUN_H} PARENT_SCOPE)

set(CMAKE_REQUIRED_QUIET ${SAVED_CMAKE_REQUIRED_QUIET})
set(CMAKE_REQUIRED_FLAGS ${SAVED_CMAKE_REQUIRED_FLAGS})

check_include_file(inttypes.h HAVE_INTTYPES_H)
check_include_file(stdint.h HAVE_STDINT_H)
check_include_file(sys/types.h HAVE_SYS_TYPES_H)

check_function_exists(memcpy HAVE_MEMCPY)

check_type_size("long long" LONG_LONG)
check_type_size("uint16_t" UINT16_T)
check_type_size("u_int16_t" U_INT16_T)
check_type_size("__uint16_t" __UINT16_T)

configure_file("config.h.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/src/sparsehash/internal/sparseconfig.h")

file(GLOB SPARSEHASH_HEADER_FILES sparsehash)
file(GLOB SPARSEHASH_INTERNAL_HEADER_FILES sparsehash/*.h sparsehash/internal/*.h)

set(SOURCES 
  ${SPARSEHASH_HEADER_FILES} 
  ${SPARSEHASH_INTERNAL_HEADER_FILES})

source_group("Header Files\\" FILES ${SPARSEHASH_HEADER_FILES})
source_group("Header Files\\internal" FILES ${SPARSEHASH_INTERNAL_HEADER_FILES})

add_custom_target(sparsehash ${SOURCES})
set_target_properties(sparsehash PROPERTIES 
  FOLDER "Driver/Dependencies")
