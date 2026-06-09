#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kvstore.h"

#define KVSTORE_MAX_TOKENS 128

const char *comands[] = {
    "SET", "GET", "DEL", "MOD",
    "RSET", "RGET", "RDEL", "RMOD",
    "HSET", "HGET", "HDEL", "HMOD",
    "ZSET", "ZGET", "ZDEL", "ZMOD",
};

enum {
    KVS_CMD_START = 0,
    KVS_CMD_SET = KVS_CMD_START,
    KVS_CMD_GET,
    KVS_CMD_DEL,
    KVS_CMD_MOD,
    KVS_CMD_RSET,
    KVS_CMD_RGET,
    KVS_CMD_RDEL,
    KVS_CMD_RMOD,
    KVS_CMD_HSET,
    KVS_CMD_HGET,
    KVS_CMD_HDEL,
    KVS_CMD_HMOD,
    KVS_CMD_ZSET,
    KVS_CMD_ZGET,
    KVS_CMD_ZDEL,
    KVS_CMD_ZMOD,
    KVS_CMD_COUNT,
};

void *kvstore_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return NULL;
    }
    return ptr;
}

void kvstore_free(void *ptr) {
    if (ptr != NULL) {
        free(ptr);
    }
}

int kvstore_spilt_token(char *msg, char **tokens) {
    int idx = 0;
    char *token = NULL;

    if (msg == NULL || tokens == NULL) {
        return -1;
    }

    token = strtok(msg, " \r\n\t");
    while (token != NULL && idx < KVSTORE_MAX_TOKENS) {
        tokens[idx++] = token;
        token = strtok(NULL, " \r\n\t");
    }

    return idx;
}

static int kvstore_command_index(char *cmd_name) {
    int cmd = KVS_CMD_START;

    if (cmd_name == NULL) {
        return KVS_CMD_COUNT;
    }

    for (cmd = KVS_CMD_START; cmd < KVS_CMD_COUNT; cmd++) {
        if (strcmp(comands[cmd], cmd_name) == 0) {
            return cmd;
        }
    }

    return KVS_CMD_COUNT;
}

static void kvstore_write_set_response(char *msg, int res) {
    if (res < 0) {
        snprintf(msg, BUFFER_LENGTH, "ERROR");
    } else if (res == 0) {
        snprintf(msg, BUFFER_LENGTH, "SUCCESS");
    } else {
        snprintf(msg, BUFFER_LENGTH, "FAIL");
    }
}

static void kvstore_write_get_response(char *msg, char *value) {
    if (value != NULL) {
        snprintf(msg, BUFFER_LENGTH, "%s", value);
    } else {
        snprintf(msg, BUFFER_LENGTH, "NO EXIST");
    }
}

static void kvstore_write_delmod_response(char *msg, int res) {
    if (res < 0) {
        snprintf(msg, BUFFER_LENGTH, "ERROR");
    } else if (res == 0) {
        snprintf(msg, BUFFER_LENGTH, "SUCCESS");
    } else {
        snprintf(msg, BUFFER_LENGTH, "NO EXIST");
    }
}

int kvstore_parse_protocol(struct conn_item *item, char **tokens, int count) {
    int cmd = KVS_CMD_COUNT;
    char *msg = NULL;

    if (item == NULL || tokens == NULL || count <= 0) {
        return -1;
    }

    msg = item->wbuffer;
    memset(msg, 0, BUFFER_LENGTH);

    cmd = kvstore_command_index(tokens[0]);
    if (cmd == KVS_CMD_COUNT) {
        snprintf(msg, BUFFER_LENGTH, "UNKNOWN COMMAND");
        return -1;
    }

    switch (cmd) {
        case KVS_CMD_SET:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_set_response(msg, kvstore_array_set(tokens[1], tokens[2]));
            break;
        case KVS_CMD_GET:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_get_response(msg, kvstore_array_get(tokens[1]));
            break;
        case KVS_CMD_DEL:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_array_del(tokens[1]));
            break;
        case KVS_CMD_MOD:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_array_mod(tokens[1], tokens[2]));
            break;
#if ENABLE_RBTREE_KVENGINE
        case KVS_CMD_RSET:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_set_response(msg, kvstore_rbtree_set(tokens[1], tokens[2]));
            break;
        case KVS_CMD_RGET:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_get_response(msg, kvstore_rbtree_get(tokens[1]));
            break;
        case KVS_CMD_RDEL:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_rbtree_del(tokens[1]));
            break;
        case KVS_CMD_RMOD:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_rbtree_mod(tokens[1], tokens[2]));
            break;
#endif
#if ENABLE_HASH_KVENGINE
        case KVS_CMD_HSET:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_set_response(msg, kvstore_hash_set(tokens[1], tokens[2]));
            break;
        case KVS_CMD_HGET:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_get_response(msg, kvstore_hash_get(tokens[1]));
            break;
        case KVS_CMD_HDEL:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_hash_del(tokens[1]));
            break;
        case KVS_CMD_HMOD:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_hash_mod(tokens[1], tokens[2]));
            break;
#endif
#if ENABLE_SKIPTABLE_KVENGINE
        case KVS_CMD_ZSET:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_set_response(msg, kvstore_skiplist_set(tokens[1], tokens[2]));
            break;
        case KVS_CMD_ZGET:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_get_response(msg, kvstore_skiplist_get(tokens[1]));
            break;
        case KVS_CMD_ZDEL:
            if (count < 2) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_skiplist_del(tokens[1]));
            break;
        case KVS_CMD_ZMOD:
            if (count < 3) {
                snprintf(msg, BUFFER_LENGTH, "ERROR");
                return -1;
            }
            kvstore_write_delmod_response(msg, kvstore_skiplist_mod(tokens[1], tokens[2]));
            break;
#endif
        default:
            snprintf(msg, BUFFER_LENGTH, "UNKNOWN COMMAND");
            return -1;
    }

    return 0;
}

int kvstore_request(struct conn_item *item) {
    char *tokens[KVSTORE_MAX_TOKENS] = {0};
    int token_count = 0;

    if (item == NULL) {
        return -1;
    }

    printf("kvstore_request: %s\n", item->rbuffer);

    token_count = kvstore_spilt_token(item->rbuffer, tokens);
    if (token_count <= 0) {
        snprintf(item->wbuffer, BUFFER_LENGTH, "ERROR");
        return -1;
    }

    return kvstore_parse_protocol(item, tokens, token_count);
}

int kvstore_response(struct conn_item *item) {
    (void)item;
    return 0;
}

int main(void) {
#if ENABLE_RBTREE_KVENGINE
    if (kvstore_rbtree_create() != 0) {
        return -1;
    }
#endif
#if ENABLE_HASH_KVENGINE
    if (kvstore_hash_create(1024) != 0) {
        return -1;
    }
#endif
#if ENABLE_SKIPTABLE_KVENGINE
    if (kvstore_skiplist_create() != 0) {
        return -1;
    }
#endif

#if ENABLE_NETWORK_SELECT == NETWORK_EPOLL
    epoll_entry();
#elif ENABLE_NETWORK_SELECT == NETWORK_NTYCO
    ntyco_entry();
#elif ENABLE_NETWORK_SELECT == NETWORK_IO_URING
    io_uring_entry();
#endif

#if ENABLE_SKIPTABLE_KVENGINE
    kvstore_skiplist_destroy();
#endif
#if ENABLE_HASH_KVENGINE
    kvstore_hash_destroy();
#endif
#if ENABLE_RBTREE_KVENGINE
    kvstore_rbtree_destroy();
#endif

    return 0;
}
