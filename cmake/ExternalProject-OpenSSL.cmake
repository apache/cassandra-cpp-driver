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
if(NOT MSVC_ENVIRONMENT_SCRIPT)
  message(FATAL_ERROR "Visual Studio environment script is required to build OpenSSL")
endif()

# OpenSSL related CMake options
option(OPENSSL_VERSION "OpenSSL version to build and install")
if(NOT OPENSSL_VERSION)
  # TODO: Should we default to OpenSSL 1.1 (e.g. 1.1.1c)?
  set(OPENSSL_VERSION "1.0.2s")
endif()
option(OPENSSL_INSTALL_PREFIX "OpenSSL installation prefix location")
if(CASS_USE_ZLIB)
  include(ExternalProject-zlib)
endif()
set(OPENSSL_VERSION ${OPENSSL_VERSION} CACHE STRING "OpenSSL version to build and install" FORCE)

# Determine the major and minor version of OpenSSL used
string(REPLACE "." ";" OPENSSL_VERSION_LIST ${OPENSSL_VERSION})
list(GET OPENSSL_VERSION_LIST 0 OPENSSL_VERSION_MAJOR)
list(GET OPENSSL_VERSION_LIST 1 OPENSSL_VERSION_MINOR)
set(OPENSSL_MAJOR_MINOR_VERSION "${OPENSSL_VERSION_MAJOR}.${OPENSSL_VERSION_MINOR}")

# Determine the OpenSSL archive name to download
string(REPLACE "." "_" OPENSSL_ARCHIVE_VERSION ${OPENSSL_VERSION})
set(OPENSSL_ARCHIVE_NAME OpenSSL_${OPENSSL_ARCHIVE_VERSION} CACHE STRING "OpenSSL archive name" FORCE)

