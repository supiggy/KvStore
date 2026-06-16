#ifndef MINIVEC_QUANT_H
#define MINIVEC_QUANT_H

#include "common/minivec.h"
#include <stdint.h>

/* ============================================================
 * int8 标量量化(对称)—— G7
 * float(4B) → int8(1B) + 一个 scale,内存约 4x↓。
 * 对称量化:scale = maxabs/127;q = round(x/scale);x ≈ q*scale。
 * 检索时用量化域内积近似真实内积(精度损失用 G6 的 recall 曲线量)。
 * ============================================================ */

/* 量化 v[dim] -> code[dim](int8),返回 scale(反量化要用)。 */
float q8_encode(const float *v, int dim, int8_t *code);

/* 反量化 code[dim] -> out[dim]。 */
void q8_decode(const int8_t *code, int dim, float scale, float *out);

/* 量化域内积近似:dot(a,b) ≈ (Σ a[i]*b[i]) * sa * sb。
 * 整型累加务必用 int32 防溢出(127*127*dim,dim=384 时约 6.2e6 < 2^31)。 */
float q8_dot(const int8_t *a, float sa, const int8_t *b, float sb, int dim);

#endif /* MINIVEC_QUANT_H */
