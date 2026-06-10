#include "engine/vector_store.h"
#include "engine/distance.h"

/* ============================================================================
 * 存储层  ——  照搬 kvstore 的 array 引擎（最简单那个，你最早写的）
 * ----------------------------------------------------------------------------
 * 对应关系（kvstore 文件: kvstore.h 的 kvs_array_item / kvstore_array_*）：
 *   kvstore                          MiniVec                  说明
 *   --------------------------------------------------------------------------
 *   struct kvs_array_item{key,value} vec_item_t{id,vec,meta}  一条记录
 *   定长数组 + 线性扫描               定长数组 + 线性扫描       同一种"最朴素存法"
 *   kvstore_array_set                vstore_add               存：查重→找位→拷贝
 *   kvstore_array_get                vstore_get               查：线性找 key/id
 *   kvstore_array_del                vstore_del               删
 *
 * 改动点（务必看清）：
 *   [改1] key→id：kvstore 的 key 是字符串、用 strcmp 比较；
 *         MiniVec 的 id 是 uint64、用 == 比较。
 *   [改2] value→向量：kvstore 存 strdup(value)；MiniVec 存 malloc+memcpy 的 float 数组，
 *         且余弦度量下入库时归一化（kvstore 没有这一步）。
 *   [改3] 存储/检索分家：kvstore 的 array 引擎把"查"也包了（精确匹配很简单）；
 *         MiniVec 只负责"存"，"按相似度查"交给 index_flat / index_hnsw。
 *         所以这里多了个 vstore_at() 给检索层遍历全库用。
 *   [改4] 删除：起步用"墓碑标记"(把 vec 置 NULL)，不做物理搬移——因为物理删除
 *         还要顾及 HNSW 图连通性（见 index_hnsw.c 注释），留到以后。
 *
 * 性能备注：线性扫描 get/del 是 O(N)，和 kvstore array 引擎一样慢。
 *           想要 O(1)，照搬 kvstore 的 hash 引擎做 id->slot 映射即可（后续优化）。
 * ============================================================================ */

struct vector_store {
    int         dim;
    int         capacity;
    metric_t    metric;
    vec_item_t *items;    /* items[slot]，slot 即物理下标 */
    size_t      count;    /* 已使用的物理槽位数（含墓碑），= 追加位置 */
};

/* 内部小工具：拷贝一段字符串（避免用非 C11 标准的 strdup） */
static char *dup_str(const char *s) {
    if (s == NULL) return NULL;
    size_t n = strlen(s) + 1;
    char *p = (char *)mv_malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* vstore_create —— 分配定长数组（对应 kvstore array 的固定大小数组） */
vector_store_t *vstore_create(int dim, int capacity, metric_t metric) {
    vector_store_t *s = (vector_store_t *)mv_malloc(sizeof(*s));
    if (s == NULL) return NULL;

    s->dim      = dim;
    s->capacity = capacity;
    s->metric   = metric;
    s->count    = 0;
    s->items    = (vec_item_t *)mv_calloc((size_t)capacity, sizeof(vec_item_t));
    if (s->items == NULL) {
        mv_free(s);
        return NULL;
    }
    return s;
}

/* vstore_destroy —— 释放每条记录的向量/meta，再释放数组本身 */
void vstore_destroy(vector_store_t *s) {
    if (s == NULL) return;
    for (size_t i = 0; i < s->count; i++) {
        mv_free(s->items[i].vec);    /* 墓碑位 vec 已是 NULL，mv_free(NULL) 安全 */
        mv_free(s->items[i].meta);
    }
    mv_free(s->items);
    mv_free(s);
}

/* 内部：线性查找 id 的物理槽位，找不到/已删除返回 -1
 * 对应 kvstore_array_get 里那段 for 循环 strcmp 比较，只是 strcmp→== */
static int find_slot(const vector_store_t *s, uint64_t id) {
    for (size_t i = 0; i < s->count; i++) {
        if (s->items[i].vec != NULL && s->items[i].id == id) {
            return (int)i;
        }
    }
    return -1;
}

/* vstore_add —— 对应 kvstore_array_set
 * 流程：查重 → 满判断 → 追加槽位 → 拷贝向量(可选归一化) → count++ */
int vstore_add(vector_store_t *s, uint64_t id, const vec_t *vec, const char *meta) {
    if (s == NULL || vec == NULL) return -1;

    /* 查重：id 已存在则拒绝（对应 kvstore_array_set 找到同 key 就返回） */
    if (find_slot(s, id) >= 0) return 1;

    /* 满判断 */
    if (s->count >= (size_t)s->capacity) return -1;

    size_t slot = s->count;   /* 追加到末尾（最简单的找空位方式） */

    /* [改2] 拷贝向量：malloc dim 个 float 再 memcpy（kvstore 是 strdup 字符串） */
    vec_t *copy = (vec_t *)mv_malloc((size_t)s->dim * sizeof(vec_t));
    if (copy == NULL) return -1;
    memcpy(copy, vec, (size_t)s->dim * sizeof(vec_t));

    /* 余弦度量：入库即归一化，之后检索用内积即可。
     * 注意：vec_normalize 现在是 distance.c 里的桩(空实现)，
     *       实现 distance 之前这行不起作用，但不影响存储正确性。 */
    if (s->metric == METRIC_COSINE) {
        vec_normalize(copy, s->dim);
    }

    s->items[slot].id   = id;
    s->items[slot].vec  = copy;
    s->items[slot].meta = dup_str(meta);
    s->count++;
    return 0;
}

/* vstore_get —— 对应 kvstore_array_get：线性找 id，返回只读记录指针 */
const vec_item_t *vstore_get(vector_store_t *s, uint64_t id) {
    if (s == NULL) return NULL;
    int slot = find_slot(s, id);
    return (slot < 0) ? NULL : &s->items[slot];
}

/* vstore_del —— 对应 kvstore_array_del
 * [改4] 起步用墓碑标记：释放向量、把 vec 置 NULL；count 不变，槽位不搬移。
 *       后续遍历(vstore_at)与检索都靠 vec==NULL 跳过墓碑。 */
int vstore_del(vector_store_t *s, uint64_t id) {
    if (s == NULL) return -1;
    int slot = find_slot(s, id);
    if (slot < 0) return -1;

    mv_free(s->items[slot].vec);
    mv_free(s->items[slot].meta);
    s->items[slot].vec  = NULL;   /* 墓碑：标记此槽已删除 */
    s->items[slot].meta = NULL;
    return 0;
}

/* vstore_count —— 当前物理槽位数。
 * 备注：含墓碑位，起步够用；要精确"存活数"可另维护一个 live 计数（日后再说）。 */
size_t vstore_count(const vector_store_t *s) {
    return s ? s->count : 0;
}

/* vstore_at —— [改3] kvstore array 引擎没有这个口子。
 * 给检索层(index_flat/hnsw)按物理下标遍历全库用；墓碑位返回的 item.vec==NULL，
 * 调用方需自行跳过。idx 越界返回 NULL。 
 * 备注：这个函数是 返回当前物理下标 idx 的记录指针，idx 从 0 到 count-1；如果 idx 越界或 s==NULL 则返回 NULL。
 * */
const vec_item_t *vstore_at(const vector_store_t *s, size_t idx) {
    if (s == NULL || idx >= s->count) return NULL;
    return &s->items[idx];
}
