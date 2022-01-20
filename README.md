# Augyang

[![BSD license](https://img.shields.io/badge/License-BSD-blue.svg)](https://opensource.org/licenses/BSD-3-Clause)

Augyang is a framework that adds [YANG](http://tools.ietf.org/html/rfc7950) support for configuration files supported by [Augeas](https://augeas.net/). In other words, using this suite you can ultimately add support for remote management of configuration files described by Augeas lenses without writing any specific code.

This framework consists of the following components:

* augyang - tool for generating YANG files for Augeas lenses
* srds_augeas - sysrepo custom datastore plugin that handles transformation of Augeas data to YANG data and vice versa
* ay_startup - small utility using `srds_augeas` functionality to get current system configuration for a particular Augeas lens and printing it in YANG XML data

## Requirements

### Build Requirements

* C compiler (gcc >= 4.8.4, clang >= 3.0, ...)
* cmake >= 2.8.12
* [augeas](https://augeas.net/)
  * is statically linked to `augyang` so as part of the build process it is downloaded and compiled locally
* [libyang](https://github.com/CESNET/libyang)
* [sysrepo](https://github.com/sysrepo/sysrepo)

#### Optional

* cmocka >= 1.0.1 (for tests only, see [Tests](#Tests))
* [netopeer2](https://github.com/CESNET/netopeer2) (adding NETCONF support on top of `sysrepo`)

## Building And Installation

```
$ mkdir build; cd build
$ cmake ..
$ make
# make install
```

Note that by default, these steps will **fully install** the whole suite. That means generating YANG modules for all the supported Augeas lenses, installing these modules into `sysrepo` with their current content read from the system and put into the *startup* datastore, and finally installing `srds_augeas` custom DS plugin into `sysrepo`. All this can be customized by compile-time variables mentioned in the following section.

### Useful CMake Augyang Options

Set the list of supported Augeas lenses to generate/install:
```
-DSUPPORTED_LENSES="passwd simplevars"
```

Set whether to install custom DS plugin and YANG modules into `sysrepo`:
```
-DINSTALL_MODULES=OFF
```

### Useful CMake Build Options

#### Changing Compiler

Set `CC` variable:

```
$ CC=/usr/bin/clang cmake ..
```

#### Changing Install Path

To change the prefix where the library, headers and any other files are installed,
set `CMAKE_INSTALL_PREFIX` variable:
```
$ cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ..
```

Default prefix is `/usr/local`.

#### Build Modes

There are two build modes:
* Release.
  This generates library for the production use without any debug information.
* Debug.
  This generates library with the debug information and disables optimization
  of the code.

The `Debug` mode is currently used as the default one. to switch to the
`Release` mode, enter at the command line:
```
$ cmake -D CMAKE_BUILD_TYPE:String="Release" ..
```

## Tests

There are several tests included and built with [cmocka](https://cmocka.org/). The tests
can be found in `tests` subdirectory and they are designed for checking library
functionality after code changes.

The tests are by default built in the `Debug` build mode by running
```
$ make
```

In case of the `Release` mode, the tests are not built by default (it requires
additional dependency), but they can be enabled via cmake option:
```
$ cmake -DENABLE_TESTS=ON ..
```

Note that if the necessary [cmocka](https://cmocka.org/) headers are not present
in the system include paths, tests are not available despite the build mode or
cmake's options.

Tests can be run by the make's `test` target:
```
$ make test
```
