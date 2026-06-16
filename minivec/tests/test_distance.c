#include "test_framework.h"
#include "engine/distance.h"
#include "common/minivec.h"

/* ============================================================
 * distance 单测
 * 已填:dot / l2sq 的手算对照(给你看断言怎么写)
 * 留白:normalize 与 cosine 的性质(你填)
 * ============================================================ */
void test_distance_suite(void) {
    vec_t a[3] = {1.0f, 2.0f, 3.0f};
    vec_t b[3] = {4.0f, 5.0f, 6.0f};

    /* dot = 1*4 + 2*5 + 3*6 = 32 */
    MV_CHECK_FEQ(dist_dot(a, b, 3), 32.0f, 1e-4f, "dot([1,2,3],[4,5,6]) == 32");

    /* l2sq = 3^2 + 3^2 + 3^2 = 27 (不开方) */
    MV_CHECK_FEQ(dist_l2sq(a, b, 3), 27.0f, 1e-4f, "l2sq([1,2,3],[4,5,6]) == 27");

    /* ★ 留白 1:归一化后模长应为 1 ───────────────────────
     * 流程树:
     *   v = {3, 4}            (二维,模长 = sqrt(9+16) = 5)
     *   vec_normalize(v, 2)   → 期望 v ≈ {0.6, 0.8}
     *   dist_dot(v, v, 2)     → 期望 ≈ 1.0  (单位向量自点积=1)
     * TODO(你填): 构造 v={3,4},调 vec_normalize,用 MV_CHECK_FEQ
     *             断言 dist_dot(v,v,2) ≈ 1.0 */
    /* TODO */

    /* ★ 留白 2:cosine 等价于"归一化后的 dot" ────────────
     * 流程树:
     *   c1 = dist_cosine(a, b, 3)
     *   把 a、b 各自 vec_normalize 后,c2 = dist_dot(a, b, 3)
     *   期望 c1 ≈ c2
     * (这正是"余弦=归一化后内积"——库里 COSINE 度量入库即归一化的原因)
     * TODO(你填): 实现上面的对照断言 */
    /* TODO */
}
