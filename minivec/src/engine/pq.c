#include "engine/pq.h"
#include "engine/distance.h"   /* dist_l2sq:子空间距离直接复用 */

#include <stdlib.h>   /* malloc/calloc/free/rand */
#include <string.h>   /* memcpy */

/* ============================================================================
 * PQ 内部布局
 *   codebook 是一块扁平 float:大小 = m * k * dsub
 *   第 m 段、第 j 个质心 = codebook + ((size_t)m*k + j) * dsub
 *   向量第 m 段子向量      = v + m*dsub
 * ----------------------------------------------------------------------------
 * 已填:create/destroy/decode + 取指针小工具。
 * 留白(你填):pq_train(k-means)、pq_encode、pq_build_table、pq_adc。
 * 填完 tests/test_pq 的两条红测试转绿。
 * ============================================================================ */
struct pq {
    int    dim;
    int    m;       /* 段数 */
    int    k;       /* 每段质心数 */
    int    dsub;    /* 每段维度 = dim/m */
    float *codebook;
};

/* 第 m 段第 j 个质心的指针(可写) */
static float *pq_centroid(struct pq *pq, int seg, int j) {
    return pq->codebook + ((size_t)seg * pq->k + j) * pq->dsub;
}

pq_t *pq_create(int dim, int m, int k) {
    if (m <= 0 || k <= 0 || k > 256 || dim % m != 0) return NULL;
    struct pq *pq = (struct pq *)calloc(1, sizeof(*pq));
    if (pq == NULL) return NULL;
    pq->dim  = dim;
    pq->m    = m;
    pq->k    = k;
    pq->dsub = dim / m;
    pq->codebook = (float *)calloc((size_t)m * k * pq->dsub, sizeof(float));
    if (pq->codebook == NULL) { free(pq); return NULL; }
    return pq;
}

void pq_destroy(pq_t *pqh) {
    struct pq *pq = (struct pq *)pqh;
    if (pq == NULL) return;
    free(pq->codebook);
    free(pq);
}

int pq_subdim(const pq_t *pqh) { return ((const struct pq *)pqh)->dsub; }
int pq_m(const pq_t *pqh)      { return ((const struct pq *)pqh)->m; }

/* ★ 留白 1:训练(k-means,每段独立)──────────────────────────
 * pq_train(pq, vecs, n): 对 vecs[n][dim] 的每一段跑 k-means,结果写进码本。
 * 流程树(对 seg in [0, m)):
 *   取每个训练向量的第 seg 段子向量(在 vecs[i*dim + seg*dsub] 处,长 dsub)
 *   ├─ 初始化 k 个质心:从训练子向量里挑 k 个【不同】的点(简单做法:前 k 个;
 *   │   更稳:随机挑,注意别都挑到同一个值)
 *   ├─ 迭代若干轮(如 10~25):
 *   │     assign : 每个点找最近质心(dist_l2sq(子向量, pq_centroid(seg,j), dsub) 取最小)
 *   │     update : 每个质心 = 分到它的点的均值;某簇为空 → 重挑一个随机点补上
 *   └─ 把质心写进 pq_centroid(seg, j)(共 k 个)
 * 复用:子空间距离用 dist_l2sq(a, b, dsub) 即可。
 * 返回 0 成功。
 * TODO(你填) */
int pq_train(pq_t *pqh, const float *vecs, int n) {
    struct pq *pq = (struct pq *)pqh;
    (void)pq; (void)vecs; (void)n;
    return 0;   /* TODO */
}

/* ★ 留白 2:编码 ──────────────────────────────────────────────
 * pq_encode(pq, v, code): 每段取最近质心下标 → code[seg]
 * 流程树:
 *   for seg in [0,m): 在第 seg 段 k 个质心里,找 dist_l2sq(v+seg*dsub, 质心, dsub)
 *                     最小的那个,把它的下标(0..k-1)存进 code[seg]((uint8_t))
 * TODO(你填) */
void pq_encode(pq_t *pqh, const float *v, uint8_t *code) {
    struct pq *pq = (struct pq *)pqh;
    (void)pq; (void)v; (void)code;
    /* TODO */
}

/* 解码/重建(已填):拼接各段所选质心 */
void pq_decode(pq_t *pqh, const uint8_t *code, float *out) {
    struct pq *pq = (struct pq *)pqh;
    for (int seg = 0; seg < pq->m; seg++) {
        memcpy(out + (size_t)seg * pq->dsub,
               pq_centroid(pq, seg, code[seg]),
               (size_t)pq->dsub * sizeof(float));
    }
}

/* ★ 留白 3:建距离表 ──────────────────────────────────────────
 * pq_build_table(pq, query, table): table[seg*k + j] = ‖query 第 seg 段 − 该段第 j 质心‖²
 * 流程树:
 *   for seg in [0,m): for j in [0,k):
 *       table[seg*pq->k + j] = dist_l2sq(query + seg*dsub, pq_centroid(seg,j), dsub)
 * TODO(你填) */
void pq_build_table(pq_t *pqh, const float *query, float *table) {
    struct pq *pq = (struct pq *)pqh;
    (void)pq; (void)query; (void)table;
    /* TODO */
}

/* ★ 留白 4:ADC 查表求近似距离 ────────────────────────────────
 * pq_adc(pq, code, table): Σ_seg table[seg*k + code[seg]]
 * 流程树: float d=0; for seg in [0,m): d += table[seg*pq->k + code[seg]]; return d;
 * TODO(你填) */
float pq_adc(const pq_t *pqh, const uint8_t *code, const float *table) {
    const struct pq *pq = (const struct pq *)pqh;
    (void)pq; (void)code; (void)table;
    return 0.0f;   /* TODO */
}
