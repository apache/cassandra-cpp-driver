# Copyright (c) DataStax, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)
include(ExternalProject)
include(Windows-Environment)

# zlib related CMake options
option(ZLIB_VERSION "zlib version to build and install")
if(NOT ZLIB_VERSION)
  set(ZLIB_VERSION "1.2.11")
endif()
option(ZLIB_INSTALL_PREFIX "zlib installation prefix location")
if(NOT ZLIB_INSTALL_PREFIX)
  set(ZLIB_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libs/zlib" CACHE STRING "zlib installation prefix" FORCE)
endif()
set(ZLIB_VERSION ${ZLIB_VERSION} CACHE STRING "zlib version to build and install" FORCE)

# Determine the zlib archive name to download
set(ZLIB_ARCHIVE_NAME v${ZLIB_VERSION} CACHE STRING "zlib archive name" FORCE)

# zlib external project variables
set(ZLIB_LIBRARY_NAME "zlib-library")
set(ZLIB_PROJECT_PREFIX ${CMAKE_BINARY_DIR}/external/zlib)
set(ZLIB_ARCHIVE_URL_PREFIX "https://github.com/madler/zlib/archive/")
set(ZLIB_ARCHIVE_URL_SUFFIX ".tar.gz")
set(ZLIB_ARCHIVE_URL "${ZLIB_ARCHIVE_URL_PREFIX}${ZLIB_ARCHIVE_NAME}${ZLIB_ARCHIVE_URL_SUFFIX}")

# Make sure Visual Studio is available
if(NOT MSVC)
  message(FATAL_ERROR "Visual Studio is required to build zlib")
endif()
message(STATUS "zlib: v${ZLIB_VERSION}")

# zlib library configuration variables
set(ZLIB_INSTALL_DIR "${ZLIB_INSTALL_PREFIX}" CACHE STRING "zlib installation directory" FORCE)
set(ZLIB_BINARY_DIR "${ZLIB_INSTALL_DIR}/bin" CACHE STRING "zlib binary directory" FORCE)
set(ZLIB_INCLUDE_DIR "${ZLIB_INSTALL_DIR}/include" CACHE STRING "zlib include directory" FORCE)
set(ZLIB_INCLUDE_DIRS "${ZLIB_INCLUDE_DIR}" CACHE STRING "zlib include directory" FORCE) # Alias to stay consistent with FindZlib
set(ZLIB_LIBRARY_DIR "${ZLIB_INSTALL_DIR}/lib" CACHE STRING "zlib library directory" FORCE)
if(CASS_USE_ZLIB)
  set(ZLIB_LIBRARIES ${ZLIB_LIBRARY_DIR}/zlib.lib CACHE STRING "zlib libraries" FORCE)
  if(CASS_USE_STATIC_LIBS)
    set(ZLIB_LIBRARIES ${ZLIB_LIBRARY_DIR}/zlibstatic.lib CACHE STRING "zlib libraries" FORCE)
  endif()
endif()
set(ZLIB_ROOT_DIR "${ZLIB_INSTALL_DIR}" CACHE STRING "zlib root directory" FORCE)

# Create a package name for the binaries
set(ZLIB_PACKAGE_NAME "zlib-${ZLIB_VERSION}-${PACKAGE_ARCH_TYPE}-msvc${VS_INTERNAL_VERSION}.zip" CACHE STRING "zlib package name" FORCE)

# Create an additional install script step for zlib
file(TO_NATIVE_PATH ${ZLIB_LIBRARY_DIR} ZLIB_NATIVE_LIBRARY_DIR)
set(ZLIB_INSTALL_EXTRAS_SCRIPT "${ZLIB_PROJECT_PREFIX}/scripts/install_zlib_extras.bat")
file(REMOVE ${ZLIB_INSTALL_EXTRAS_SCRIPT})
file(WRITE ${ZLIB_INSTALL_EXTRAS_SCRIPT}
  "@REM Generated install script for zlib\r\n"
  "@ECHO OFF\r\n"
  "IF EXIST zlibstatic.dir\\RelWithDebInfo\\*.pdb (\r\n"
  "  COPY /Y zlibstatic.dir\\RelWithDebInfo\\*.pdb \"${ZLIB_NATIVE_LIBRARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "EXIT /B\r\n")
set(ZLIB_INSTALL_EXTRAS_COMMAND "${ZLIB_INSTALL_EXTRAS_SCRIPT}")

# Add zlib as an external project
externalproject_add(${ZLIB_LIBRARY_NAME}
  PREFIX ${ZLIB_PROJECT_PREFIX}
  URL ${ZLIB_ARCHIVE_URL}
  DOWNLOAD_DIR ${ZLIB_PROJECT_PREFIX}
  INSTALL_DIR ${ZLIB_INSTALL_DIR}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${ZLIB_INSTALL_DIR}
    -DBUILD_SHARED_LIBS=On
    -DASM686=Off # Disable assembly compiling (does not build on all compilers)
    -DASM64=Off # Disable assembly compiling (does not build on all compilers)
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config RelWithDebInfo
  INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config RelWithDebInfo --target install
    COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/README ${ZLIB_INSTALL_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy RelWithDebInfo/zlib.pdb ${ZLIB_BINARY_DIR}
    COMMAND ${ZLIB_INSTALL_EXTRAS_COMMAND}
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Update the include directory to use zlib
include_directories(${ZLIB_INCLUDE_DIR})
