#include "engine/index_hnsw.h"
#include "engine/distance.h"

#include <math.h>     /* log */
#include <stdlib.h>   /* malloc/calloc/free/qsort/rand */
#include <string.h>   /* memcpy */

/* ╔══════════════════════════════════════════════════════════════════╗
 * ║  HNSW —— Hierarchical Navigable Small World (分层可导航小世界图)   ║
 * ║  原理 = 跳表的升维:上层稀疏粗导航,Layer0 含全部点精搜。           ║
 * ║  详细讲解见 docs/讲解-03-index_hnsw.md。                           ║
 * ╚══════════════════════════════════════════════════════════════════╝
 *
 * 内部约定:统一成"distance 越小越近"。
 *   L2        : dist = dist_l2sq
 *   COSINE/DOT: dist = -dist_dot   (相似度越大 → 距离越小)
 * 这样图里所有"更近/更远"的比较只有一个方向。
 */

#define MAX_HNSW_LEVEL 16   /* 层级上限,防止极端随机值撑爆邻居数组 */

/* 图节点 */
struct hnsw_node {
    uint64_t  id;
    vec_t    *vec;             /* HNSW 自己拥有一份拷贝(COSINE 下已归一化) */
    int       level;          /* 该点最高层 [0, level] */
    int       deleted;        /* G1:软删墓碑标记(1=已删)。仍留在图里当路由跳板 */
    int      *neighbors;      /* 扁平存各层邻居下标,层 L 的块 = neighbors + L*Mcap */
    int      *neighbor_count; /* 每层当前邻居数,长度 level+1 */
};

struct hnsw_index {
    int       dim;
    metric_t  metric;
    int       M;               /* 上层每点邻居上限 */
    int       M0;              /* Layer0 邻居上限(= 2M) */
    int       Mcap;            /* 每层邻居数组步长(= 2M,统一,简化下标) */
    int       ef_construction;
    int       ef_search;
    double    mL;              /* 层级归一化因子 = 1/ln(M) */

    struct hnsw_node *nodes;
    int       node_count;
    int       deleted_count;   /* G1:墓碑(软删)节点数,供上层决定何时重建 */
    int       capacity;
    int       entry_point;     /* 入口点下标,空图为 -1 */
    int       max_level;       /* 当前最高层,空图为 -1 */

    /* 检索用的复用缓冲(避免每次 malloc) */
    unsigned int *visit_tag;   /* visit_tag[x]==cur_tag 表示本轮已访问 x */
    unsigned int  cur_tag;
    struct cand  *scratch_C;   /* 候选最小堆 */
    struct cand  *scratch_W;   /* 结果最大堆 */
};

/* 堆元素:一个节点 + 它到 query 的距离 */
struct cand { int node; float dist; };

/* ============================================================
 * 距离(统一成越小越近)
 * ============================================================ */
static float hnsw_dist(const struct hnsw_index *h, const vec_t *a, const vec_t *b) {
    if (h->metric == METRIC_L2) return dist_l2sq(a, b, h->dim);
    return -dist_dot(a, b, h->dim);   /* COSINE/DOT */
}

static int *node_neighbors(struct hnsw_index *h, int node, int layer) {
    return h->nodes[node].neighbors + (size_t)layer * h->Mcap;
}

/* 线性扫描:id → 节点下标,找不到返回 -1。
 * (起步 O(N) 扫描;要 O(1) 可加 id→下标 哈希,与 vector_store 同款局限) */
static int hnsw_find_node(struct hnsw_index *h, uint64_t id) {
    for (int i = 0; i < h->node_count; i++) {
        if (h->nodes[i].id == id) return i;
    }
    return -1;
}

/* ============================================================
 * 通用二叉堆(按 dist),is_min=1 小顶堆,is_min=0 大顶堆
 * ============================================================ */
