#ifndef _KVSTORE_H
#define _KVSTORE_H

#include <stddef.h>

#define BUFFER_LENGTH		512

typedef int (*RCALLBACK)(int fd);

// conn, fd, buffer, callback
struct conn_item {
	int fd;
	
	char rbuffer[BUFFER_LENGTH];
	int rlen;
	char wbuffer[BUFFER_LENGTH];
	int wlen;

	union {
		RCALLBACK accept_callback;
		RCALLBACK recv_callback;
	} recv_t;
	RCALLBACK send_callback;
};

int epoll_entry(void);
int ntyco_entry(void);


int kvstore_request(struct conn_item *item);

//这是一个简单的内存分配函数, 用于在kvstore中分配内存
//方便后续封装其他的内存分配器, 例如slab allocator等
void *kvstore_malloc(size_t size);
void kvstore_free(void *ptr);

#define NETWORK_EPOLL 0
#define NETWORK_NTYCO 1
#define NETWORK_IO_URING 2


#define ENABLE_ARRAY_KVENGINE 1
#define ENABLE_RBTREE_KVENGINE 1
#define ENABLE_HASH_KVENGINE 1
#define ENABLE_SKIPTABLE_KVENGINE 1
#define ENABLE_NETWORK_SELECT NETWORK_EPOLL


//这是 数组实现的 KVStore，提供了基本的增删改查
//它使用一个固定大小的数组来存储键值对，每个键值对由一个结构体 kvs_array_item 表示
//包含 key 和 value 两个字符串指针。数组的大小由 KVS_ARRAY_SIZE 定义，这里设置为 1024。
#if ENABLE_ARRAY_KVENGINE
struct kvs_array_item {
    char *key;
    char *value;
}; 

char *kvstore_array_get(char *key);
int kvstore_array_set(char *key, char *value);
int kvstore_array_del(char *key);
int kvstore_array_mod(char *key, char *value);

#define KVS_ARRAY_SIZE 1024
#endif

#if ENABLE_RBTREE_KVENGINE
int   kvstore_rbtree_create(void);
void  kvstore_rbtree_destroy(void);
int   kvstore_rbtree_set(char *key, char *value);
char *kvstore_rbtree_get(char *key);
int   kvstore_rbtree_del(char *key);
int   kvstore_rbtree_mod(char *key, char *value);
#endif

#if ENABLE_HASH_KVENGINE
int   kvstore_hash_create(int size);
void  kvstore_hash_destroy(void);
int   kvstore_hash_set(char *key, char *value);
char *kvstore_hash_get(char *key);
int   kvstore_hash_del(char *key);
int   kvstore_hash_mod(char *key, char *value);
#endif

#if ENABLE_SKIPTABLE_KVENGINE
int   kvstore_skiplist_create(void);
void  kvstore_skiplist_destroy(void);
int   kvstore_skiplist_set(char *key, char *value);
char *kvstore_skiplist_get(char *key);
int   kvstore_skiplist_del(char *key);
int   kvstore_skiplist_mod(char *key, char *value);
#endif


#endif
