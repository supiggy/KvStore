#include "test_framework.h"
#include "engine/vector_store.h"
#include "common/minivec.h"
#include <string.h>

/* ============================================================
 * vector_store 单测
 * 已填:add / count / get 命中与未命中
 * 留白:删除墓碑语义、重复 id 拒绝(你填)
 * ============================================================ */
void test_vstore_suite(void) {
    /* 用 L2 度量,避免入库归一化改写向量值,断言更直观 */
    vector_store_t *s = vstore_create(MINIVEC_DIM, 100, METRIC_L2);
    MV_CHECK(s != NULL, "vstore_create 返回非 NULL");

    vec_t v[MINIVEC_DIM];
    memset(v, 0, sizeof(v));
    v[0] = 1.0f;

    MV_CHECK(vstore_add(s, 1, v, NULL) == 0, "add id=1 返回 0(成功)");
    MV_CHECK(vstore_count(s) == 1, "add 后 count == 1");
    MV_CHECK(vstore_get(s, 1) != NULL, "get id=1 命中");
    MV_CHECK(vstore_get(s, 999) == NULL, "get 不存在的 id -> NULL");

    /* ★ 留白 1:删除后语义 ──────────────────────────────
     * 流程树:
     *   vstore_del(s, 1)   → 期望返回 0
     *   vstore_get(s, 1)   → 期望 NULL(被打成墓碑)
     *   注意:vstore_count 含墓碑,删后可能仍 == 1,别拿 count 判断删没删
     * TODO(你填): 删除 id=1,断言 del 返回 0 且 get 返回 NULL */
    /* TODO */

    /* ★ 留白 2:重复 id 应被拒绝 ─────────────────────────
     * 流程树:
     *   再 vstore_add(s, 1, v, NULL)   → 期望返回 != 0(实现里重复返回 1)
     * TODO(你填): 断言重复插入返回非 0 */
    /* TODO */

    vstore_destroy(s);
}
