# BEHAVIORAL MODEL REPOSITORY - Freebsd port

## Dependencies

On Freebsd 10, the following packages are required:

- automake
- judy
- gmp
- libpcap
- boost-all
- libevent2
- libtool
- flex
- bison
- thrift
- thrift-cpp
- py27-thrift
- nanomsg``

To use the CLI, you will need to install the
[nnpy](https://github.com/nanomsg/nnpy) Python package:

    pip install nnpy


## Building the code

    1. ./autogen.sh
    2. ./configure 'CXXFLAGS=-I/usr/local/include' 'CFLAGS=-I/usr/local/include' 'LDFLAGS=-L/usr/local/lib /home/yuri/cxa_thread_atexit/libcxa_thread_atexit.a'
    3. make

Debug logging is enabled by default. If you want to disable it for performance
reasons, you can pass `--disable-logging-macros` to the `configure` script.

In 'debug mode', you probably want to disable compiler optimization and enable
symbols in the binary:

    ./configure 'CXXFLAGS=-O0 -g'

The new bmv2 debugger can be enabled by passing `--enable-debugger` to
`configure`.

