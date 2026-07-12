#ifndef MINIVEC_TOPK_HEAP_H
#define MINIVEC_TOPK_HEAP_H

#include "common/minivec.h"   /* search_result_t */

/* ============================================================
 * topK 小顶堆(工具模块)
 * 按 score 维护"当前最优的 K 个",堆顶是这 K 个里 score 最小的(门槛)。
 * 用于 index_flat / index_hnsw 的 topK 检索。
 *
 * 约定:堆数组由调用方提供(容量 >= K),size 由调用方维护(初值 0)。
 *       本模块只提供"塞一个进去"和"输出排序",不持有内存。
 * ============================================================ */

/* 尝试把一个候选 (id, score) 放进容量 K 的小顶堆。
 *   heap: 容量 >= K 的数组
 *   size: 当前元素数(传地址,内部会更新;调用前置 0)
 *   K   : 目标个数
 * 规则:没满 K 个直接放;满了且 score 大于门槛(堆顶)才替换堆顶。 */
void topk_push(search_result_t *heap, int *size, int K, uint64_t id, float score);

/* qsort 比较器:按 score 从大到小排序。检索结束、输出前用。
 * 用法: qsort(out, n, sizeof(search_result_t), topk_cmp_desc); */
int topk_cmp_desc(const void *a, const void *b);

#endif /* MINIVEC_TOPK_HEAP_H */
