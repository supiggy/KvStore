#ifndef MINIVEC_DISTANCE_H
#define MINIVEC_DISTANCE_H

#include "common/minivec.h"

/* 内积：sum(a[i]*b[i])。越大越相似。 */
float dist_dot(const vec_t *a, const vec_t *b, int dim);

/* L2 距离的平方：sum((a[i]-b[i])^2)。越小越相似。
 * 用平方而非开方：单调性相同，省一次 sqrt，topK 排序不受影响。 */
float dist_l2sq(const vec_t *a, const vec_t *b, int dim);

/* 余弦相似度：dot(a,b) / (|a|*|b|)。越大越相似。
 * 若 a、b 都已归一化，则等价于 dist_dot。 */
float dist_cosine(const vec_t *a, const vec_t *b, int dim);

/* 原地归一化：v <- v / |v|。|v|=0 时不变。 */
void vec_normalize(vec_t *v, int dim);

#endif /* MINIVEC_DISTANCE_H */
