#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kvstore.h"

#if ENABLE_SKIPTABLE_KVENGINE

/* ============================================================================
 *  跳表引擎（Skip List，概率型有序链表）
 * ----------------------------------------------------------------------------
 *  思路：在一条**有序单链表**之上，再随机给部分节点"加高"，建出多层稀疏的索引链，
 *  查找时从最高层往右走、走不动就下沉一层，像坐电梯快速跳过大段节点。
 *
 *      level 3:  H ----------------------> 25 -----------> NULL
 *      level 2:  H --------> 9 ----------> 25 -----------> NULL
 *      level 1:  H --> 6 --> 9 --> 17 ---> 25 --> 30 ----> NULL
 *      level 0:  H --> 6 --> 9 --> 17 ---> 25 --> 30 ----> NULL   (最底层是完整链表)
 *
 *  H 是哨兵头节点(key=NULL)，每个节点带一个 forward[] 数组：forward[i] 指向同层下一个节点。
 *  节点的层高在插入时**抛硬币随机决定**（P=0.5：一半节点只 1 层、1/4 到 2 层……），
 *  期望层数 O(log n)，所以增删查期望都是 O(log n)，且最底层天然有序、范围查询友好。
 *
 *  为什么用它（对比红黑树）：实现简单、范围查询/前驱后继直接顺着底层链走、并发改造容易，
 *  没有旋转那套复杂操作——这正是 Redis 有序集合(zset) 选跳表的原因。
 *
 *  语义约定（与 array/rbtree/hash 引擎一致）：
 *    set: 0 成功 / 1 key 已存在(不覆盖) / -1 出错 —— SET 只负责"新增"，改值用 MOD
 *    get: 返回 value 指针 / NULL 表示不存在
 *    del/mod: 0 成功 / 1 不存在 / -1 出错
 * ========================================================================== */
#define SKIPLIST_MAX_LEVEL 32   /* 层高上限：2^32 个节点也够用，定长省去扩容 */

struct skipnode {
    char *key;
    char *value;
    struct skipnode **forward;  /* 长度为本节点层高，forward[i] 是第 i 层的后继 */
};

struct skiplist {
    struct skipnode *header;    /* 哨兵头节点：key=NULL，forward 拉满 MAX_LEVEL 层 */
    int level;                  /* 当前实际用到的最高层数（1..MAX_LEVEL） */
    int count;                  /* 节点总数（不含哨兵） */
};

static struct skiplist skip_list = {0};

//strdup 的变体，失败返回 NULL，成功返回新分配的字符串指针（调用者负责 free）。
//是用来复制字符串的函数，它会分配足够的内存来存储源字符串，并将源字符串的内容复制到新分配的内存中。调用者需要负责释放返回的指针所指向的内存，以避免内存泄漏。
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

/* 抛硬币定层高：每多一层概率减半（P=0.5）。level 从 1 起步，连续抛到正面就加一层，
 * 抛到反面或到达上限就停。这样大多数节点只占 1 层，越高的层节点越稀疏。 */
static int skiplist_random_level(void) {
    int level = 1;

    while ((rand() & 0x1) && level < SKIPLIST_MAX_LEVEL) {
        level++;
    }

    return level;
}

/* 从最高层往下、每层向右走到"下一个 key >= 目标"前停住，定位到目标节点（或 NULL）。
 * get/mod 只需要节点本身，不关心各层前驱，所以单独抽一个查找函数。 */
static struct skipnode *skiplist_search_node(char *key) {
    struct skipnode *x = NULL;
    int i = 0;

    if (key == NULL || skip_list.header == NULL) {
        return NULL;
    }

    x = skip_list.header;
    for (i = skip_list.level - 1; i >= 0; i--) {
        while (x->forward[i] != NULL && strcmp(x->forward[i]->key, key) < 0) {
            x = x->forward[i];
        }
    }

    /* 此时 x 是最底层里"最后一个 key < 目标"的节点，它的后继才可能正是目标。 */
    x = x->forward[0];
    if (x != NULL && strcmp(x->key, key) == 0) {
        return x;
    }

    return NULL;
}

int kvstore_skiplist_create(void) {
    int i = 0;

    if (skip_list.header != NULL) {
        return 0;
    }

    skip_list.header = kvstore_malloc(sizeof(struct skipnode));
    if (skip_list.header == NULL) {
        return -1;
    }

    /* 哨兵头节点的 forward 一次性开满 MAX_LEVEL 层，省得后续抬高时再扩容。 */
    skip_list.header->forward =
        kvstore_malloc(sizeof(struct skipnode *) * SKIPLIST_MAX_LEVEL);
    if (skip_list.header->forward == NULL) {
        kvstore_free(skip_list.header);
        skip_list.header = NULL;
        return -1;
    }

    for (i = 0; i < SKIPLIST_MAX_LEVEL; i++) {
        skip_list.header->forward[i] = NULL;
    }

    skip_list.header->key = NULL;
    skip_list.header->value = NULL;
    skip_list.level = 1;
    skip_list.count = 0;

    srand((unsigned int)time(NULL));   /* 给随机层高播种 */
    return 0;
}

