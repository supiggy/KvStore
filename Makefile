CC = gcc
FLAGS =
SRCS = kvstore.c epoll_entry.c kvstore_array.c kvstore_rbtree.c kvstore_hash.c kvstore_skiplist.c
TARGET = kvstore

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(FLAGS)

%.o: %.c
	$(CC) $(FLAGS) -c $^ -o $@

clean:
	rm -rf $(OBJS) $(TARGET)
