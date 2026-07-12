/* clock_gettime 需要(见 bench_engine.c 同样的说明) */
#define _POSIX_C_SOURCE 200809L

#include "gen.h"
#include "metrics.h"
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
 * G6:参数扫描基准 —— 扫 (M, ef_search),为每个配置量 recall / p99 / QPS,
 * 输出 CSV(再用 bench/plot_recall.py 画 recall–QPS 曲线)。
 * ----------------------------------------------------------------------------
 * 两个助手已写好(直接用):
 *   build_hnsw(...)       用给定 M / ef_construction 建一次图,返回索引 + 建图耗时
 *   measure_queries(...)  在已建好的图上跑 Q 个查询,返回该配置的一行结果
 *                         (ef_search 用 hnsw_set_ef_search 在查询前设定,不重建图)
 *
 * 你要填的是 main 里的【扫描循环】:外层 M(每个 M 重建图)、内层 ef_search
 * (复用同一张图,只调旋钮)。这正是 G6 要你体会的:哪个参数变了必须重建,
 * 哪个是查询时旋钮。
 *
 * 数据/查询用固定 seed,保证不同配置之间公平可比、且可复现。
 * 用法: ./run_sweep [N] [Q] [topk] [out.csv]   默认 5000 500 10 bench/results.csv
 * ============================================================================ */

#define DATA_SEED  12345u
#define QUERY_SEED 999u
#define EF_CONSTRUCTION 200   /* 固定建图宽度,只扫 M 与 ef_search */

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* 一个配置的测量结果(一行) */
typedef struct {
    int    M;
    int    ef_search;
    double recall;     /* 平均 recall@topk */
    double p50_us;
    double p99_us;
    double qps;
    double build_ms;
} sweep_row_t;

/* ---- 助手 1:建图(已填) ----------------------------------------------------
 * 用 store 里已有的 N 条向量,按给定 M / ef_construction 建一张 HNSW。
 * build_ms_out 写回建图耗时。失败返回 NULL。 */
static hnsw_index_t *build_hnsw(const vector_store_t *store, int dim, metric_t metric,
                                int M, int ef_construction, double *build_ms_out) {
    int n = (int)vstore_count(store);
    hnsw_params_t p = { dim, n + 16, metric, M, ef_construction, /*ef_search 占位*/16 };
    hnsw_index_t *h = hnsw_create(&p);
    if (h == NULL) return NULL;

    double t0 = now_us();
    for (size_t i = 0; i < vstore_count(store); i++) {
        const vec_item_t *it = vstore_at(store, i);
        if (it == NULL || it->vec == NULL) continue;   /* 跳墓碑 */
        hnsw_insert(h, it->id, it->vec);
    }
    if (build_ms_out) *build_ms_out = (now_us() - t0) / 1000.0;
    return h;
}

/* ---- 助手 2:在已建好的图上跑查询(已填) ------------------------------------
 * ef_search 是查询时旋钮:set 一下即可,不用重建图。
 * 用 flat_search 当 ground truth 算 recall。queries 用固定 seed,保证可比。
 * 返回一行(M/ef_search 由调用方填回)。 */
static sweep_row_t measure_queries(hnsw_index_t *h, const vector_store_t *store,
                                   int Q, int topk, int dim, metric_t metric,
                                   int ef_search,
                                   vec_t *qbuf, double *lat,
                                   search_result_t *gt, search_result_t *appr) {
    sweep_row_t row;
    memset(&row, 0, sizeof(row));
    row.ef_search = ef_search;

    hnsw_set_ef_search(h, ef_search);

    gen_seed(QUERY_SEED);   /* 每个配置用同一组查询 */
    double th = 0.0, recall_sum = 0.0;
    for (int i = 0; i < Q; i++) {
        gen_vector(qbuf, dim);
        if (metric == METRIC_COSINE) vec_normalize(qbuf, dim);

        int kf = flat_search(store, qbuf, topk, metric, gt);   /* ground truth */
        double a = now_us();
        int kh = hnsw_search(h, qbuf, topk, appr);
        lat[i] = now_us() - a;
        th += lat[i];

        int kk = (kf < kh ? kf : kh);
        if (kk > topk) kk = topk;
        recall_sum += recall_at_k(appr, gt, kk > 0 ? kk : topk);
    }

    row.recall = recall_sum / Q;
    row.p50_us = percentile(lat, Q, 0.50);
    row.p99_us = percentile(lat, Q, 0.99);
    row.qps    = (double)Q / (th / 1e6);
    return row;
}