static int cand_higher(struct cand a, struct cand b, int is_min) {
    return is_min ? (a.dist < b.dist) : (a.dist > b.dist);
}
static void cand_siftup(struct cand *h, int i, int is_min) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (cand_higher(h[i], h[p], is_min)) {
            struct cand t = h[i]; h[i] = h[p]; h[p] = t; i = p;
        } else break;
    }
}
static void cand_siftdown(struct cand *h, int n, int i, int is_min) {
    while (1) {
        int l = 2*i+1, r = 2*i+2, best = i;
        if (l < n && cand_higher(h[l], h[best], is_min)) best = l;
        if (r < n && cand_higher(h[r], h[best], is_min)) best = r;
        if (best == i) break;
        struct cand t = h[i]; h[i] = h[best]; h[best] = t; i = best;
    }
}
static void cand_push(struct cand *h, int *n, struct cand x, int is_min) {
    h[*n] = x; cand_siftup(h, *n, is_min); (*n)++;
}
static struct cand cand_pop(struct cand *h, int *n, int is_min) {
    struct cand top = h[0];
    h[0] = h[--(*n)];
    if (*n > 0) cand_siftdown(h, *n, 0, is_min);
    return top;
}

/* 按 dist 升序(给 qsort) */
static int cand_cmp_asc(const void *a, const void *b) {
    float da = ((const struct cand *)a)->dist;
    float db = ((const struct cand *)b)->dist;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* ============================================================
 * random_level:指数衰减抽层级(跳表"抛硬币"的连续版)
 * ============================================================ */
static int random_level(struct hnsw_index *h) {
    double r = (double)rand() / ((double)RAND_MAX + 1.0);  /* [0,1) */
    if (r <= 0.0) r = 1e-12;
    int lvl = (int)(-log(r) * h->mL);
    if (lvl > MAX_HNSW_LEVEL) lvl = MAX_HNSW_LEVEL;
    return lvl;
}

/* ============================================================
 * search_layer:在单层里从 ep 出发,找离 query 最近的 ef 个。
 * 结果留在 h->scratch_W[0..返回值),无序(是个最大堆)。
 *   C = 候选最小堆(先扩展最近的);W = 结果最大堆(堆顶是已知最差的,守 ef)
 * ============================================================ */
static int search_layer(struct hnsw_index *h, const vec_t *q, int ep, int ef, int layer) {
    struct cand *C = h->scratch_C; int cn = 0;
    struct cand *W = h->scratch_W; int wn = 0;
    unsigned int tag = ++h->cur_tag;

    struct cand e = { ep, hnsw_dist(h, q, h->nodes[ep].vec) };
    cand_push(C, &cn, e, 1);   /* 候选:最小堆 */
    cand_push(W, &wn, e, 0);   /* 结果:最大堆 */
    h->visit_tag[ep] = tag;

    while (cn > 0) {
        struct cand c = cand_pop(C, &cn, 1);   /* 最近的待扩展点 */
        if (c.dist > W[0].dist) break;          /* 连最近候选都比已知最差还远 → 停 */

        int *nbr = node_neighbors(h, c.node, layer);
        int ncnt = h->nodes[c.node].neighbor_count[layer];
        for (int j = 0; j < ncnt; j++) {
            int nb = nbr[j];
            if (h->visit_tag[nb] == tag) continue;
            h->visit_tag[nb] = tag;
            float dn = hnsw_dist(h, q, h->nodes[nb].vec);
            if (wn < ef || dn < W[0].dist) {
                struct cand ne = { nb, dn };
                cand_push(C, &cn, ne, 1);
                cand_push(W, &wn, ne, 0);
                if (wn > ef) cand_pop(W, &wn, 0);  /* 超 ef → 弹掉最远 */
            }
        }
    }
    return wn;
}

/* 给 target 在 layer 层加一条到 newnode 的边;满了就保留离 target 最近的 Mlayer 个 */
static void add_neighbor(struct hnsw_index *h, int target, int layer,
                         int newnode, int Mlayer) {
    int *tn   = node_neighbors(h, target, layer);
    int *cnt  = &h->nodes[target].neighbor_count[layer];
    if (*cnt < Mlayer) {
        tn[*cnt] = newnode;
        (*cnt)++;
        return;
    }
    /* 已满:在 已有邻居 + newnode 里挑离 target 最近的 Mlayer 个 */
    const vec_t *tvec = h->nodes[target].vec;
    int m = *cnt + 1;
    struct cand *b = (struct cand *)malloc((size_t)m * sizeof(struct cand));
    for (int i = 0; i < *cnt; i++) {
        b[i].node = tn[i];
        b[i].dist = hnsw_dist(h, tvec, h->nodes[tn[i]].vec);
    }
    b[*cnt].node = newnode;
    b[*cnt].dist = hnsw_dist(h, tvec, h->nodes[newnode].vec);
    qsort(b, m, sizeof(struct cand), cand_cmp_asc);
    int keep = (m < Mlayer) ? m : Mlayer;
    for (int i = 0; i < keep; i++) tn[i] = b[i].node;
    *cnt = keep;
    free(b);
}

/* ============================================================
 * 对外 API
 * ============================================================ */
hnsw_index_t *hnsw_create(const hnsw_params_t *p) {
    if (p == NULL) return NULL;
    struct hnsw_index *h = (struct hnsw_index *)calloc(1, sizeof(*h));
    if (h == NULL) return NULL;

    int M = p->M < 2 ? 2 : p->M;     /* M>=2,否则 ln(M) 为 0 */
    h->dim = p->dim;
    h->metric = p->metric;
    h->M = M;
    h->M0 = 2 * M;
    h->Mcap = 2 * M;
    h->ef_construction = p->ef_construction > 1 ? p->ef_construction : 16;
    h->ef_search = p->ef_search > 1 ? p->ef_search : 16;
    h->mL = 1.0 / log((double)M);
    h->capacity = p->max_elements;
    h->node_count = 0;
    h->deleted_count = 0;
    h->entry_point = -1;
    h->max_level = -1;
    h->cur_tag = 0;

    h->nodes      = (struct hnsw_node *)calloc((size_t)h->capacity, sizeof(struct hnsw_node));
    h->visit_tag  = (unsigned int *)calloc((size_t)h->capacity, sizeof(unsigned int));
    h->scratch_C  = (struct cand *)malloc((size_t)h->capacity * sizeof(struct cand));
    h->scratch_W  = (struct cand *)malloc((size_t)h->capacity * sizeof(struct cand));
    if (!h->nodes || !h->visit_tag || !h->scratch_C || !h->scratch_W) {
        hnsw_destroy((hnsw_index_t *)h);
        return NULL;
    }
    return (hnsw_index_t *)h;
}

void hnsw_destroy(hnsw_index_t *hh) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    if (h == NULL) return;
    if (h->nodes) {
        for (int i = 0; i < h->node_count; i++) {
            free(h->nodes[i].vec);
            free(h->nodes[i].neighbors);
            free(h->nodes[i].neighbor_count);
        }
        free(h->nodes);
    }
    free(h->visit_tag);
    free(h->scratch_C);
    free(h->scratch_W);
    free(h);
}

