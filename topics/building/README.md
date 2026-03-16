# Building

The DataStax C/C++ Driver for Apache Cassandra and DataStax Products will build
on most standard Unix-like and Microsoft Windows platforms. Packages are
available for the following platforms:

* Rocky Linux 8.8
* Rocky Linux 9.2
* Ubuntu 20.04
* Ubuntu 22.04
* Windows

__NOTE__: The build procedures only need to be performed for driver development
          or if your system doesn't have packages available for download and
          installation.

## Compatibility

* Architectures: 32-bit (x86) and 64-bit (x64)
* Compilers: GCC 4.8.5+ Clang 3.4+, and MSVC 2013/2015/2017/2019

## Dependencies

The C/C++ driver depends on the following software:

* [CMake] v3.10+
* [libuv] 1.x
* Kerberos v5 ([Heimdal] or [MIT]) \*
* [OpenSSL] v1.0.x, v1.1.x or v3.x \*\*
* [zlib] v1.x \*\*\*

__\*__ Use the `CASS_USE_KERBEROS` CMake option to enable/disable Kerberos
       support. Enabling this option will enable Kerberos authentication
       protocol within the driver; defaults to `Off`.

__\*\*__ Use the `CASS_USE_OPENSSL` CMake option to enable/disable OpenSSL
         support. Disabling this option will disable SSL/TLS protocol support
         within the driver; defaults to `On`.

__\*\*\*__ Use the `CASS_USE_ZLIB` CMake option to enable/disable zlib support.
           Disabling this option will disable DataStax Astra support
           within the driver; defaults to `On`.

### A Brief Note on OpenSSL 3.x

Migrating from OpenSSL 1.1.x to 3.x largely involves avoiding the use of many functions which are now deprecated (consult
the [migration guide] for details).  The driver does not use any of these functions so we expect the transition to OpenSSL
3.x to be relatively painless.  Note that two officially supported platforms (Ubuntu 22.04 and Rocky Linux 9.2) come with
OpenSSL 3.x by default and the unit and integration tests all pass on these platforms.

## Linux/Mac OS

The driver is known to build on Rocky Linux 8/9, Mac OS X 10.10/10.11 (Yosemite
and El Capitan), Mac OS 10.12/10.13 (Sierra and High Sierra), and Ubuntu
20.04/22.04 LTS.

__NOTE__: The driver will also build on most standard Unix-like systems using
          GCC 4.1.2+ or Clang 3.4+.

### Installing dependencies

#### Initial environment setup

##### Rocky/RHEL (Yum)

```bash
yum install automake cmake gcc-c++ git libtool
```

##### Ubuntu (APT)

```bash
apt-get update
apt-get install build-essential cmake git
```

##### Mac OS (Brew)

[Homebrew][Homebrew] (or brew) is a free and open-source software package
management system that simplifies the installation of software on the Mac OS
operating system. Ensure [Homebrew is installed][Homebrew] before proceeding.

```bash
brew update
brew upgrade
brew install autoconf automake cmake libtool
```

#### Kerberos

##### Rocky/RHEL (Yum)

```bash
yum install krb5-devel
```

##### Ubuntu (APT)

```bash
apt-get install libkrb5-dev
```

#### libuv

libuv v1.x should be used in order to ensure all features of the C/C++ driver
are available. When using a package manager for your operating system make sure
you install v1.x.

##### Rocky and Ubuntu packages

Packages are available from our [Artifactory server].  Select the driver version,
build and platform and then look for the `dependencies` directory.  Note that the
version of libuv available on supported versions of Ubuntu can be used when
building the driver.  As a result we only provide packages for Rocky Linux only.

##### Mac OS (Brew)

```bash
brew install libuv
```

##### Manually build and install

_The following procedures should be performed if packages are not available for
your system._

```bash
pushd /tmp
wget http://dist.libuv.org/dist/v1.34.0/libuv-v1.34.0.tar.gz
tar xzf libuv-v1.34.0.tar.gz
pushd libuv-v1.34.0
sh autogen.sh
./configure
make install
popd
popd
```

#### OpenSSL

##### Rocky (Yum)

```bash
yum install openssl-devel
```

##### Ubuntu (APT)

```bash
apt-get install libssl-dev
```

##### Mac OS (Brew)

```bash
brew install openssl
```

__Note__: For Mac OS X 10.11 (El Capitan) and Mac OS 10.12/10.13 (Sierra and
          High Sierra) a link needs to be created in order to make OpenSSL
          available to the building libraries:

```bash
brew link --force openssl
```

