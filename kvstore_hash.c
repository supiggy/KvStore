#include <string.h>

#include "kvstore.h"

#if ENABLE_HASH_KVENGINE

/* ============================================================================
 *  哈希表引擎（链地址法 / 拉链法）
 * ----------------------------------------------------------------------------
 *  思路：用一个"桶数组"(nodes) 存指针，key 经哈希函数算出下标定位到某个桶；
 *  不同 key 哈希到同一个桶（冲突）时，挂成一条单链表。
 *
 *      nodes[0] -> NULL
 *      nodes[1] -> {k1,v1} -> {k7,v7} -> NULL   // 1 和 7 冲突，串成链
 *      nodes[2] -> {k2,v2} -> NULL
 *
 *  复杂度：哈希均匀时增删查平均 O(1)；极端冲突退化成单链表 O(n)。
 *  装载因子 = count / max_slots，过大查询变慢，工程上需 rehash 扩容；
 *  本实现桶数固定（create 时给定），不做扩容——属于教学简化。
 *
 *  语义约定（与 array/rbtree 引擎一致）：
 *    set: 0 成功 / 1 key 已存在(不覆盖) / -1 出错 —— SET 只负责"新增"，改值用 MOD
 *    get: 返回 value 指针 / NULL 表示不存在
 *    del/mod: 0 成功 / 1 不存在 / -1 出错
 * ========================================================================== */
struct hashnode {
    char *key;
    char *value;
    struct hashnode *next;   /* 同桶冲突链的下一个节点 */
};

struct hashtable {
    struct hashnode **nodes; /* 桶数组：max_slots 个 hashnode* 指针 */
    int max_slots;           /* 桶个数 */
    int count;               /* 已存键值对总数（可用于算装载因子） */
};

static struct hashtable hash_table = {0};

static char *kvstore_strdup(const char *src) {
    char *dst = NULL;

    if (src == NULL) {
        return NULL;
    }

    dst = kvstore_malloc(strlen(src) + 1);
    if (dst == NULL) {
        return NULL;
    }

    strcpy(dst, src);
    return dst;
}

/* BKDR 哈希：hash = hash*31(此处用131) + 当前字符，逐字符累乘累加。
 * 131 是经验上冲突较少的乘子(31/131/1313 都常见)。强转 unsigned char 避免
 * 负的 char 参与运算；返回 unsigned 让溢出按模 2^32 自然回绕。
 * 调用方再 % max_slots 把哈希值映射到合法桶下标。 */
static unsigned int kvstore_hash_func(const char *key) {
    unsigned int hash = 0;

    while (*key != '\0') {
        hash = hash * 131 + (unsigned char)*key++;
    }

    return hash;
}

static struct hashnode *kvstore_hash_find(char *key, unsigned int *slot) {
    struct hashnode *node = NULL;
    unsigned int idx = 0;

    if (key == NULL || hash_table.nodes == NULL || hash_table.max_slots <= 0) {
        return NULL;
    }

    idx = kvstore_hash_func(key) % hash_table.max_slots;
    if (slot != NULL) {
        *slot = idx;
    }

    node = hash_table.nodes[idx];
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

int kvstore_hash_create(int size) {
    int i = 0;

    if (size <= 0) {
        return -1;
    }

    if (hash_table.nodes != NULL) {
        return 0;
    }

    hash_table.nodes = kvstore_malloc(sizeof(struct hashnode *) * size);
    if (hash_table.nodes == NULL) {
        return -1;
    }

    for (i = 0; i < size; i++) {
        hash_table.nodes[i] = NULL;
    }

    hash_table.max_slots = size;
    hash_table.count = 0;
    return 0;
}

void kvstore_hash_destroy(void) {
    int i = 0;

    if (hash_table.nodes == NULL) {
        return;
    }

    for (i = 0; i < hash_table.max_slots; i++) {
        struct hashnode *node = hash_table.nodes[i];
        while (node != NULL) {
            struct hashnode *next = node->next;
            kvstore_free(node->key);
            kvstore_free(node->value);
            kvstore_free(node);
            node = next;
        }
    }

    kvstore_free(hash_table.nodes);
    hash_table.nodes = NULL;
    hash_table.max_slots = 0;
    hash_table.count = 0;
}

int kvstore_hash_set(char *key, char *value) {
    struct hashnode *node = NULL;
    char *key_copy = NULL;
    char *value_copy = NULL;
    unsigned int slot = 0;

    if (key == NULL || value == NULL || hash_table.nodes == NULL) {
        return -1;
    }

    /* 先查重：已存在则返回 1（SET 不覆盖）。顺带通过 &slot 拿到桶下标，省一次哈希。 */
    if (kvstore_hash_find(key, &slot) != NULL) {
        return 1;
    }

    /* 逐步分配 node / key / value，任意一步失败都要回滚已分配的内存，避免泄漏。 */
    node = kvstore_malloc(sizeof(struct hashnode));
    if (node == NULL) {
        return -1;
    }

    key_copy = kvstore_strdup(key);
    if (key_copy == NULL) {
        kvstore_free(node);
        return -1;
    }

    value_copy = kvstore_strdup(value);
    if (value_copy == NULL) {
        kvstore_free(key_copy);
        kvstore_free(node);
        return -1;
    }

    /* 头插法：新节点接到桶链表头部，O(1)，无需遍历到尾。 */
    node->key = key_copy;
    node->value = value_copy;
    node->next = hash_table.nodes[slot];
    hash_table.nodes[slot] = node;
    hash_table.count++;

    return 0;
}

char *kvstore_hash_get(char *key) {
    struct hashnode *node = kvstore_hash_find(key, NULL);

    if (node == NULL) {
        return NULL;
    }

    return node->value;
}

int kvstore_hash_del(char *key) {
    struct hashnode *node = NULL;
    struct hashnode *prev = NULL;
    unsigned int slot = 0;

    if (key == NULL || hash_table.nodes == NULL || hash_table.max_slots <= 0) {
        return -1;
    }

    slot = kvstore_hash_func(key) % hash_table.max_slots;
    node = hash_table.nodes[slot];

    /* 单链表删除：用 prev 记住前驱，找到目标后把前驱的 next 跨过它。 */
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            if (prev == NULL) {
                /* 删的是链表头：直接让桶指向下一个 */
                hash_table.nodes[slot] = node->next;
            } else {
                /* 删的是中间/尾部：前驱跨过当前节点 */
                prev->next = node->next;
            }

            kvstore_free(node->key);
            kvstore_free(node->value);
            kvstore_free(node);
            hash_table.count--;
            return 0;
        }

        prev = node;
        node = node->next;
    }

    return 1;
}

int kvstore_hash_mod(char *key, char *value) {
    struct hashnode *node = NULL;
    char *value_copy = NULL;

    if (key == NULL || value == NULL || hash_table.nodes == NULL) {
        return -1;
    }

    node = kvstore_hash_find(key, NULL);
    if (node == NULL) {
        return 1;
    }

    value_copy = kvstore_strdup(value);
    if (value_copy == NULL) {
        return -1;
    }

    kvstore_free(node->value);
    node->value = value_copy;
    return 0;
}

#endif
