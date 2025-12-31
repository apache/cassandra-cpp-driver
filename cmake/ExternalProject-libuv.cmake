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

# libuv related CMake options
option(LIBUV_INSTALL_PREFIX "libuv installation prefix location")
if(NOT LIBUV_INSTALL_PREFIX)
  set(LIBUV_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libs/libuv" CACHE STRING "libuv installation prefix" FORCE)
endif()
option(LIBUV_VERSION "libuv version to build and install")
if(NOT LIBUV_VERSION)
  set(LIBUV_VERSION "1.32.0")
endif()
set(LIBUV_VERSION ${LIBUV_VERSION} CACHE STRING "libuv version to build and install" FORCE)

# Determine the libuv archive name to download
set(LIBUV_ARCHIVE_NAME v${LIBUV_VERSION} CACHE STRING "libuv archive name" FORCE)

# libuv external project variables
set(LIBUV_LIBRARY_NAME "libuv-library")
set(LIBUV_PROJECT_PREFIX ${CMAKE_BINARY_DIR}/external/libuv)
set(LIBUV_ARCHIVE_URL_PREFIX "https://github.com/libuv/libuv/archive/")
set(LIBUV_ARCHIVE_URL_SUFFIX ".tar.gz")
set(LIBUV_ARCHIVE_URL "${LIBUV_ARCHIVE_URL_PREFIX}${LIBUV_ARCHIVE_NAME}${LIBUV_ARCHIVE_URL_SUFFIX}")

# Make sure Visual Studio is available
if(NOT MSVC)
  message(FATAL_ERROR "Visual Studio is required to build libuv")
endif()
message(STATUS "libuv: v${LIBUV_VERSION}")

# libuv library configuration variables
set(LIBUV_INSTALL_DIR "${LIBUV_INSTALL_PREFIX}" CACHE STRING "libuv installation directory" FORCE)
set(LIBUV_BINARY_DIR "${LIBUV_INSTALL_DIR}/bin" CACHE STRING "libuv binary directory" FORCE)
set(LIBUV_INCLUDE_DIR "${LIBUV_INSTALL_DIR}/include" CACHE STRING "libuv include directory" FORCE)
set(LIBUV_INCLUDE_DIRS "${LIBUV_INCLUDE_DIR}" CACHE STRING "libuv include directory" FORCE) # Alias to stay consistent with FindLibuv
set(LIBUV_LIBRARY_DIR "${LIBUV_INSTALL_DIR}/lib" CACHE STRING "libuv library directory" FORCE)
set(LIBUV_LIBRARIES ${LIBUV_LIBRARY_DIR}/uv.lib CACHE STRING "libuv libraries" FORCE)
if(CASS_USE_STATIC_LIBS)
  set(LIBUV_LIBRARIES ${LIBUV_LIBRARY_DIR}/uv_a.lib CACHE STRING "libuv libraries" FORCE)
endif()
set(LIBUV_ROOT_DIR "${LIBUV_INSTALL_DIR}" CACHE STRING "libuv root directory" FORCE)

# Create a package name for the binaries
set(LIBUV_PACKAGE_NAME "libuv-${LIBUV_VERSION}-${PACKAGE_ARCH_TYPE}-msvc${VS_INTERNAL_VERSION}.zip" CACHE STRING "libuv package name" FORCE)

# Create an additional install script step for libuv
file(TO_NATIVE_PATH ${LIBUV_BINARY_DIR} LIBUV_NATIVE_BINARY_DIR)
file(TO_NATIVE_PATH ${LIBUV_LIBRARY_DIR} LIBUV_NATIVE_LIBRARY_DIR)
set(LIBUV_INSTALL_EXTRAS_SCRIPT "${LIBUV_PROJECT_PREFIX}/scripts/install_libuv_extras.bat")
file(REMOVE ${LIBUV_INSTALL_EXTRAS_SCRIPT})
file(WRITE ${LIBUV_INSTALL_EXTRAS_SCRIPT}
  "@REM Generated install script for libuv\r\n"
  "@ECHO OFF\r\n"
  "IF EXIST RelWithDebInfo\\*.dll (\r\n"
  "  COPY /Y RelWithDebInfo\\*.dll \"${LIBUV_NATIVE_BINARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "IF EXIST RelWithDebInfo\\*.pdb (\r\n"
  "  COPY /Y RelWithDebInfo\\*.pdb \"${LIBUV_NATIVE_BINARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "IF EXIST uv_a.dir\\RelWithDebInfo\\*.pdb (\r\n"
  "  COPY /Y uv_a.dir\\RelWithDebInfo\\*.pdb \"${LIBUV_NATIVE_LIBRARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "IF EXIST RelWithDebInfo\\*.lib (\r\n"
  "  COPY /Y RelWithDebInfo\\*.lib \"${LIBUV_NATIVE_LIBRARY_DIR}\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "EXIT /B\r\n")
set(LIBUV_INSTALL_EXTRAS_COMMAND "${LIBUV_INSTALL_EXTRAS_SCRIPT}")

# Add libuv as an external project
externalproject_add(${LIBUV_LIBRARY_NAME}
  PREFIX ${LIBUV_PROJECT_PREFIX}
  URL ${LIBUV_ARCHIVE_URL}
  DOWNLOAD_DIR ${LIBUV_PROJECT_PREFIX}
  INSTALL_DIR ${LIBUV_INSTALL_DIR}
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${LIBUV_INSTALL_DIR}
    -DBUILD_SHARED_LIBS=On
    -DBUILD_TESTING=Off
  BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --config RelWithDebInfo
  INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBUV_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBUV_INCLUDE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBUV_LIBRARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory <SOURCE_DIR>/include ${LIBUV_INCLUDE_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/LICENSE ${LIBUV_INSTALL_DIR}
    COMMAND ${LIBUV_INSTALL_EXTRAS_COMMAND}
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Update the include directory to use libuv
include_directories(${LIBUV_INCLUDE_DIR})
