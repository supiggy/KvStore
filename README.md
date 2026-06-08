# KvStore

KvStore is a C-based in-memory key-value store learning project. It combines a simple text protocol, a fixed-size array storage engine, and a TCP server entry based on NtyCo coroutines.

The current default configuration uses the NtyCo network backend and the array KV engine.

## Features

- In-memory key-value storage
- Text commands over TCP
- Basic `SET`, `GET`, `DEL`, and `MOD` operations
- Fixed-size array engine with duplicated key/value memory
- Pluggable network backend switches defined in `kvstore.h`
- NtyCo coroutine server listening on port `9096`

## Project Structure

```text
.
|-- kvstore.c                  # Protocol parsing and request dispatch
|-- kvstore.h                  # Shared structs, engine/network switches
|-- kvstore_array.c            # Array-based KV engine
|-- ntyco_entry.c              # NtyCo coroutine TCP server entry
|-- testcase.c                 # TCP client test scaffold
|-- Makefile                   # Build script
|-- tcp-connect-template-IO.c  # TCP I/O learning template
|-- tcp-connect-template-reactorc
|                              # Reactor learning template
`-- 2.1.1-multi-io-main/       # Multi-I/O learning examples
```

## Protocol

Commands are uppercase and space-separated.

```text
SET <key> <value>
GET <key>
MOD <key> <value>
DEL <key>
```

Example session:

```text
SET NAME King
SUCCESS

GET NAME
King

MOD NAME Queen
SUCCESS

DEL NAME
SUCCESS

GET NAME
NO EXIST
```

## Configuration

Main switches are defined in `kvstore.h`:

```c
#define ENABLE_ARRAY_KVENGINE 1
#define ENABLE_NETWORK_SELECT NETWORK_NTYCO
#define KVS_ARRAY_SIZE 1024
```

Available network constants:

```c
#define NETWORK_EPOLL 0
#define NETWORK_NTYCO 1
#define NETWORK_IO_URING 2
```

The current source tree mainly wires the NtyCo path through `ntyco_entry.c`.

## Build

The Makefile expects the NtyCo dependency to be available under `./NtyCo/` and to provide:

- headers under `./NtyCo/core/`
- library file linked by `-L ./NtyCo/ -lntyco`

Build command:

```sh
make
```

Clean command:

```sh
make clean
```

Note: the current Makefile references `epoll_entry.c`, but that file is not present in this repository snapshot. If you only want to build the NtyCo version, remove `epoll_entry.c` from `SRCS` or add the missing implementation.

## Run

After a successful build:

```sh
./kvstore
```

The NtyCo server listens on:

```text
0.0.0.0:9096
```

You can connect with `nc`:

```sh
nc 127.0.0.1 9096
```

Then send commands such as:

```text
SET NAME King
GET NAME
MOD NAME Queen
DEL NAME
```

## Test Client

`testcase.c` is a simple TCP client test scaffold intended to exercise the array engine commands:

```sh
./testcase -s 127.0.0.1 -p 9096 -m 1
```

It may need cleanup before being used as an automated test runner.

## Current Status

This repository is primarily a learning project for:

- TCP server programming
- epoll/reactor concepts
- coroutine-based network handling
- simple KV engine design

The array engine is functional at a basic level, but the project still needs build cleanup, safer protocol validation, and more complete automated tests before production use.