int hnsw_insert(hnsw_index_t *hh, uint64_t id, const vec_t *vec) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    if (h == NULL || vec == NULL) return -1;
    if (h->node_count >= h->capacity) return -1;

    int nd = h->node_count;
    int level = random_level(h);

    /* 建节点:拷贝向量(COSINE 归一化),分配邻居表 */
    struct hnsw_node *node = &h->nodes[nd];
    node->id = id;
    node->level = level;
    node->deleted = 0;
    node->vec = (vec_t *)malloc((size_t)h->dim * sizeof(vec_t));
    node->neighbors = (int *)malloc((size_t)(level + 1) * h->Mcap * sizeof(int));
    node->neighbor_count = (int *)calloc((size_t)(level + 1), sizeof(int));
    if (!node->vec || !node->neighbors || !node->neighbor_count) return -1;
    memcpy(node->vec, vec, (size_t)h->dim * sizeof(vec_t));
    if (h->metric == METRIC_COSINE) vec_normalize(node->vec, h->dim);
    h->node_count++;

    if (h->entry_point == -1) {           /* 空图:第一个点直接当入口 */
        h->entry_point = nd;
        h->max_level = level;
        return 0;
    }

    int ep = h->entry_point;

    /* 阶段A:从顶层贪心下降到 level+1,只挪 ep,不连边 */
    for (int lc = h->max_level; lc > level; lc--) {
        int cnt = search_layer(h, node->vec, ep, 1, lc);
        if (cnt > 0) ep = h->scratch_W[0].node;   /* ef=1,W 里只有一个 */
    }

    /* 阶段B:从 min(max_level, level) 到 0,逐层连双向边 */
    int start = (h->max_level < level) ? h->max_level : level;
    for (int lc = start; lc >= 0; lc--) {
        int cnt = search_layer(h, node->vec, ep, h->ef_construction, lc);
        qsort(h->scratch_W, cnt, sizeof(struct cand), cand_cmp_asc);  /* 近→远 */

        int Mlayer = (lc == 0) ? h->M0 : h->M;
        int sel = (cnt < Mlayer) ? cnt : Mlayer;

        /* 新点 → 选中的 Mlayer 个 */
        int *ndn = node_neighbors(h, nd, lc);
        for (int k = 0; k < sel; k++) ndn[k] = h->scratch_W[k].node;
        node->neighbor_count[lc] = sel;

        /* 反向边:选中的点 → 新点(满了就裁剪) */
        for (int k = 0; k < sel; k++) {
            add_neighbor(h, h->scratch_W[k].node, lc, nd, Mlayer);
        }

        if (cnt > 0) ep = h->scratch_W[0].node;   /* 最近点作为下一层入口 */
    }

    if (level > h->max_level) {
        h->entry_point = nd;
        h->max_level = level;
    }
    return 0;
}

