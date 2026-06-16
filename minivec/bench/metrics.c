#include "metrics.h"
#include <stdlib.h>   /* qsort */
#include <math.h>     /* ceil */

/* ============================================================================
 * 基准核心指标 —— 两处留白(G0/G6 共享,你填这一个文件即可)
 * 填完后:run_bench 的 p50/p99/recall、run_sweep 的每行数据 都会从 -1 变成真值。
 * ============================================================================ */

/* 给 qsort 用:double 升序 */
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* ★ 留白 1:百分位 ──────────────────────────────────────────────
 * percentile(lat, n, p): 返回 lat[0..n) 的第 p 百分位(p∈[0,1])
 * 流程树:
 *   ├─ 升序排序:qsort(lat, n, sizeof(double), cmp_double)
 *   ├─ nearest-rank 取下标:idx = (int)ceil(p * n) - 1
 *   ├─ 夹紧:idx < 0 → 0;idx > n-1 → n-1
 *   └─ return lat[idx]
 * 为什么用 p99 不用平均:平均被极端值"摊平",p99 才反映最差 1% 的真实体验。
 * TODO(你填) */
double percentile(double *lat, int n, double p) {
    (void)lat; (void)n; (void)p;
    (void)cmp_double;   /* 实现时用它做 qsort 比较器 */
    return -1.0;   /* TODO */
}

/* ★ 留白 2:recall@k ────────────────────────────────────────────
 * recall_at_k(approx, gt, k): approx 与 gt 的 id 交集占比
 * 流程树:
 *   ├─ hit = 0
 *   ├─ for i in [0,k):
 *   │     for j in [0,k): if approx[i].id == gt[j].id: hit++; break
 *   └─ return (double)hit / k
 * TODO(你填) */
double recall_at_k(const search_result_t *approx, const search_result_t *gt, int k) {
    (void)approx; (void)gt; (void)k;
    return -1.0;   /* TODO */
}