# Determine if OpenSSL installation directory should be set
if(NOT OPENSSL_INSTALL_PREFIX)
  set(OPENSSL_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/libs/openssl" CACHE STRING "OpenSSL installation prefix" FORCE)
endif()

# OpenSSL external project variables
set(OPENSSL_LIBRARY_NAME "openssl-${OPENSSL_VERSION}-library")
set(OPENSSL_PROJECT_PREFIX ${CMAKE_BINARY_DIR}/external/openssl)
set(OPENSSL_ARCHIVE_URL_PREFIX "https://github.com/openssl/openssl/archive/")
set(OPENSSL_ARCHIVE_URL_SUFFIX ".tar.gz")
set(OPENSSL_ARCHIVE_URL "${OPENSSL_ARCHIVE_URL_PREFIX}${OPENSSL_ARCHIVE_NAME}${OPENSSL_ARCHIVE_URL_SUFFIX}")

# Make sure Visual Studio is available
if(NOT MSVC)
  message(FATAL_ERROR "Visual Studio is required to build OpenSSL")
endif()

# Perl is required to create NMake files for OpenSSL build
find_package(Perl REQUIRED)
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.1")
  if(PERL_VERSION_STRING VERSION_LESS "5.10.0")
    message(FATAL_ERROR "Invalid Perl Version: OpenSSL v1.1.0+ requires Perl v5.10.0+")
  endif()
endif()
if(CYGWIN_INSTALL_PATH)
  if(PERL_EXECUTABLE MATCHES "${CYGWIN_INSTALL_PATH}")
    message(FATAL_ERROR "Cygwin Perl Executable Found: Please install Strawberry Perl [http://strawberryperl.com/releases.html]")
  endif()
endif()
get_filename_component(PERL_PATH ${PERL_EXECUTABLE} DIRECTORY)
file(TO_NATIVE_PATH ${PERL_PATH} PERL_PATH)

message(STATUS "OpenSSL: ${OPENSSL_VERSION}")

# OpenSSL library configuration variables
set(OPENSSL_INSTALL_DIR "${OPENSSL_INSTALL_PREFIX}" CACHE STRING "OpenSSL installation directory" FORCE)
set(OPENSSL_BINARY_DIR "${OPENSSL_INSTALL_DIR}/bin" CACHE STRING "OpenSSL binary directory" FORCE)
set(OPENSSL_INCLUDE_DIR "${OPENSSL_INSTALL_DIR}/include" CACHE STRING "OpenSSL include directory" FORCE)
set(OPENSSL_INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR}" CACHE STRING "OpenSSL include directory" FORCE) # Alias to stay consistent with FindOpenSSL
set(OPENSSL_LIBRARY_DIR "${OPENSSL_INSTALL_DIR}/lib" CACHE STRING "OpenSSL library directory" FORCE)
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.0")
  set(OPENSSL_LIBRARIES ${OPENSSL_LIBRARY_DIR}/libeay32.lib
                        ${OPENSSL_LIBRARY_DIR}/ssleay32.lib
                        CACHE STRING "OpenSSL libraries" FORCE)
else()
  set(OPENSSL_LIBRARIES ${OPENSSL_LIBRARY_DIR}/libcrypto.lib
                        ${OPENSSL_LIBRARY_DIR}/libssl.lib
                        CACHE STRING "OpenSSL libraries" FORCE)
endif()
set(OPENSSL_ROOT_DIR "${OPENSSL_INSTALL_DIR}" CACHE STRING "OpenSSL root directory" FORCE)

# Create build options for the platform build scripts
if(BUILD_SHARED_LIBS)
  if(CASS_USE_ZLIB)
    set(OPENSSL_ZLIB_CONFIGURE_ARGUMENT "zlib-dynamic")
    set(ZLIB_LIB zlib.lib)
  endif()
else()
  if(CASS_USE_ZLIB)
    set(OPENSSL_ZLIB_CONFIGURE_ARGUMENT "no-zlib-dynamic")
    set(ZLIB_LIB zlibstatic.lib)
  endif()
endif()

# Determine if shared or static library should be built
set(OPENSSL_CONFIGURE_COMPILER "no-asm no-ssl2")
if(BUILD_SHARED_LIBS)
  set(OPENSSL_CONFIGURE_COMPILER "${OPENSSL_CONFIGURE_COMPILER} shared")
  set(OPENSSL_1_0_MAKEFILE "ms\\ntdll.mak")
  set(OPENSSL_1_1_MAKEFILE "makefile.shared")
else()
  set(OPENSSL_CONFIGURE_COMPILER "${OPENSSL_CONFIGURE_COMPILER} no-shared")
  set(OPENSSL_1_0_MAKEFILE "ms\\nt.mak")
  set(OPENSSL_1_1_MAKEFILE "makefile")
endif()

# Determine which compiler to use for configuration script
if(CMAKE_CL_64)
  set(OPENSSL_CONFIGURE_COMPILER "${OPENSSL_CONFIGURE_COMPILER} VC-WIN64A")
  set(OPENSSL_1_0_CONFIGURE_MAKEFILE_SCRIPT "ms\\do_win64a.bat")
else()
  set(OPENSSL_CONFIGURE_COMPILER "${OPENSSL_CONFIGURE_COMPILER} VC-WIN32")
  set(OPENSSL_1_0_CONFIGURE_MAKEFILE_SCRIPT "ms\\do_ms.bat")
endif()

# Determine which Makefile is being used
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.0")
  set(OPENSSL_MAKEFILE ${OPENSSL_1_0_MAKEFILE})
else()
  set(OPENSSL_MAKEFILE ${OPENSSL_1_1_MAKEFILE})
endif()

# Create a package name for the binaries
set(OPENSSL_PACKAGE_NAME "openssl-${OPENSSL_VERSION}-${PACKAGE_BUILD_TYPE}-${PACKAGE_ARCH_TYPE}-msvc${VS_INTERNAL_VERSION}.zip" CACHE STRING "OpenSSL package name" FORCE)

# Create a place holder for the configure script (source_dir property required from external project)
set(OPENSSL_CONFIGURE_SCRIPT "${OPENSSL_PROJECT_PREFIX}/scripts/configure_openssl.bat")
set(OPENSSL_CONFIGURE_COMMAND "${OPENSSL_CONFIGURE_SCRIPT}")

# Create the make script
set(OPENSSL_MAKE_SCRIPT "${OPENSSL_PROJECT_PREFIX}/scripts/make_openssl.bat")
file(REMOVE ${OPENSSL_MAKE_SCRIPT})
file(WRITE ${OPENSSL_MAKE_SCRIPT}
  "@REM Generated make script for OpenSSL\r\n"
  "@ECHO OFF\r\n"
  "SETLOCAL ENABLEDELAYEDEXPANSION\r\n"
  "PUSHD .\r\n")
if(DEFINED ENV{APPVEYOR} AND CMAKE_CL_64)
  file(APPEND ${OPENSSL_MAKE_SCRIPT}
    "CALL \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Bin\\SetEnv.cmd\" /x64\r\n"
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\" ${MSVC_ENVIRONMENT_ARCH}\r\n")
else()
  file(APPEND ${OPENSSL_MAKE_SCRIPT}
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\"\r\n")
endif()
file(APPEND ${OPENSSL_MAKE_SCRIPT}
  "POPD\r\n")
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.0")
  file(APPEND ${OPENSSL_MAKE_SCRIPT}
    "NMake /F \"${OPENSSL_1_0_MAKEFILE}\"\r\n")
else()
  file(APPEND ${OPENSSL_MAKE_SCRIPT}
    "NMake\r\n")
endif()
file(APPEND ${OPENSSL_MAKE_SCRIPT}
  "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "  ENDLOCAL\r\n"
  "  EXIT /B 1\r\n"
  ")\r\n"
  "ENDLOCAL\r\n"
  "EXIT /B\r\n")
set(OPENSSL_BUILD_COMMAND "${OPENSSL_MAKE_SCRIPT}")

# Create the install script
file(TO_NATIVE_PATH ${OPENSSL_BINARY_DIR} OPENSSL_NATIVE_BINARY_DIR)
file(TO_NATIVE_PATH ${OPENSSL_INCLUDE_DIR} OPENSSL_NATIVE_INCLUDE_DIR)
file(TO_NATIVE_PATH ${OPENSSL_INSTALL_DIR} OPENSSL_NATIVE_INSTALL_DIR)
file(TO_NATIVE_PATH ${OPENSSL_LIBRARY_DIR} OPENSSL_NATIVE_LIBRARY_DIR)
set(OPENSSL_INSTALL_SCRIPT "${OPENSSL_PROJECT_PREFIX}/scripts/install_openssl.bat")
file(REMOVE ${OPENSSL_INSTALL_SCRIPT})
file(WRITE ${OPENSSL_INSTALL_SCRIPT}
  "@REM Generated install script for OpenSSL\r\n"
  "@ECHO OFF\r\n"
  "SETLOCAL ENABLEDELAYEDEXPANSION\r\n"
  "PUSHD .\r\n")
if(DEFINED ENV{APPVEYOR} AND CMAKE_CL_64)
  file(APPEND ${OPENSSL_INSTALL_SCRIPT}
    "CALL \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Bin\\SetEnv.cmd\" /x64\r\n"
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\" ${MSVC_ENVIRONMENT_ARCH}\r\n")
else()
  file(APPEND ${OPENSSL_INSTALL_SCRIPT}
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\"\r\n")
endif()
file(APPEND ${OPENSSL_INSTALL_SCRIPT}
  "POPD\r\n"
  "CALL :SHORTENPATH \"${OPENSSL_NATIVE_BINARY_DIR}\" SHORTENED_OPENSSL_BINARY_DIR\r\n"
  "CALL :SHORTENPATH \"${OPENSSL_NATIVE_INCLUDE_DIR}\" SHORTENED_OPENSSL_INCLUDE_DIR\r\n"
  "CALL :SHORTENPATH \"${OPENSSL_NATIVE_INSTALL_DIR}\" SHORTENED_OPENSSL_INSTALL_DIR\r\n"
  "CALL :SHORTENPATH \"${OPENSSL_NATIVE_LIBRARY_DIR}\" SHORTENED_OPENSSL_LIBRARY_DIR\r\n")
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.0")
  file(APPEND ${OPENSSL_INSTALL_SCRIPT}
    "NMake /F \"${OPENSSL_1_0_MAKEFILE}\" install\r\n")
else()
  file(APPEND ${OPENSSL_INSTALL_SCRIPT}
    "NMake install\r\n")
endif()
file(APPEND ${OPENSSL_INSTALL_SCRIPT}
  "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "  ENDLOCAL\r\n"
  "  EXIT /B 1\r\n"
  ")\r\n"
  "IF EXIST ms\\applink,c (\r\n"
  "  COPY /Y ms\\applink.c \"!SHORTENED_OPENSSL_INCLUDE_DIR!\\openssl\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    ENDLOCAL\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "IF EXIST tmp32dll\\lib.pdb (\r\n"
  "  COPY /Y tmp32dll\\lib.pdb \"!SHORTENED_OPENSSL_BINARY_DIR!\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    ENDLOCAL\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "IF EXIST tmp32\\lib.pdb (\r\n"
  "  COPY /Y tmp32\\lib.pdb \"!SHORTENED_OPENSSL_LIBRARY_DIR!\"\r\n"
  "  IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "    ENDLOCAL\r\n"
  "    EXIT /B 1\r\n"
  "  )\r\n"
  ")\r\n"
  "COPY /Y LICENSE \"!SHORTENED_OPENSSL_INSTALL_DIR!\"\r\n"
  "ENDLOCAL\r\n"
  "EXIT /B\r\n"
  ":SHORTENPATH\r\n"
  "  FOR %%A IN (\"%~1\") DO SET %~2=%%~SA\r\n"
  "  EXIT /B\r\n")
set(OPENSSL_INSTALL_COMMAND "${OPENSSL_INSTALL_SCRIPT}")

# Add OpenSSL as an external project
externalproject_add(${OPENSSL_LIBRARY_NAME}
  PREFIX ${OPENSSL_PROJECT_PREFIX}
  URL ${OPENSSL_ARCHIVE_URL}
  DOWNLOAD_DIR ${OPENSSL_PROJECT_PREFIX}
  CONFIGURE_COMMAND "${OPENSSL_CONFIGURE_COMMAND}"
  BUILD_COMMAND "${OPENSSL_BUILD_COMMAND}"
  INSTALL_COMMAND "${OPENSSL_INSTALL_COMMAND}"
  BUILD_IN_SOURCE 1
  LOG_DOWNLOAD 1
  LOG_CONFIGURE 1
  LOG_BUILD 1
  LOG_INSTALL 1)

# Create the configure script
file(REMOVE ${OPENSSL_CONFIGURE_SCRIPT})
file(WRITE ${OPENSSL_CONFIGURE_SCRIPT}
  "@REM Generated configure script for OpenSSL\r\n"
  "@ECHO OFF\r\n"
  "SETLOCAL ENABLEDELAYEDEXPANSION\r\n"
  "PUSHD .\r\n")
if(DEFINED ENV{APPVEYOR} AND CMAKE_CL_64)
  file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
    "CALL \"C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1\\Bin\\SetEnv.cmd\" /x64\r\n"
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\" ${MSVC_ENVIRONMENT_ARCH}\r\n")
else()
  file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
    "CALL \"${MSVC_ENVIRONMENT_SCRIPT}\"\r\n")
endif()
file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
  "POPD\r\n"
  "SET PATH=${PERL_PATH};%PATH%\r\n"
  "CALL :SHORTENPATH \"${OPENSSL_NATIVE_INSTALL_DIR}\" SHORTENED_OPENSSL_INSTALL_DIR\r\n")
