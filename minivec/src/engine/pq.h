#ifndef MINIVEC_PQ_H
#define MINIVEC_PQ_H

#include "common/minivec.h"
#include <stdint.h>

/* ============================================================
 * PQ —— Product Quantization 乘积量化(G8,招牌菜)
 * ------------------------------------------------------------
 * 思路:把 dim 维向量切成 M 段子向量(每段 dsub=dim/M 维),
 *   对每一段单独跑 k-means 训出 K 个质心(码本)。
 *   一个向量 → M 个"质心下标"(每个 uint8,K<=256)→ 共 M 字节。
 *   压缩比 = dim*4 / M(例:dim=384, M=48 → 48B vs 1536B = 32x)。
 *
 * 检索用 ADC(非对称距离计算):
 *   查询向量不量化;先对每段预算"查询子向量 → 该段 K 个质心"的距离表
 *   table[M][K];之后任意库向量的近似距离 = Σ_m table[m][code[m]]
 *   —— M 次查表加法,极快。
 *
 * 不透明类型;码本布局藏在 .c。
 * ============================================================ */
typedef struct pq pq_t;

/* 创建:dim 必须能被 m 整除;k<=256(码下标用 uint8)。失败返回 NULL。 */
pq_t *pq_create(int dim, int m, int k);
void  pq_destroy(pq_t *pq);

int   pq_subdim(const pq_t *pq);   /* dsub = dim/m */
int   pq_m(const pq_t *pq);        /* 段数 M */

/* 训练:对 vecs[n][dim] 的每一段跑 k-means,填码本。返回 0 成功。 */
int   pq_train(pq_t *pq, const float *vecs, int n);

/* 编码:v[dim] → code[m](每段取最近质心下标)。 */
void  pq_encode(pq_t *pq, const float *v, uint8_t *code);

/* 解码(重建):code[m] → out[dim](拼接各段所选质心)。 */
void  pq_decode(pq_t *pq, const uint8_t *code, float *out);

/* 建距离表:table[m*k + j] = ‖query 第 m 段 − 第 m 段第 j 个质心‖²。
 * 调用方提供 table,长度 m*k。 */
void  pq_build_table(pq_t *pq, const float *query, float *table);

/* ADC:用距离表求库向量近似距离 = Σ_m table[m*k + code[m]]。 */
float pq_adc(const pq_t *pq, const uint8_t *code, const float *table);

#endif /* MINIVEC_PQ_H */
