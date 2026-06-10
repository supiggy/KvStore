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
	rm -f $(OBJS) $(TARGET)
