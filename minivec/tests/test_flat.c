#include "test_framework.h"
#include "engine/vector_store.h"
#include "engine/index_flat.h"
#include "common/minivec.h"
#include <string.h>

/* ============================================================
 * index_flat 单测(暴力检索是正确性基线,必须先对)
 * 构造一维可手算的数据集:三个点落在 x 轴 1 / 5 / 10。
 * 已填:top-1 命中
 * 留白:top-2 顺序(你填)
 * ============================================================ */
static void put_x(vector_store_t *s, uint64_t id, float x) {
    vec_t v[MINIVEC_DIM];
    memset(v, 0, sizeof(v));
    v[0] = x;
    vstore_add(s, id, v, NULL);
}

void test_flat_suite(void) {
    vector_store_t *s = vstore_create(MINIVEC_DIM, 100, METRIC_L2);
    put_x(s, 1, 1.0f);
    put_x(s, 5, 5.0f);
    put_x(s, 10, 10.0f);

    /* 查询点 x=4:到 1 距离 9(平方)、到 5 距离 1、到 10 距离 36 → 最近是 id=5 */
    vec_t q[MINIVEC_DIM];
    memset(q, 0, sizeof(q));
    q[0] = 4.0f;

    search_result_t out[3];
    int k = flat_search(s, q, 1, METRIC_L2, out);
    MV_CHECK(k == 1, "flat top-1 返回 1 条");
    MV_CHECK(out[0].id == 5, "x=4 的最近邻是 id=5");

    /* ★ 留白:top-2 的顺序 ──────────────────────────────
     * 流程树:
     *   query x=4 → 到 5 距离1、到 1 距离9、到 10 距离36
     *   按相似度从高到低(距离从小到大): [id=5, id=1]
     * TODO(你填): flat_search 取 topk=2,断言 out[0].id==5 && out[1].id==1 */
    /* TODO */

    vstore_destroy(s);
}
