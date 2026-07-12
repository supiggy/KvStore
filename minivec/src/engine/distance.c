#include "engine/distance.h"
#include <math.h>   // sqrtf（dist_cosine / vec_normalize 用到）

/* ============================================================
 * 距离 / 相似度计算
 * 这是整个库最底层、最热的代码（每次检索会被调用 N 次）。
 * 先写对，再写快（SIMD/多线程是后期优化，不要一上来就做）。
 * ============================================================ */

/* dist_dot 流程树
 * ----------------------------------------------------------
 * dist_dot(a, b, dim)
 *   ├─ s = 0
 *   ├─ for i in [0, dim):
 *   │     s += a[i] * b[i]
 *   └─ return s
 * 优化预留：循环可用 AVX 一次算 8 个 float（分支5 SIMD），先别做。
 */
//这里传入的 a、b 是指向向量数据的指针，dim 是向量的维度。函数通过循环计算 a[i] * b[i] 的和来得到内积。
float dist_dot(const vec_t *a, const vec_t *b, int dim) {
    float s = 0.0f;
    for (int i = 0; i < dim; i++)
    {
        s += a[i] * b[i];
    }
    
    return s;
}

/* dist_l2sq 流程树
 * ----------------------------------------------------------
 * dist_l2sq(a, b, dim)
 *   ├─ s = 0
 *   ├─ for i in [0, dim):
 *   │     d = a[i] - b[i]
 *   │     s += d * d
 *   └─ return s          (不开方，保持单调性即可)
 */
//这里算的是，L2 距离的平方，
//即每个维度上 a[i] 和 b[i] 的差的平方的和。函数通过循环计算 a[i] - b[i] 的差值 d，然后将 d 的平方累加到 s 中，最后返回 s。
float dist_l2sq(const vec_t *a, const vec_t *b, int dim) {
    float s = 0.0f;
    for (int i = 0; i < dim; i++)
    {
        float d = a[i] - b[i];
        s += d * d;
    }
    return s;
}

/* dist_cosine 流程树
 * ----------------------------------------------------------
 * dist_cosine(a, b, dim)
 *   ├─ dot  = sum(a[i]*b[i])
 *   ├─ na   = sqrt(sum(a[i]^2))
 *   ├─ nb   = sqrt(sum(b[i]^2))
 *   ├─ if na==0 or nb==0: return 0
 *   └─ return dot / (na*nb)
 * 工程提示：若入库和查询向量都已 vec_normalize，检索时直接用 dist_dot 即可，
 *           不必每次重算模长 —— 这就是"归一化后余弦=内积"的性能意义。
 */
float dist_cosine(const vec_t *a, const vec_t *b, int dim) {
    float dot = dist_dot(a, b, dim);
    float na = sqrtf(dist_dot(a, a, dim));
    float nb = sqrtf(dist_dot(b, b, dim));
    if (na == 0.0f || nb == 0.0f) {
        return 0.0f;
    }
    return dot / (na * nb);
}

/* vec_normalize 流程树
 * ----------------------------------------------------------
 * vec_normalize(v, dim)
 *   ├─ norm = sqrt(sum(v[i]^2))
 *   ├─ if norm == 0: return        (零向量不处理，避免除零)
 *   └─ for i in [0, dim): v[i] /= norm
 */
void vec_normalize(vec_t *v, int dim) {

    float norm = 0.0f;
    norm = sqrtf(dist_dot(v, v, dim));
    if (norm == 0.0f) {
        return;
    }
    for (int i = 0; i < dim; i++)
    {
        v[i] /= norm;
    }
}
