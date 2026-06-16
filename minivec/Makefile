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
	rm -f $(OBJS) $(TARGET) run_tests run_bench run_sweep run_loadgen

# ---- G0:测试 & 基准 -------------------------------------------------------
# 只链接引擎源码(不含 main.c / net/server.c),所以不依赖 epoll,
# 可脱离网络层单独编译运行。一条命令:make test / make bench
ENGINE_SRCS := src/engine/distance.c src/engine/vector_store.c \
               src/engine/index_flat.c src/engine/index_hnsw.c \
               src/engine/topk_heap.c src/engine/persist.c src/engine/quant.c
TEST_SRCS   := $(wildcard tests/*.c)
BENCH_UTIL  := bench/gen.c bench/metrics.c   # 共享:造数 + 指标(留白在 metrics.c)

.PHONY: test bench sweep loadgen

test: $(ENGINE_SRCS) $(TEST_SRCS)
	$(CC) $(CFLAGS) -Itests $(ENGINE_SRCS) $(TEST_SRCS) -o run_tests $(LDFLAGS)
	./run_tests

# G0:单点基准(flat vs hnsw 一组数字)
bench: $(ENGINE_SRCS) $(BENCH_UTIL) bench/bench_engine.c
	$(CC) $(CFLAGS) -Ibench $(ENGINE_SRCS) $(BENCH_UTIL) bench/bench_engine.c -o run_bench $(LDFLAGS)
	@echo "用法: ./run_bench [N] [Q] [topk] [ef]   (默认 5000 500 10 50)"
	./run_bench

# G6:参数扫描基准(扫 M/ef 出 recall-QPS 曲线,产 CSV)
sweep: $(ENGINE_SRCS) $(BENCH_UTIL) bench/sweep.c
	$(CC) $(CFLAGS) -Ibench $(ENGINE_SRCS) $(BENCH_UTIL) bench/sweep.c -o run_sweep $(LDFLAGS)
	@echo "用法: ./run_sweep [N] [Q] [topk] [out.csv]   然后 python3 bench/plot_recall.py"
	./run_sweep

# 并发压测客户端(连真实服务端 → 端到端 QPS/p99)。需先在另一终端跑 ./minivec
loadgen: bench/loadgen.c bench/metrics.c
	$(CC) $(CFLAGS) -Ibench bench/loadgen.c bench/metrics.c -o run_loadgen $(LDFLAGS)
	@echo "已构建 run_loadgen。先 ./minivec,再: ./run_loadgen [host] [port] [threads] [reqs] [populate] [topk]"
