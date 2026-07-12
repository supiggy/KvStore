#include "test_framework.h"
#include "engine/pq.h"
#include "engine/distance.h"
#include "common/minivec.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ============================================================================
 * G8:PQ 乘积量化测试(TDD —— 2 条红,填 pq.c 后转绿)
 * 用"4 个原型 + 小噪声"的可聚类数据:K=16 个质心足够把它们表达好,
 * 所以正确实现的重建 MSE 极小;桩实现(码本全 0)重建误差很大 → 红。
 * ============================================================================ */
void test_pq_suite(void) {
    int dim = MINIVEC_DIM, m = 8, k = 16, n = 200;

    pq_t *pq = pq_create(dim, m, k);
    MV_CHECK(pq != NULL, "pq_create(dim,8,16) 非 NULL");
    MV_CHECK(pq_create(dim, 7, 16) == NULL, "dim 不被 m 整除时返回 NULL");
    if (pq == NULL) return;

    /* 可聚类数据:4 个原型 + ~[-0.01,0.01] 噪声 */
    float *data = (float *)malloc(sizeof(float) * (size_t)n * dim);
    unsigned int st = 12345u;
    for (int i = 0; i < n; i++) {
        int c = i % 4;
        for (int d = 0; d < dim; d++) {
            st ^= st << 13; st ^= st >> 17; st ^= st << 5;
            float noise = ((float)(st & 0xFFFFu) / 65535.0f - 0.5f) * 0.02f;
            data[(size_t)i * dim + d] = sinf((float)c * 1.3f + (float)d * 0.02f) + noise;
        }
    }

    MV_CHECK(pq_train(pq, data, n) == 0, "pq_train 返回 0");

    /* 1) 重建 MSE 足够小 */
    uint8_t code[64];
    float rec[MINIVEC_DIM];
    double sse = 0.0;
    for (int i = 0; i < n; i++) {
        memset(code, 0, sizeof(code));
        pq_encode(pq, data + (size_t)i * dim, code);
        pq_decode(pq, code, rec);
        for (int d = 0; d < dim; d++) {
            float e = data[(size_t)i * dim + d] - rec[d];
            sse += (double)e * (double)e;
        }
    }
    double mse = sse / ((double)n * dim);
    MV_CHECK(mse < 0.05, "PQ 重建 MSE < 0.05  (★ 填 pq_train/pq_encode 才绿)");

    /* 2) ADC 距离 ≈ 直接重建距离(同一组平方差之和) */
    float *table = (float *)malloc(sizeof(float) * (size_t)m * k);
    const float *q = data;                 /* 用第 0 条当查询 */
    memset(code, 0, sizeof(code));
    pq_encode(pq, q, code);
    pq_build_table(pq, q, table);
    pq_decode(pq, code, rec);
    float adc    = pq_adc(pq, code, table);
    float direct = dist_l2sq(q, rec, dim);
    MV_CHECK(fabsf(adc - direct) <= 1e-3f * (direct + 1.0f),
             "ADC 距离 ≈ 重建距离  (★ 填 pq_build_table/pq_adc 才绿)");

    free(table);
    free(data);
    pq_destroy(pq);
}