if(CASS_USE_ZLIB)
  # OpenSSL requires zlib paths to be relative (otherwise build errors may occur)
  externalproject_get_property(${OPENSSL_LIBRARY_NAME} SOURCE_DIR)
  file(RELATIVE_PATH ZLIB_INCLUDE_RELATIVE_DIR ${SOURCE_DIR} ${ZLIB_INCLUDE_DIR})
  file(TO_NATIVE_PATH ${ZLIB_INCLUDE_RELATIVE_DIR} ZLIB_NATIVE_INCLUDE_RELATIVE_DIR)
  file(RELATIVE_PATH ZLIB_LIBRARY_RELATIVE_DIR ${SOURCE_DIR} ${ZLIB_LIBRARY_DIR})
  file(TO_NATIVE_PATH ${ZLIB_LIBRARY_RELATIVE_DIR} ZLIB_NATIVE_LIBRARY_RELATIVE_DIR)
  set(OPENSSL_WITH_ZLIB_ARGUMENT "zlib ${OPENSSL_ZLIB_CONFIGURE_ARGUMENT} --with-zlib-include=\"${ZLIB_NATIVE_INCLUDE_RELATIVE_DIR}\" --with-zlib-lib=\"${ZLIB_NATIVE_LIBRARY_RELATIVE_DIR}\\${ZLIB_LIB}\"")
  set(OPENSSL_WITH_ZLIB_ARGUMENT "zlib ${OPENSSL_ZLIB_CONFIGURE_ARGUMENT} --with-zlib-include=\"${ZLIB_INCLUDE_RELATIVE_DIR}\" --with-zlib-lib=\"${ZLIB_LIBRARY_RELATIVE_DIR}\\${ZLIB_LIB}\"")
