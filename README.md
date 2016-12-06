# Warpcore

## About

Warpcore is a minimal userspace UDP/IPv4/Ethernet stack for the [netmap packet
I/O framework](http://info.iet.unipi.it/~luigi/netmap/). Due to its dependency
on netmap, warpcore supports Linux (with a netmap-patched kernel) and FreeBSD
(which has netmap support since release 11). However, warpcore has a shim
implementation using the Socket API that should compile on generic POSIX
platforms, such as Darwin.

Warpcore prioritizes performance over features, and likely over full standards
compliance. It supports zero-copy transmit and receive, and uses neither
threads, timers nor signals. It exposes the underlying file descriptors to an
application, for easy integration with different event loops (e.g.,
[libev](http://software.schmorp.de/pkg/libev.html)).


## Building

Warpcore uses [cmake](https://cmake.org/) as a build system. To do an
out-of-source build of warpcore (best practice with `cmake`), do the following
to build with `make` as a generator:

```
mkdir Debug
cd Debug
cmake ..
make
```

(cmake supports other generators, such as [ninja](https://ninja-build.org/). See
the [cmake
documentation](https://cmake.org/cmake/help/v3.7/manual/cmake-generators.7.html).)

On platforms where netmap is available, the steps above will build a debug
version of `libwarpcore.a` against netmap as a backend, and place it into the
`Debug/lib`. When netmap is *not* available, the steps above should build
warpcore against the Socket API. This allows more convenient development of
applications that link against warpcore when netmap is not available.

Additionally, the steps above build `bin/warpping`, an example client
application, as well as `bin/warpinetd`, which is an example server application
implementing the [`echo`](https://www.ietf.org/rfc/rfc862.txt),
[`discard`](https://www.ietf.org/rfc/rfc863.txt),
[`time`](https://www.ietf.org/rfc/rfc868.txt) and
[`daytime`](https://www.ietf.org/rfc/rfc867.txt) services.

The default build (per above) is without optimizations and with extensive debug
logging enabled. In order to build an optimized build, do this:

```
mkdir Release
cd Release
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

If you are on a platform where netmap is not available, you can use the included
`Vagrantfile` to bring up either a [`vagrant`](https://www.vagrantup.com/)
FreeBSD or Linux VM, which should be automatically provisioned with netmap.


## Documentation

Warpcore will hopefully eventually come with an extensive documentation. This
documentation can be build by doing

```
make doc
```

in any build directory. The starting page of the documentation is then
`doc/html/index.html`.


## Copyright

Copyright (c) 2014-2016, NetApp, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


[//]: # (@example warpping.c)
[//]: # (@example warpinetd.c)

