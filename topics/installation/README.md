# Installation

## Packages

Pre-built packages are available for Ubuntu 20.04/22.04, Rocky Linux 8 and 9 and Windows.
All packages are available from our [Artifactory server].

### Rocky Linux

Rocky Linux also doesn't have up-to-date versions of libuv so packages are available
for this platform as well.

To install dependencies:

```bash
yum install openssl krb5 zlib
rpm -Uvh libuv-<version>.rpm
```

Then install the runtime library (and optionally the development package and debug
symbols) as described above.

### Ubuntu

Newer versions of Ubuntu include workable versions of all dependencies; you do not
need to download anything from Artifactory.

To install dependencies:

```bash
apt-get install libssl libkrb5 zlib1g libuv1
```

Install the runtime library:

```bash
dpkg -i cassandra-cpp-driver_<version>.deb
```

When developing against the driver you'll also want to install the development
package and the debug symbols.

```bash
dpkg -i cassandra-cpp-driver-dev_<version>.deb
dpkg -i cassandra-cpp-driver-dbg_<version>.deb
```

### Windows

We provide packages (`.zip` files) for all the dependencies (except for
Kerberos) on Windows because they can be difficult to install/build.

Unzip the packages obtained from Artifactory and add the include and
library directories to your project's `Additional Include Directories` and
`Additional Dependencies` configuration properties.

You will also need to download and install [Kerberos] for Windows.

## Building

If pre-built packages are not available for your platform or architecture you
will need to build the driver from source. Directions for building and
installing the DataStax C/C++ Driver for Apache Cassandra and DataStax Products
can be found [here](/topics/building/).

[Artifactory server]: https://datastax.jfrog.io/artifactory/cpp-php-drivers/cpp-driver/builds
[Kerberos]: https://web.mit.edu/kerberos
