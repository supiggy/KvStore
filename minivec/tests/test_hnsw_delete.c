#include "test_framework.h"
#include "engine/index_hnsw.h"
#include "common/minivec.h"
#include <string.h>

/* ============================================================================
 * G1:HNSW 软删除测试(TDD —— 这条测试编码了 DESIGN §3.3 的 bug)
 * ----------------------------------------------------------------------------
 * 最后一条断言现在是【红】的:删除 id=5 后,VSEARCH 仍会返回它。
 * 你在 index_hnsw.c 的 hnsw_search 里加上"跳过墓碑"那一行后,它会变【绿】。
 * 绿 = G1 的完成信号。
 * ============================================================================ */
static void put_x(hnsw_index_t *h, uint64_t id, float x) {
    vec_t v[MINIVEC_DIM];
    memset(v, 0, sizeof(v));
    v[0] = x;
    hnsw_insert(h, id, v);
}

void test_hnsw_delete_suite(void) {
    hnsw_params_t p = { MINIVEC_DIM, 100, METRIC_L2, /*M*/16, /*ef_c*/100, /*ef_s*/50 };
    hnsw_index_t *h = hnsw_create(&p);
    MV_CHECK(h != NULL, "hnsw_create 返回非 NULL");

    put_x(h, 1, 1.0f);
    put_x(h, 5, 5.0f);
    put_x(h, 10, 10.0f);

    /* 删 id=5(它本是 x=4 查询的最近邻) */
    MV_CHECK(hnsw_delete(h, 5) == 0, "hnsw_delete(5) 返回 0");
    MV_CHECK(hnsw_deleted_count(h) == 1, "deleted_count == 1");
    MV_CHECK(hnsw_delete(h, 999) == -1, "删不存在的 id 返回 -1");

    /* query x=4:删除后结果里不应再出现 id=5 */
    vec_t q[MINIVEC_DIM];
    memset(q, 0, sizeof(q));
    q[0] = 4.0f;

    search_result_t out[3];
    int k = hnsw_search(h, q, 3, out);
    int found5 = 0;
    for (int i = 0; i < k; i++) {
        if (out[i].id == 5) found5 = 1;
    }
    MV_CHECK(found5 == 0,
             "VSEARCH 不再返回已删的 id=5  (★ 填了 hnsw_search 墓碑过滤才会绿)");

    hnsw_destroy(h);
}
