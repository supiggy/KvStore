#ifndef MINIVEC_INDEX_FLAT_H
#define MINIVEC_INDEX_FLAT_H

#include "common/minivec.h"
#include "engine/vector_store.h"

/* 暴力检索：遍历全库，返回最相似的 topk 个。
 * 它是"正确性基线"——HNSW 的召回率用它当标准答案来衡量。
 *
 * query : 查询向量（COSINE 度量下调用前应已归一化）
 * topk  : 要返回的个数
 * metric: 度量方式
 * out   : 调用方提供的数组，至少 topk 个元素，按相似度从高到低填充
 * 返回  : 实际写入的结果数（可能 < topk，当库里不足 topk 条）
 */
int flat_search(const vector_store_t *s, const vec_t *query, int topk,
                metric_t metric, search_result_t *out);

#endif /* MINIVEC_INDEX_FLAT_H */
