#include "common/minivec.h"
#include "protocol/parser.h"
#include "net/server.h"

/* ============================================================
 * 入口：组装各层。这是"接线"代码，非算法逻辑，已填好。
 *   init engine(db) -> start server(阻塞) -> cleanup
 * ============================================================ */

int main(void) {
    /* 1. 建库：存储 + (后期)索引。度量先用余弦。 */
    minivec_db_t *db = minivec_db_create(METRIC_COSINE);
    if (db == NULL) {
        fprintf(stderr, "[minivec] db create failed\n");
        return -1;
    }

    printf("[minivec] starting on port %d, dim=%d\n", MINIVEC_PORT, MINIVEC_DIM);

    /* 2. 启动网络服务（阻塞在事件循环里）。 */
    int rc = minivec_server_start(MINIVEC_PORT, db);
    if (rc < 0) {
        fprintf(stderr, "[minivec] server start failed\n");
    }

    /* 3. 退出清理。 */
    minivec_db_destroy(db);
    return rc;
}
