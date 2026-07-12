/* ============================================================================
 * LeetCode 706. Design HashMap —— 从零实现哈希表(纯 C)
 * ----------------------------------------------------------------------------
 * 编译运行: gcc -Wall leetcode-706-hashmap.c -o hashmap && ./hashmap
 *
 * 为什么放这:vector_store 的 `id -> slot` 现在是 O(N) 线性扫描(讲解-04)。
 *   想升级成 O(1),就要把它换成哈希表 —— 本题就是从零实现一个哈希表。
 *   把这里的 key 当成 id、value 当成 slot,就是 vector_store 的提速方案。
 *
 * 核心知识点:
 *   1. 哈希函数:key -> 桶下标(key % 桶数)
 *   2. 哈希冲突:不同 key 落到同一个桶 → 链地址法(每个桶挂一条链表)
 *   3. put/get/remove 都是"先定位桶,再在桶内链表里找"
 * ============================================================================ */
#include <stdio.h>
#include <stdlib.h>

#define NBUCKET 1024   /* 桶数量。桶越多冲突越少,但越费内存 */

/* 链表结点:同一个桶里的多个 (key,val) 串成链表 */
typedef struct Node {
    int key, val;
    struct Node *next;
} Node;

typedef struct {
    Node *buckets[NBUCKET];   /* 每个桶是一条链表的头指针 */
} MyHashMap;

/* 哈希函数:key -> [0, NBUCKET) 的桶下标 */
static int hash(int key) {
    return (key & 0x7fffffff) % NBUCKET;   /* 去负号再取模 */
}

MyHashMap *myHashMapCreate(void) {
    return (MyHashMap *)calloc(1, sizeof(MyHashMap));  /* 所有桶初始化为 NULL */
}

/* put 流程树
 * ----------------------------------------------------------
 * put(key, value)
 *   ├─ h = hash(key)
 *   ├─ 遍历 buckets[h] 链表:
 *   │     找到同 key → 更新 val,返回
 *   └─ 没找到 → new 结点,头插到 buckets[h]
 */
void myHashMapPut(MyHashMap *m, int key, int value) {
    int h = hash(key);
    for (Node *p = m->buckets[h]; p; p = p->next) {
        if (p->key == key) { p->val = value; return; }   /* 已存在 → 更新 */
    }
    Node *node = (Node *)malloc(sizeof(Node));
    node->key = key;
    node->val = value;
    node->next = m->buckets[h];   /* 头插:新结点指向原链表头 */
    m->buckets[h] = node;
}

/* get 流程树
 * ----------------------------------------------------------
 * get(key)
 *   ├─ h = hash(key)
 *   ├─ 遍历 buckets[h]:同 key 返回 val
 *   └─ 没找到 → -1
 */
int myHashMapGet(MyHashMap *m, int key) {
    for (Node *p = m->buckets[hash(key)]; p; p = p->next) {
        if (p->key == key) return p->val;
    }
    return -1;
}

/* remove 流程树(链表删除,用二级指针最干净)
 * ----------------------------------------------------------
 * remove(key)
 *   ├─ pp 指向 "指向当前结点的指针"
 *   └─ 走链表:找到同 key → *pp = 它的 next,free 它
 */
void myHashMapRemove(MyHashMap *m, int key) {
    Node **pp = &m->buckets[hash(key)];
    while (*pp) {
        if ((*pp)->key == key) {
            Node *del = *pp;
            *pp = del->next;
            free(del);
            return;
        }
        pp = &(*pp)->next;
    }
}

void myHashMapFree(MyHashMap *m) {
    for (int i = 0; i < NBUCKET; i++) {
        Node *p = m->buckets[i];
        while (p) { Node *n = p->next; free(p); p = n; }
    }
    free(m);
}

/* ===================== 测试 ===================== */
int main(void) {
    MyHashMap *m = myHashMapCreate();
    myHashMapPut(m, 1, 100);
    myHashMapPut(m, 2, 200);
    printf("get(1) = %d  (expect 100)\n", myHashMapGet(m, 1));
    printf("get(3) = %d  (expect -1)\n",  myHashMapGet(m, 3));
    myHashMapPut(m, 1, 111);                       /* 更新已有 key */
    printf("get(1) = %d  (expect 111)\n", myHashMapGet(m, 1));
    myHashMapRemove(m, 1);
    printf("get(1) = %d  (expect -1 删除后)\n", myHashMapGet(m, 1));

    /* 演示冲突:1 和 1+NBUCKET 落同一个桶,链表也能正确区分 */
    myHashMapPut(m, 1, 1);
    myHashMapPut(m, 1 + NBUCKET, 999);
    printf("get(1)=%d get(%d)=%d  (expect 1 和 999,同桶不串味)\n",
           myHashMapGet(m, 1), 1 + NBUCKET, myHashMapGet(m, 1 + NBUCKET));

    myHashMapFree(m);
    return 0;
}