void kvstore_skiplist_destroy(void) {
    struct skipnode *node = NULL;

    if (skip_list.header == NULL) {
        return;
    }

    /* 沿最底层(level 0)的完整链表逐个释放，每个节点只会被走到一次。 */
    node = skip_list.header->forward[0];
    while (node != NULL) {
        struct skipnode *next = node->forward[0];
        kvstore_free(node->key);
        kvstore_free(node->value);
        kvstore_free(node->forward);
        kvstore_free(node);
        node = next;
    }

    kvstore_free(skip_list.header->forward);
    kvstore_free(skip_list.header);
    skip_list.header = NULL;
    skip_list.level = 0;
    skip_list.count = 0;
}

int kvstore_skiplist_set(char *key, char *value) {
    struct skipnode *update[SKIPLIST_MAX_LEVEL] = {0};
    struct skipnode *x = NULL;
    struct skipnode *node = NULL;
    char *key_copy = NULL;
    char *value_copy = NULL;
    int new_level = 0;
    int i = 0;

    if (key == NULL || value == NULL || skip_list.header == NULL) {
        return -1;
    }

    /* 自顶向下查找，并用 update[i] 记下每一层"插入位置的前驱"，后面接线要用。 */
    x = skip_list.header;
    for (i = skip_list.level - 1; i >= 0; i--) {
        while (x->forward[i] != NULL && strcmp(x->forward[i]->key, key) < 0) {
            x = x->forward[i];
        }
        update[i] = x;
    }

    /* 查重：已存在则返回 1（SET 不覆盖）。 */
    x = x->forward[0];
    if (x != NULL && strcmp(x->key, key) == 0) {
        return 1;
    }

    /* 先把所有内存都分配好再动链表，任一步失败就回滚，且无需撤销已改的结构。 */
    new_level = skiplist_random_level();

    node = kvstore_malloc(sizeof(struct skipnode));
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

    node->forward = kvstore_malloc(sizeof(struct skipnode *) * new_level);
    if (node->forward == NULL) {
        kvstore_free(value_copy);
        kvstore_free(key_copy);
        kvstore_free(node);
        return -1;
    }

    node->key = key_copy;
    node->value = value_copy;

    /* 新节点比当前最高层还高：拔高的那几层前驱只能是哨兵头节点。 */
    if (new_level > skip_list.level) {
        for (i = skip_list.level; i < new_level; i++) {
            update[i] = skip_list.header;
        }
        skip_list.level = new_level;
    }

    /* 逐层把新节点接进去：和单链表头插同理，先接后继再改前驱。 */
    for (i = 0; i < new_level; i++) {
        node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = node;
    }

    skip_list.count++;
    return 0;
}

char *kvstore_skiplist_get(char *key) {
    struct skipnode *node = skiplist_search_node(key);

    if (node == NULL) {
        return NULL;
    }

    return node->value;
}

int kvstore_skiplist_del(char *key) {
    struct skipnode *update[SKIPLIST_MAX_LEVEL] = {0};
    struct skipnode *x = NULL;
    int i = 0;

    if (key == NULL || skip_list.header == NULL) {
        return -1;
    }

    /* 同样自顶向下，记下每层前驱，方便解链。 */
    x = skip_list.header;
    for (i = skip_list.level - 1; i >= 0; i--) {
        while (x->forward[i] != NULL && strcmp(x->forward[i]->key, key) < 0) {
            x = x->forward[i];
        }
        update[i] = x;
    }

    x = x->forward[0];
    if (x == NULL || strcmp(x->key, key) != 0) {
        return 1;
    }

    /* 逐层解链：某层的前驱后继不是 x，说明 x 没到这一层，再往上也不会有，提前停。 */
    for (i = 0; i < skip_list.level; i++) {
        if (update[i]->forward[i] != x) {
            break;
        }
        update[i]->forward[i] = x->forward[i];
    }

    kvstore_free(x->key);
    kvstore_free(x->value);
    kvstore_free(x->forward);
    kvstore_free(x);

    /* 删完后顶部若空出了若干层，回收当前层高，避免查找空跑高层。 */
    while (skip_list.level > 1 &&
           skip_list.header->forward[skip_list.level - 1] == NULL) {
        skip_list.level--;
    }

    skip_list.count--;
    return 0;
}

int kvstore_skiplist_mod(char *key, char *value) {
    struct skipnode *node = NULL;
    char *value_copy = NULL;

    if (key == NULL || value == NULL || skip_list.header == NULL) {
        return -1;
    }

    node = skiplist_search_node(key);
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