##### Manually build and install

```bash
pushd /tmp
wget --no-check-certificate https://www.openssl.org/source/openssl-1.0.2u.tar.gz
tar xzf openssl-1.0.2u.tar.gz
pushd openssl-1.0.2u
CFLAGS=-fpic ./config shared
make install
popd
popd
```

#### zlib

##### Rocky (Yum)

```bash
yum install zlib-devel
```

##### Ubuntu (APT)

```bash
apt-get install zlib1g-dev
```

##### Mac OS (Brew)

```bash
brew install zlib
```

##### Manually build and install

```bash
pushd /tmp
wget --no-check-certificate https://www.zlib.net/zlib-1.2.11.tar.gz
tar xzf zlib-1.2.11.tar.gz
pushd zlib-1.2.11
./configure
make install
popd
popd
```

### Building and installing the C/C++ driver

```bash
mkdir build
pushd build
cmake ..
make
make install
popd
```

#### Building examples (optional)

Examples are not built by default and need to be enabled. Update your [CMake]
line to build examples.

```bash
cmake -DCASS_BUILD_EXAMPLES=On ..
```

#### Building tests (optional)

Tests (integration and unit) are not built by default and need to be enabled.

##### All tests

```bash
cmake -DCASS_BUILD_TESTS=On ..
```

__Note__: This will build both the integration and unit tests

##### Integration tests

```bash
cmake -DCASS_BUILD_INTEGRATION_TESTS=On ..
```

##### Unit tests

```bash
cmake -DCASS_BUILD_UNIT_TESTS=On ..
```

## Windows

The driver is known to build with Visual Studio 2013, 2015, 2017, and 2019.

### Obtaining build dependencies

* Download and install [Bison]
  * Make sure Bison is in your system PATH and not installed in a directory with
    spaces (e.g. `%SYSTEMDRIVE%\GnuWin32`)
* Download and install [CMake]
  * Make sure to select the option "Add CMake to the system PATH for all users"
    or "Add CMake to the system PATH for current user"
* Download and install [Strawberry Perl] or [ActiveState Perl]
  * Make sure to select the option "Add Perl to PATH environment variable"
* Download and install Kerberos for Windows v4.0.1
  * [32-bit][k4w-32]
  * [64-bit][k4w-64]

### Building the driver

First you will need to open a "Command Prompt" to execute the CMake commands.

#### Building the C/C++ driver

Supported generators are:
* Visual Studio 12 2013
* Visual Studio 14 2015
* Visual Studio 15 2017
* Visual Studio 16 2019

```bash
mkdir build
pushd build
cmake -G "Visual Studio 16 2019" -A x64 ..
cmake --build .
popd
```

__Note__: To build 32-bit binaries/libraries use `-A Win32`.

#### Building examples (optional)

Examples are not built by default and need to be enabled. Update your [CMake]
line to build examples.

```bash
cmake -G "Visual Studio 16 2019" -A x64 -DCASS_BUILD_EXAMPLES=On ..
```

#### Building tests (optional)

Tests (integration and unit) are not built by default and need to be enabled.

##### All tests

```bash
cmake -G "Visual Studio 16 2019" -A x64 -DCASS_BUILD_TESTS=On ..
```

__Note__: This will build both the integration and unit tests


##### Integration tests

```bash
cmake -G "Visual Studio 16 2019" -A x64 -DCASS_BUILD_INTEGRATION_TESTS=On ..
```

##### Unit tests

```bash
cmake -G "Visual Studio 16 2019" -A x64 -DCASS_BUILD_UNIT_TESTS=On ..
```

[Artifactory server]: https://datastax.jfrog.io/artifactory/cpp-php-drivers/cpp-driver/builds
[Homebrew]: https://brew.sh
[Bison]: http://gnuwin32.sourceforge.net/downlinks/bison.php
[CMake]: http://www.cmake.org/download
[Strawberry Perl]: http://strawberryperl.com
[ActiveState Perl]: https://www.perl.org/get.html#win32
[k4w-32]: http://web.mit.edu/kerberos/dist/kfw/4.0/kfw-4.0.1-i386.msi
[k4w-64]: http://web.mit.edu/kerberos/dist/kfw/4.0/kfw-4.0.1-amd64.msi
[libuv]: http://libuv.org
[Heimdal]: https://www.h5l.org
[MIT]: https://web.mit.edu/kerberos
[OpenSSL]: https://www.openssl.org
[zlib]: https://www.zlib.net
[migration guide]: https://www.openssl.org/docs/man3.0/man7/migration_guide.html
