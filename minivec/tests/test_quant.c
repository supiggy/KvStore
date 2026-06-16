#include "test_framework.h"
#include "engine/quant.h"
#include "engine/distance.h"
#include "common/minivec.h"
#include <math.h>

/* ============================================================================
 * G7:int8 量化测试(TDD —— 两条红,填 quant.c 后转绿)
 *   1) 反量化误差 <= 量化步长(scale)
 *   2) 量化域内积 q8_dot 与浮点 dist_dot 相对误差 < 5%
 * 用确定性正值向量,保证内积明显非零、相对误差稳定。
 * ============================================================================ */
void test_quant_suite(void) {
    vec_t a[MINIVEC_DIM], b[MINIVEC_DIM];
    for (int i = 0; i < MINIVEC_DIM; i++) {
        a[i] = (float)((i % 7) + 1);   /* 1..7 */
        b[i] = (float)((i % 5) + 1);   /* 1..5 */
    }

    int8_t ca[MINIVEC_DIM], cb[MINIVEC_DIM];
    float sa = q8_encode(a, MINIVEC_DIM, ca);
    float sb = q8_encode(b, MINIVEC_DIM, cb);

    /* 1) 反量化:逐元素误差不超过一个量化步长 */
    vec_t back[MINIVEC_DIM];
    q8_decode(ca, MINIVEC_DIM, sa, back);
    float maxerr = 0.0f;
    for (int i = 0; i < MINIVEC_DIM; i++) {
        float e = fabsf(a[i] - back[i]);
        if (e > maxerr) maxerr = e;
    }
    MV_CHECK(maxerr <= sa + 1e-4f,
             "int8 反量化误差 <= 量化步长  (★ 填 q8_encode/q8_decode 才绿)");

    /* 2) 量化域内积 ≈ 浮点内积 */
    float exact  = dist_dot(a, b, MINIVEC_DIM);
    float approx = q8_dot(ca, sa, cb, sb, MINIVEC_DIM);
    float rel = fabsf(exact - approx) / (fabsf(exact) + 1e-6f);
    MV_CHECK(rel < 0.05f,
             "q8_dot 与浮点内积相对误差 < 5%  (★ 填 q8_dot 才绿)");
}
