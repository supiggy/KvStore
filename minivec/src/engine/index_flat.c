#include "engine/index_flat.h"
#include "engine/distance.h"
#include "engine/topk_heap.h"
#include <stdlib.h>    /* qsort */

/* ============================================================
 * 暴力（brute-force）检索
 * 复杂度 O(N*D)。数据量大时慢，但永远正确 —— 拿它当 HNSW 的标准答案。
 * 堆工具在 engine/topk_heap.{h,c};流程树见 docs/讲解-02-index_flat.md。
 * ============================================================ */

/* 唯一的"分类判断"在这里:不同 metric 都归一成"score 越大越相似",
 * 这样后面永远用一套小顶堆,不必为 L2 单独写大顶堆。 */
static float score_by_metric(metric_t metric, const vec_t *query,
                             const vec_t *v, int dim) {
    switch (metric) {
        case METRIC_COSINE:
        case METRIC_DOT:  return  dist_dot(query, v, dim);    // 本来就越大越相似
        case METRIC_L2:   return -dist_l2sq(query, v, dim);   // ★ 取负 → 也变成越大越相似
        default:          return 0.0f;
    }
}

int flat_search(const vector_store_t *s, const vec_t *query, int topk,
                metric_t metric, search_result_t *out) {
    if (s == NULL || query == NULL || out == NULL || topk <= 0) return 0;

    int size = 0;   /* out 当 topk 小顶堆用;size = 当前堆里元素数 */

    for (size_t i = 0; i < vstore_count(s); i++) {
        const vec_item_t *it = vstore_at(s, i);
        if (it == NULL || it->vec == NULL) continue;   /* 跳过墓碑(已删除) */

        float score = score_by_metric(metric, query, it->vec, MINIVEC_DIM);
        topk_push(out, &size, topk, it->id, score);    /* 维护最优 topk 个 */
    }

    qsort(out, size, sizeof(search_result_t), topk_cmp_desc);  /* 从大到小 */
    return size;   /* 实际个数,可能 < topk */
}
