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

# libssh2 related CMake options
option(LIBSSH2_INSTALL_PREFIX "libssh2 installation prefix location")
if(NOT LIBSSH2_INSTALL_PREFIX)
  set(LIBSSH2_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libs/libssh2" CACHE STRING "libssh2 installation prefix" FORCE)
endif()
option(LIBSSH2_VERSION "libssh2 version to build and install")
if(NOT LIBSSH2_VERSION)
  set(LIBSSH2_VERSION "1.9.0")
endif()
set(LIBSSH2_VERSION ${LIBSSH2_VERSION} CACHE STRING "libssh2 version to build and install" FORCE)

# Determine the libssh2 archive name to download
set(LIBSSH2_ARCHIVE_NAME libssh2-${LIBSSH2_VERSION} CACHE STRING "libssh2 archive name" FORCE)

# libssh2 external project variables
set(LIBSSH2_LIBRARY_NAME "libssh2-library")
set(LIBSSH2_PROJECT_PREFIX ${CMAKE_BINARY_DIR}/external/libssh2)
set(LIBSSH2_ARCHIVE_URL_PREFIX "https://github.com/libssh2/libssh2/archive/")
set(LIBSSH2_ARCHIVE_URL_SUFFIX ".tar.gz")
set(LIBSSH2_ARCHIVE_URL "${LIBSSH2_ARCHIVE_URL_PREFIX}${LIBSSH2_ARCHIVE_NAME}${LIBSSH2_ARCHIVE_URL_SUFFIX}")

# Make sure Visual Studio is available
if(NOT MSVC)
  message(FATAL_ERROR "Visual Studio is required to build libssh2")
endif()
message(STATUS "libssh2: v${LIBSSH2_VERSION}")

# libssh2 library configuration variables
set(LIBSSH2_INSTALL_DIR "${LIBSSH2_INSTALL_PREFIX}" CACHE STRING "libssh2 installation directory" FORCE)
set(LIBSSH2_BINARY_DIR "${LIBSSH2_INSTALL_DIR}/bin" CACHE STRING "libssh2 binary directory" FORCE)
set(LIBSSH2_INCLUDE_DIR "${LIBSSH2_INSTALL_DIR}/include" CACHE STRING "libssh2 include directory" FORCE)
set(LIBSSH2_INCLUDE_DIRS "${LIBSSH2_INCLUDE_DIR}" CACHE STRING "libssh2 include directory" FORCE) # Alias to stay consistent with FindLibssh2
set(LIBSSH2_LIBRARY_DIR "${LIBSSH2_INSTALL_DIR}/lib" CACHE STRING "libssh2 library directory" FORCE)
set(LIBSSH2_LIBRARIES ${LIBSSH2_LIBRARY_DIR}/libssh2.lib CACHE STRING "libssh2 libraries" FORCE)
set(LIBSSH2_ROOT_DIR "${LIBSSH2_INSTALL_DIR}" CACHE STRING "libssh2 root directory" FORCE)

# Create a package name for the binaries
set(LIBSSH2_PACKAGE_NAME "libssh2-${LIBSSH2_VERSION}-static-${PACKAGE_ARCH_TYPE}-msvc${VS_INTERNAL_VERSION}.zip" CACHE STRING "libssh2 package name" FORCE)

# Create an additional install script step for libssh2
file(TO_NATIVE_PATH ${LIBSSH2_BINARY_DIR} LIBSSH2_NATIVE_BINARY_DIR)
file(TO_NATIVE_PATH ${LIBSSH2_LIBRARY_DIR} LIBSSH2_NATIVE_LIBRARY_DIR)
set(LIBSSH2_INSTALL_EXTRAS_SCRIPT "${LIBSSH2_PROJECT_PREFIX}/scripts/install_libssh2_extras.bat")
file(REMOVE ${LIBSSH2_INSTALL_EXTRAS_SCRIPT})
file(WRITE ${LIBSSH2_INSTALL_EXTRAS_SCRIPT}
  "@REM Generated install script for libssh2\r\n"
  "@ECHO OFF\r\n"
  "IF EXIST src\\libssh2.dir\\RelWithDebInfo\\*.pdb (\r\n"
  "  COPY /Y src\\libssh2.dir\\RelWithDebInfo\\*.pdb \"${LIBSSH2_NATIVE_LIBRARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "EXIT /B\r\n")
set(LIBSSH2_INSTALL_EXTRAS_COMMAND "${LIBSSH2_INSTALL_EXTRAS_SCRIPT}")

# Add libssh2 as an external project
externalproject_add(${LIBSSH2_LIBRARY_NAME}
  PREFIX ${LIBSSH2_PROJECT_PREFIX}
  URL ${LIBSSH2_ARCHIVE_URL}
  DOWNLOAD_DIR ${LIBSSH2_PROJECT_PREFIX}
  INSTALL_DIR ${LIBSSH2_INSTALL_DIR}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBSSH2_INSTALL_DIR}
    -DBUILD_SHARED_LIBS=Off # Only build static for test linking
    -DBUILD_EXAMPLES=Off
    -DBUILD_TESTING=Off
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config RelWithDebInfo
  INSTALL_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config RelWithDebInfo --target install
    COMMAND ${LIBSSH2_INSTALL_EXTRAS_COMMAND}
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Update the include directory to use libssh2
include_directories(${LIBSSH2_INCLUDE_DIR})
