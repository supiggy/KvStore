#include "engine/quant.h"
#include <math.h>   /* fabsf / lroundf */

/* ============================================================================
 * int8 标量量化 —— 三处留白(你填)。填完 tests/test_quant 的两条红测试转绿。
 * ============================================================================ */

/* ★ 留白 1:量化 ──────────────────────────────────────────────
 * q8_encode(v, dim, code): 把 v 量化进 code,返回 scale
 * 流程树:
 *   ├─ maxabs = max_i |v[i]|
 *   ├─ if maxabs == 0: 把 code 全置 0,return 1.0f(避免除零)
 *   ├─ scale = maxabs / 127.0f
 *   ├─ for i: q = lroundf(v[i] / scale);
 *   │         若 q > 127 取 127,< -127 取 -127;  code[i] = (int8_t)q
 *   └─ return scale
 * TODO(你填) */
float q8_encode(const float *v, int dim, int8_t *code) {
    (void)v; (void)dim; (void)code;
    return 1.0f;   /* TODO */
}

/* ★ 留白 2:反量化 ────────────────────────────────────────────
 * q8_decode(code, dim, scale, out): for i: out[i] = code[i] * scale
 * TODO(你填) */
void q8_decode(const int8_t *code, int dim, float scale, float *out) {
    (void)code; (void)dim; (void)scale; (void)out;
    /* TODO */
}

/* ★ 留白 3:量化域内积 ────────────────────────────────────────
 * q8_dot(a, sa, b, sb, dim):
 *   ├─ int32_t acc = 0;
 *   ├─ for i: acc += (int)a[i] * (int)b[i];   // int8*int8 提升为 int,累加进 int32
 *   └─ return (float)acc * sa * sb;
 * TODO(你填) */
float q8_dot(const int8_t *a, float sa, const int8_t *b, float sb, int dim) {
    (void)a; (void)sa; (void)b; (void)sb; (void)dim;
    return 0.0f;   /* TODO */
}
