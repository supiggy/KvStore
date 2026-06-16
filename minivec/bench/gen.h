#ifndef MV_BENCH_GEN_H
#define MV_BENCH_GEN_H

#include "common/minivec.h"

/* 确定性随机向量生成(基础设施,已填好)。
 * 同一个 seed 必产同一组向量 —— 基准要可复现,不能每次跑都不一样。 */
void gen_seed(unsigned int seed);

/* 往 out 填 dim 个 [-1, 1] 的随机 float */
void gen_vector(vec_t *out, int dim);

#endif /* MV_BENCH_GEN_H */