endif()
file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
  "perl Configure ${OPENSSL_WITH_ZLIB_ARGUMENT} --openssldir=!SHORTENED_OPENSSL_INSTALL_DIR! --prefix=!SHORTENED_OPENSSL_INSTALL_DIR! ${OPENSSL_CONFIGURE_COMPILER}\r\n"
  "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
  "  ENDLOCAL\r\n"
  "  EXIT /B 1\r\n"
  ")\r\n")
if(OPENSSL_MAJOR_MINOR_VERSION STREQUAL "1.0")
  file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
    "CALL ${OPENSSL_1_0_CONFIGURE_MAKEFILE_SCRIPT}\r\n"
    "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
    "  ENDLOCAL\r\n"
    "  EXIT /B 1\r\n"
    ")\r\n"
    "perl -p -i\".backup\" -e \"s/exe: \\$\\(T_EXE\\)/exe:/g\" ${OPENSSL_1_0_MAKEFILE}\r\n"
    "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
    "  ENDLOCAL\r\n"
    "  EXIT /B 1\r\n"
    ")\r\n")
endif()
if(NOT BUILD_SHARED_LIBS)
  file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
    "perl -p -i\".backup\" -e \"s/\\/MT/\\/MD/g\" ${OPENSSL_MAKEFILE}\r\n"
    "IF NOT %ERRORLEVEL% EQU 0 (\r\n"
    "  ENDLOCAL\r\n"
    "  EXIT /B 1\r\n"
    ")\r\n")
endif()
file(APPEND ${OPENSSL_CONFIGURE_SCRIPT}
  "ENDLOCAL\r\n"
  "EXIT /B\r\n"
  ":SHORTENPATH\r\n"
  "  FOR %%A IN (\"%~1\") DO SET %~2=%%~SA\r\n"
  "  EXIT /B\r\n")

# Determine if zlib should be added as a dependency
if(CASS_USE_ZLIB)
  add_dependencies(${OPENSSL_LIBRARY_NAME} ${ZLIB_LIBRARY_NAME})
endif()

# Update the include directory to use OpenSSL
include_directories(${OPENSSL_INCLUDE_DIR})
