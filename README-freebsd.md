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
- nanomsg

To use the CLI, you will need to install the
[nnpy](https://github.com/nanomsg/nnpy) Python package:

    sudo pip install nnpy



## Temporary workaround

edit `tools/get-version.sh` and change `#!/bin/bash` with
`#!/usr/local/bin/bash`

## Building the code

    1. ./autogen.sh
    2. ./configure --disable-logging-macros --disable-elogger 'CXXFLAGS=-I/usr/local/include -O3' 'CFLAGS=-I/usr/local/include -O3' 'LDFLAGS=-L/usr/local/lib' 'CC=clang' 'CXX=clang++'
    3. make

Debug logging is enabled by default. If you want to disable it for performance
reasons, you can pass `--disable-logging-macros` to the `configure` script.

In 'debug mode', you probably want to disable compiler optimization and enable
symbols in the binary:

    ./configure 'CXXFLAGS=-O0 -g'

The new bmv2 debugger can be enabled by passing `--enable-debugger` to
`configure`.

## Testing 

simple test with pcap files:

```
	(
	cd targets/fast_switch;
	./fast_switch --use-files 0  -i 1@first -i 2@second \
		simple_router.json --thrift-port 9999;
	)
```

speed test for comparisons between different options (see `./speed_tests.py
-h`):

```
	(
	cd speed-tests;
	sudo ./speed-test.py <options>;
	)
```
