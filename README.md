# KvStore

KvStore is a C learning project for an in-memory key-value server. The root
program combines a text TCP protocol, an epoll event loop, and several storage
engines implemented from scratch.

The current checked-in build path uses the epoll backend and listens on ports
starting at `2048`.

## What Is Here

```text
.
|-- kvstore.c                    # main(), protocol parsing, command dispatch
|-- kvstore.h                    # shared structs, engine switches, network switch
|-- epoll_entry.c                # active TCP server backend, ports 2048-2067
|-- ntyco_entry.c                # optional NtyCo coroutine backend
|-- kvstore_array.c              # fixed-size array KV engine
|-- kvstore_rbtree.c             # red-black tree KV engine
|-- kvstore_hash.c               # hash-table KV engine
|-- kvstore_skiplist.c           # skiplist KV engine
|-- testcase.c                   # TCP client test scaffold
|-- Makefile                     # root KVStore build
|-- 2.1.1-multi-io-main/         # blocking/thread/select/poll/epoll examples
|-- minivec/                     # separate vector database learning project
|-- sim/robotdog_slam_kv/        # Robotdog SLAM -> KVStore simulation
`-- 八股/                        # interview and project notes
```

`kvstore-template.c`, `tcp-connect-template-IO.c`, and
`tcp-connect-template-reactorc` are teaching/template files, not the default
build path.

## Root KVStore

### Runtime Flow

```text
client socket
  -> epoll_entry.c:recv_cb()
  -> kvstore.c:kvstore_request()
  -> kvstore.c:kvstore_parse_protocol()
  -> selected KV engine
  -> epoll_entry.c:send_cb()
```

Each connection uses `struct conn_item` from `kvstore.h`, which stores the file
descriptor, read buffer, write buffer, and callbacks.

### Commands

Commands are uppercase and space-separated.

```text
SET <key> <value>      GET <key>      DEL <key>      MOD <key> <value>
RSET <key> <value>     RGET <key>     RDEL <key>     RMOD <key> <value>
HSET <key> <value>     HGET <key>     HDEL <key>     HMOD <key> <value>
ZSET <key> <value>     ZGET <key>     ZDEL <key>     ZMOD <key> <value>
```

Command groups map to engines:

```text
SET/GET/DEL/MOD       -> array
RSET/RGET/RDEL/RMOD   -> red-black tree
HSET/HGET/HDEL/HMOD   -> hash table
ZSET/ZGET/ZDEL/ZMOD   -> skiplist
```

Responses are plain text:

```text
SUCCESS
FAIL
NO EXIST
ERROR
UNKNOWN COMMAND
```

### Configuration

Main switches are in `kvstore.h`:

```c
#define ENABLE_ARRAY_KVENGINE 1
#define ENABLE_RBTREE_KVENGINE 1
#define ENABLE_HASH_KVENGINE 1
#define ENABLE_SKIPTABLE_KVENGINE 1
#define ENABLE_NETWORK_SELECT NETWORK_EPOLL
```

Available network constants:

```c
#define NETWORK_EPOLL 0
#define NETWORK_NTYCO 1
#define NETWORK_IO_URING 2
```

The current `Makefile` builds the epoll path. The NtyCo path is present as a
learning backend, but it is not the default build.

### Build And Run

Build in Linux or the dev container. The active server uses Linux networking
APIs such as epoll.

```sh
make
./kvstore
```

Connect to the first default port:

```sh
nc 127.0.0.1 2048
```

Example session:

```text
SET NAME King
GET NAME
MOD NAME Queen
DEL NAME
GET NAME
```

Clean build outputs:

```sh
make clean
```

## MiniVec

`minivec/` is a separate C vector database learning project. It has its own
Makefile, source tree, tests, benchmarks, and docs.

It supports commands such as:

```text
VADD
VSEARCH
VDEL
VCOUNT
SAVE
LOAD
```

Use its own build from inside `minivec/`:

```sh
cd minivec
make
make test
make bench
```

## Simulation

`sim/robotdog_slam_kv/` is an application-level simulation that writes robot
state, map metadata, pose graph summaries, keyframes, and semantic tiles into
KVStore. It does not change the KVStore server; it acts as a client/demo.

Offline mode can be used without starting the server:

```sh
python3 sim/robotdog_slam_kv/robotdog_slam_kv_sim.py --offline --trace-out sim_trace.jsonl
```

Against the epoll server:

```sh
./kvstore
python3 sim/robotdog_slam_kv/robotdog_slam_kv_sim.py --host 127.0.0.1 --port 2048
```

## Current Status

This is primarily a learning repository for:

- TCP server programming
- epoll/reactor design
- coroutine backend comparison
- basic KV engine implementation
- vector search internals through `minivec/`

The root KVStore is usable for simple request-response testing. The protocol
still assumes a simple teaching model where one `recv` corresponds to one
complete command; a production version should add line framing or a
length-prefixed protocol and stronger validation.
