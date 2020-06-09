# Warpcore

[![Build Status](https://travis-ci.com/NTAP/warpcore.svg?branch=master)](https://travis-ci.com/NTAP/warpcore)
[![Total alerts](https://img.shields.io/lgtm/alerts/g/NTAP/warpcore.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/NTAP/warpcore/alerts/)
[![Coverity Badge](https://scan.coverity.com/projects/13162/badge.svg)](https://scan.coverity.com/projects/ntap-warpcore)
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/NTAP/warpcore.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/NTAP/warpcore/context:cpp)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/58fdbf0fb0ef49608cbbd296e3e75698)](https://www.codacy.com/manual/larseggert/warpcore?utm_source=github.com&amp;utm_medium=referral&amp;utm_content=NTAP/warpcore&amp;utm_campaign=Badge_Grade)

## About

Warpcore is a minimal userspace UDP/IP/Ethernet stack for the [netmap packet
I/O framework](http://info.iet.unipi.it/~luigi/netmap/). Due to its dependency
on netmap, warpcore supports Linux (with a netmap-patched kernel) and FreeBSD
(which has netmap support since release 11).

Warpcore also has a backend implementation using the Socket API that should
compile on generic POSIX platforms, such as Linux, Darwin and others. The POSIX
backend has experimental support for the
[Particle](https://github.com/particle-iot/device-os) IoT stack, allowing
applications built on warpcore (such as [quant](https://github.com/NTAP/quant))
to support embedded devices. Warpcore also has an experimental support for
[RIOT](http://riot-os.org/), another IoT stack.

Warpcore prioritizes performance over features, and over full standards
compliance. It supports zero-copy transmit and receive with netmap, and uses
neither threads, timers nor signals. It exposes the underlying file descriptors
to an application, for easy integration with different event loops (e.g.,
[libev](http://software.schmorp.de/pkg/libev.html)).

The warpcore repository is [on GitHub](https://github.com/NTAP/warpcore).

## Building

Warpcore uses [cmake](https://cmake.org/) as a build system. To do an
out-of-source build of warpcore (best practice with `cmake`), do the following
to build with `make` as a generator:

    git submodule update --init --recursive
    mkdir Debug
    cd Debug
    cmake ..
    make

(cmake supports other generators, such as [ninja](https://ninja-build.org/)
(which I highly recommend over `make`). See the [cmake
documentation](https://cmake.org/cmake/help/latest/manual/cmake-generators.7.html).)

On platforms where netmap is available, the steps above will build a debug
version of `libsockcore.a` against netmap as a backend, and place it into the
`Debug/lib`. Examples (`sockping` and `sockinetd`) will be built in `Debug/bin`.

On platforms where netmap is available, the steps above will also build a debug
version of `libwarpcore.a` against netmap as a backend, and place it into the
`Debug/lib`. Examples (`warpping` and `warpinetd`) will also be built in
`Debug/bin`.

The example server application implements the
[`echo`](https://www.ietf.org/rfc/rfc862.txt),
[`discard`](https://www.ietf.org/rfc/rfc863.txt),
[`time`](https://www.ietf.org/rfc/rfc868.txt) and
[`daytime`](https://www.ietf.org/rfc/rfc867.txt) services.

The default build (per above) is without optimizations and with extensive debug
logging enabled. In order to build an optimized build, do this:

    git submodule update --init --recursive
    mkdir Release
    cd Release
    cmake -DCMAKE_BUILD_TYPE=Release ..
    make

## Documentation

Warpcore comes with documentation. This documentation can be built (if `doxygen`
is installed) by doing

    make doc

in any build directory. The starting page of the documentation is then
`doc/html/index.html`.

## Copyright

Copyright (c) 2014-2020, NetApp, Inc.
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

## Acknowledgment

This software has received funding from the European Union's Horizon 2020
research and innovation program 2014-2018 under grant agreement 644866
(["SSICLOPS"](https://ssiclops.eu/)). The European Commission is not responsible
for any use that may be made of this software.

[/a/]: # (@example ping.c)
[/b/]: # (@example inetd.c)
