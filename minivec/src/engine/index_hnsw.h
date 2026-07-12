#ifndef MINIVEC_INDEX_HNSW_H
#define MINIVEC_INDEX_HNSW_H

#include "common/minivec.h"

/* 不透明类型：图结构藏在 .c 里 */
typedef struct hnsw_index hnsw_index_t;

/* HNSW 三个核心参数（面试必考，含义见 index_hnsw.c 顶部注释） */
typedef struct {
    int      dim;
    int      max_elements;
    metric_t metric;
    int      M;                /* 每个节点在每层连的邻居数（Layer0 常用 2*M） */
    int      ef_construction;  /* 建图时的搜索宽度（候选集大小） */
    int      ef_search;        /* 查询时的搜索宽度（越大越准越慢，可运行时调） */
} hnsw_params_t;

hnsw_index_t *hnsw_create(const hnsw_params_t *p);
void          hnsw_destroy(hnsw_index_t *h);

/* 插入一个向量（建图）。vec 由调用方拥有，HNSW 内部按需拷贝或引用。
 * 返回 0 成功。 */
int hnsw_insert(hnsw_index_t *h, uint64_t id, const vec_t *vec);

/* 查询最相似的 topk 个，写入 out[]。返回实际个数。 */
int hnsw_search(hnsw_index_t *h, const vec_t *query, int topk, search_result_t *out);

/* 运行时调节查询精度/速度的旋钮 */
void hnsw_set_ef_search(hnsw_index_t *h, int ef);

/* G1:软删除一个 id(打墓碑,仍留在图里当路由,但不再被检索返回)。
 * 返回 0 成功,-1 未找到。 */
int hnsw_delete(hnsw_index_t *h, uint64_t id);

/* G1:当前墓碑(已删)节点数。 */
int hnsw_deleted_count(hnsw_index_t *h);

#endif /* MINIVEC_INDEX_HNSW_H */
