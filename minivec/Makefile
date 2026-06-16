# MiniVec - 向量数据库 (学习项目)
# 注意: 网络层依赖 Linux (epoll)，请在 gcc:14 Docker 容器里 make，不要在原生 Windows 编译。

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -g -std=c11 -Isrc
LDFLAGS := -lm -lpthread

SRCS    := $(shell find src -name '*.c')
OBJS    := $(SRCS:.c=.o)
TARGET  := minivec

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) run_tests run_bench

# ---- G0:测试 & 基准 -------------------------------------------------------
# 只链接引擎源码(不含 main.c / net/server.c),所以不依赖 epoll,
# 可脱离网络层单独编译运行。一条命令:make test / make bench
ENGINE_SRCS := src/engine/distance.c src/engine/vector_store.c \
               src/engine/index_flat.c src/engine/index_hnsw.c \
               src/engine/topk_heap.c src/engine/persist.c
TEST_SRCS   := $(wildcard tests/*.c)
BENCH_SRCS  := $(wildcard bench/*.c)

.PHONY: test bench

test: $(ENGINE_SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) -Itests $(ENGINE_SRCS) $(TEST_SRCS) -o run_tests $(LDFLAGS)
	./run_tests

bench: $(ENGINE_SRCS) $(BENCH_SRCS)
	$(CC) $(CFLAGS) -Ibench $(ENGINE_SRCS) $(BENCH_SRCS) -o run_bench $(LDFLAGS)
	@echo "用法: ./run_bench [N] [Q] [topk] [ef]   (默认 5000 500 10 50)"
	./run_bench