/* 把一行写到 CSV 和屏幕 */
static void emit_row(FILE *csv, sweep_row_t r) {
    fprintf(csv, "%d,%d,%.4f,%.1f,%.1f,%.0f,%.1f\n",
            r.M, r.ef_search, r.recall, r.p50_us, r.p99_us, r.qps, r.build_ms);
    printf("M=%-3d ef=%-4d  recall=%.4f  p50=%8.1fus  p99=%8.1fus  QPS=%8.0f\n",
           r.M, r.ef_search, r.recall, r.p50_us, r.p99_us, r.qps);
}

int main(int argc, char **argv) {
    int   N    = (argc > 1) ? atoi(argv[1]) : 5000;
    int   Q    = (argc > 2) ? atoi(argv[2]) : 500;
    int   topk = (argc > 3) ? atoi(argv[3]) : 10;
    const char *out = (argc > 4) ? argv[4] : "bench/results.csv";
    int   dim  = MINIVEC_DIM;
    metric_t metric = METRIC_COSINE;

    /* 扫描网格:M 变要重建图;ef_search 是查询旋钮(measure_queries 内部 set) */
    int M_values[]  = { 8, 16, 32 };
    int ef_values[] = { 16, 32, 64, 128, 256 };
    int nM  = (int)(sizeof(M_values)  / sizeof(M_values[0]));
    int nef = (int)(sizeof(ef_values) / sizeof(ef_values[0]));

    printf("sweep: N=%d Q=%d topk=%d dim=%d  M=%d个 ef=%d个 -> %d 行\n",
           N, Q, topk, dim, nM, nef, nM * nef);

    /* 灌库一次(所有配置共用同一份数据) */
    vector_store_t *store = vstore_create(dim, N + 16, metric);
    vec_t *vbuf = (vec_t *)malloc((size_t)dim * sizeof(vec_t));
    double *lat = (double *)malloc(sizeof(double) * Q);
    search_result_t *gt   = (search_result_t *)malloc(sizeof(search_result_t) * topk);
    search_result_t *appr = (search_result_t *)malloc(sizeof(search_result_t) * topk);
    if (!store || !vbuf || !lat || !gt || !appr) { printf("alloc failed\n"); return 1; }

    gen_seed(DATA_SEED);
    for (int i = 0; i < N; i++) {
        gen_vector(vbuf, dim);
        vstore_add(store, (uint64_t)(i + 1), vbuf, NULL);
    }

    FILE *csv = fopen(out, "w");
    if (csv == NULL) { printf("cannot open %s\n", out); return 1; }
    fprintf(csv, "M,ef_search,recall,p50_us,p99_us,qps,build_ms\n");   /* 表头 */

    /* ★ 留白(G6 核心):扫描循环 ────────────────────────────────
     * 流程树:
     *   for mi in [0, nM):
     *       M = M_values[mi]
     *       double build_ms;
     *       hnsw_index_t *h = build_hnsw(store, dim, metric, M, EF_CONSTRUCTION, &build_ms);
     *       for ei in [0, nef):
     *           ef = ef_values[ei]
     *           sweep_row_t r = measure_queries(h, store, Q, topk, dim, metric,
     *                                           ef, vbuf, lat, gt, appr);
     *           r.M = M;  r.build_ms = build_ms;   // 助手没填这两个,这里补
     *           emit_row(csv, r);
     *       hnsw_destroy(h);                        // 换 M 前销毁,别漏(否则内存泄漏)
     * 关键洞察:外层每个 M 只 build 一次,内层 ef 复用同一张图 —— 因为 ef_search
     *          不改变图结构,只改查询时搜多宽。把 build 写进内层就白白重建 nef 次。
     * TODO(你填) */
    /* 这三个就是上面流程树要你调用的助手;填完扫描循环后可删掉本行(届时自然被用到) */
    (void)build_hnsw; (void)measure_queries; (void)emit_row;
    printf("(扫描循环为留白:填了 main 里的 TODO 才会产生数据行)\n");

    fclose(csv);
    printf("\nCSV -> %s   画图: python3 bench/plot_recall.py %s\n", out, out);

    free(vbuf); free(lat); free(gt); free(appr);
    vstore_destroy(store);
    return 0;
}