int hnsw_search(hnsw_index_t *hh, const vec_t *query, int topk, search_result_t *out) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    if (h == NULL || query == NULL || out == NULL || topk <= 0) return 0;
    if (h->entry_point == -1) return 0;

    int ep = h->entry_point;

    /* 阶段A:顶层贪心下降到第 1 层,ef=1 粗导航 */
    for (int lc = h->max_level; lc > 0; lc--) {
        int cnt = search_layer(h, query, ep, 1, lc);
        if (cnt > 0) ep = h->scratch_W[0].node;
    }

    /* 阶段B:Layer0 用大 ef 精搜 */
    int ef = (h->ef_search > topk) ? h->ef_search : topk;
    int cnt = search_layer(h, query, ep, ef, 0);
    qsort(h->scratch_W, cnt, sizeof(struct cand), cand_cmp_asc);  /* 近→远 */

    /* 取 topk:scratch_W 已按 近→远 排好。
     * ★ 留白(G1 修 bug):跳过墓碑(已删)节点。
     *   现状(没填)= 老行为:已删节点照样被返回 → 这就是 DESIGN §3.3 的 bug。
     *   只要在下面循环里加一行:若 h->nodes[node].deleted 则 continue。
     * 要点:
     *   - 删除的节点仍被 search_layer 遍历(当路由跳板),只是不放进结果 —— 软删的关键。
     *   - 删得多时,前 ef 个候选里活节点可能不足 topk → 返回数 < topk;
     *     真要补满需调大 ef_search(进阶,先不管)。
     * TODO(你填): 在循环里加 "若该节点已删则 continue"。 */
    int n = 0;
    for (int i = 0; i < cnt && n < topk; i++) {
        int node = h->scratch_W[i].node;
        /* TODO(你填): if (h->nodes[node].deleted) continue; */
        out[n].id    = h->nodes[node].id;
        out[n].score = -h->scratch_W[i].dist;   /* 距离取负 → 还原成"越大越相似" */
        n++;
    }
    return n;
}

/* G1:软删 —— 标记墓碑。节点仍留在图里被遍历,但 hnsw_search 不再返回它
 * (前提:你已填上面 hnsw_search 的过滤留白)。返回 0 成功,-1 未找到。 */
int hnsw_delete(hnsw_index_t *hh, uint64_t id) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    if (h == NULL) return -1;
    int nd = hnsw_find_node(h, id);
    if (nd < 0) return -1;
    if (!h->nodes[nd].deleted) {
        h->nodes[nd].deleted = 1;
        h->deleted_count++;
    }
    return 0;
}

/* G1:当前墓碑数(上层据此决定是否重建索引) */
int hnsw_deleted_count(hnsw_index_t *hh) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    return h ? h->deleted_count : 0;
}

void hnsw_set_ef_search(hnsw_index_t *hh, int ef) {
    struct hnsw_index *h = (struct hnsw_index *)hh;
    if (h && ef > 0) h->ef_search = ef;
}
