/* clock_gettime / CLOCK_MONOTONIC 是 POSIX 扩展;-std=c11 是严格 ISO C,
 * 默认不暴露它们,必须先声明 feature-test 宏(要在所有 include 之前)。 */
#define _POSIX_C_SOURCE 200809L

#include "gen.h"
#include "common/minivec.h"
#include "engine/vector_store.h"
#include "engine/index_flat.h"
#include "engine/index_hnsw.h"
#include "engine/distance.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * MiniVec 引擎基准(G0 骨架)
 * ----------------------------------------------------------------------------
 * 它产出 README 性能表要的数字:flat / hnsw 的 p50/p99 延迟、QPS、build 速度,
 * 以及 HNSW 相对暴力基线的 recall@k。
 *
 * 进程内直接调引擎(不走网络),所以 QPS 是"单线程引擎吞吐",
 * 不含 TCP 开销 —— 这正是 README 里"QPS(单线程)"那一行的含义。
 * (并发压测/网络端到端 QPS 留到 G4 多线程时做。)
 *
 * 数据:随机向量,查询点不在库内(标准 ANN 评测设定,衡量泛化召回)。
 *
 * 两处留白(你填):percentile() 和 recall_at_k()。没填时它们返回 -1,
 * 报告里会显示 -1,程序照常跑完 —— 一眼看出哪两格还空着。
 * 用法: ./run_bench [N] [Q] [topk] [ef]   默认 5000 500 10 50
 * ============================================================================ */

/* 单调时钟,返回微秒(墙钟会被 NTP 调,基准必须用 MONOTONIC) */
static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* 给 qsort 用:double 升序 */
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

/* ★ 留白 1:百分位 ──────────────────────────────────────────────
 * percentile(lat, n, p): 返回延迟数组 lat[0..n) 的第 p 百分位(p∈[0,1],如 0.99)
 * 流程树:
 *   ├─ 升序排序:qsort(lat, n, sizeof(double), cmp_double)
 *   ├─ nearest-rank 取下标:idx = (int)ceil(p * n) - 1
 *   ├─ 夹紧:idx < 0 → 0;idx > n-1 → n-1
 *   └─ return lat[idx]
 * 为什么要 p99 不用平均:平均被极端值"摊平",p99 才反映最差 1% 的真实体验。
 * TODO(你填): 实现它。<math.h> 的 ceil 可用。 */
static double percentile(double *lat, int n, double p) {
    (void)lat; (void)n; (void)p;
    (void)cmp_double;   /* 你填 percentile 时会用到它(qsort 比较器),先引用一下免 unused 警告 */
    return -1.0;   /* TODO */
}

/* ★ 留白 2:recall@k ────────────────────────────────────────────
 * recall_at_k(approx, gt, k): HNSW 结果 approx 与 暴力 ground-truth gt 的 id 交集占比
 * 流程树:
 *   ├─ hit = 0
 *   ├─ for i in [0,k):
 *   │     for j in [0,k):
 *   │         if approx[i].id == gt[j].id: hit++; break
 *   └─ return (double)hit / k
 * 这是衡量近似索引"准不准"的核心指标:1.0 = 和暴力完全一致。
 * TODO(你填): 实现它(双重循环比对 id)。 */
static double recall_at_k(const search_result_t *approx, const search_result_t *gt, int k) {
    (void)approx; (void)gt; (void)k;
    return -1.0;   /* TODO */
}

int main(int argc, char **argv) {
    int N    = (argc > 1) ? atoi(argv[1]) : 5000;
    int Q    = (argc > 2) ? atoi(argv[2]) : 500;
    int topk = (argc > 3) ? atoi(argv[3]) : 10;
    int ef   = (argc > 4) ? atoi(argv[4]) : 50;
    int dim  = MINIVEC_DIM;

    printf("MiniVec bench: N=%d Q=%d topk=%d ef=%d dim=%d\n", N, Q, topk, ef, dim);

    int cap = N + Q + 16;
    vector_store_t *store = vstore_create(dim, cap, METRIC_COSINE);
    hnsw_params_t p = { dim, cap, METRIC_COSINE, /*M*/16, /*ef_c*/200, /*ef_s*/ef };
    hnsw_index_t *hnsw = hnsw_create(&p);
    vec_t *vec = (vec_t *)malloc((size_t)dim * sizeof(vec_t));
    if (!store || !hnsw || !vec) { printf("alloc failed\n"); return 1; }

    /* 1) 灌库 + 建图(计时) */
    gen_seed(12345);
    double t0 = now_us();
    for (int i = 0; i < N; i++) {
        gen_vector(vec, dim);
        vstore_add(store, (uint64_t)(i + 1), vec, NULL);
        hnsw_insert(hnsw, (uint64_t)(i + 1), vec);
    }
    double build_ms = (now_us() - t0) / 1000.0;
    printf("build: %.1f ms (%.0f vec/s)\n\n", build_ms, (double)N / (build_ms / 1000.0));

    /* 2) 查询:逐次计时 + 累计 recall(hnsw vs flat 基线) */
    double *lat_flat = (double *)malloc(sizeof(double) * Q);
    double *lat_hnsw = (double *)malloc(sizeof(double) * Q);
    search_result_t *gt   = (search_result_t *)malloc(sizeof(search_result_t) * topk);
    search_result_t *appr = (search_result_t *)malloc(sizeof(search_result_t) * topk);
    if (!lat_flat || !lat_hnsw || !gt || !appr) { printf("alloc failed\n"); return 1; }

    double tf = 0.0, th = 0.0, recall_sum = 0.0;
    for (int i = 0; i < Q; i++) {
        gen_vector(vec, dim);
        vec_normalize(vec, dim);                 /* COSINE:查询也归一化 */

        double a = now_us();
        int kf = flat_search(store, vec, topk, METRIC_COSINE, gt);
        double b = now_us();
        int kh = hnsw_search(hnsw, vec, topk, appr);
        double c = now_us();

        lat_flat[i] = b - a;  tf += (b - a);
        lat_hnsw[i] = c - b;  th += (c - b);

        int kk = (kf < kh ? kf : kh);
        if (kk > topk) kk = topk;
        recall_sum += recall_at_k(appr, gt, kk > 0 ? kk : topk);
    }

    /* 3) 报告 */
    double qps_flat = (double)Q / (tf / 1e6);
    double qps_hnsw = (double)Q / (th / 1e6);
    printf("%-6s %12s %12s %12s\n", "index", "p50(us)", "p99(us)", "QPS");
    printf("%-6s %12.1f %12.1f %12.0f\n", "flat",
           percentile(lat_flat, Q, 0.50), percentile(lat_flat, Q, 0.99), qps_flat);
    printf("%-6s %12.1f %12.1f %12.0f\n", "hnsw",
           percentile(lat_hnsw, Q, 0.50), percentile(lat_hnsw, Q, 0.99), qps_hnsw);
    printf("\nrecall@%d (hnsw vs flat 基线): %.3f\n", topk, recall_sum / Q);
    printf("提示:p99/recall 显示 -1 = 那两个留白还没填。\n");

    free(vec); free(lat_flat); free(lat_hnsw); free(gt); free(appr);
    hnsw_destroy(hnsw);
    vstore_destroy(store);
    return 0;
}
