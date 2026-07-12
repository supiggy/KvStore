#ifndef MV_BENCH_METRICS_H
#define MV_BENCH_METRICS_H

#include "common/minivec.h"

/* ============================================================
 * 基准用的两个核心指标(G0/G6 共享)
 * 这两个函数是留白(你填):bench_engine(单点) 和 sweep(扫描) 都靠它们出数。
 * ============================================================ */

/* 延迟数组的第 p 百分位(p∈[0,1],如 0.99 取 p99)。
 * 注意:会就地排序 lat[0..n)。 */
double percentile(double *lat, int n, double p);

/* HNSW 结果 approx 与 暴力 ground-truth gt 的 id 交集占比(recall@k)。
 * 1.0 = 与暴力完全一致。 */
double recall_at_k(const search_result_t *approx, const search_result_t *gt, int k);

#endif /* MV_BENCH_METRICS_H */
