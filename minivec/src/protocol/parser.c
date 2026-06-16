#define _POSIX_C_SOURCE 200809L   /* 暴露 pthread_rwlock_*(G3);-std=c11 默认不给 */

#include "protocol/parser.h"
#include "engine/vector_store.h"
#include "engine/index_flat.h"
#include "engine/index_hnsw.h"
#include "engine/distance.h"
#include "engine/persist.h"

#include <stdlib.h>   /* strtoull / strtof / atoi */
#include <pthread.h>  /* G3:读写锁 */

/* ============================================================================
 * 协议层  ——  照搬 kvstore.c 的命令分发框架
 * ----------------------------------------------------------------------------
 * 对应关系（kvstore 文件: kvstore.c）：
 *   kvstore                          MiniVec                      说明
 *   --------------------------------------------------------------------------
 *   comands[]                        commands[]                   命令名表（kvstore 拼成 comands）
 *   enum KVS_CMD_*                   enum MV_CMD_*                命令枚举
 *   kvstore_command_index()          minivec_command_index()     命令名→枚举，逻辑一致
 *   kvstore_spilt_token()            split_token()                strtok(" \r\n\t")，逻辑一致
 *   kvstore_request()+               minivec_handle_command()     切 token + 大 switch 分发
 *     kvstore_parse_protocol()                                    （MiniVec 合成一个函数）
 *   kvstore_array_set/get/del        vstore_add/get/del           增删查对应
 *   kvstore_write_*_response()       内联 snprintf                响应写法
 *
 * 改动点（务必看清）：
 *   [改1] 命令集：SET/GET/DEL/MOD  →  VADD/VSEARCH/VDEL/VCOUNT。
 *   [改2] value：字符串  →  DIM 维 float 向量（多了一步 parse_vector 把 token 转 float）。
 *   [改3] 查询语义：GET 精确取 1 个  →  VSEARCH 按相似度取 topK 个（多行结果）。
 *   [改4] 引擎句柄：kvstore 用全局引擎；MiniVec 用传入的 db（store + index）。
 *   [改5] 读写分离：kvstore 读写都在 conn_item；MiniVec 形参 line(输入)/out(输出) 分开。
 *
 * 注意：本文件里 vstore_* 已实现，可直接用；
 *       flat_search / hnsw_search / vec_normalize 仍是 TODO 桩，
 *       所以 VADD/VDEL/VCOUNT 现在就能跑，VSEARCH 要等你把检索实现了才有结果。
 * ============================================================================ */

#define MV_MAX_TOKENS (MINIVEC_DIM + 4)   /* 对应 kvstore KVSTORE_MAX_TOKENS */
#define MV_MAX_TOPK   256                 /* VSEARCH 单次返回上限，防止超大 topk */

/* 对应 kvstore comands[]：命令名字符串表 */
static const char *commands[] = {
    "VADD", "VSEARCH", "VDEL", "VCOUNT", "SAVE", "LOAD",
};

/* 对应 kvstore enum KVS_CMD_*：枚举顺序必须和 commands[] 一一对应 */
enum {
    MV_CMD_START = 0,
    MV_CMD_VADD = MV_CMD_START,
    MV_CMD_VSEARCH,
    MV_CMD_VDEL,
    MV_CMD_VCOUNT,
    MV_CMD_SAVE,
    MV_CMD_LOAD,
    MV_CMD_COUNT,
};

/* 应用层上下文：把"存储 + 索引"捆在一起（见 parser.h 说明） */
struct minivec_db {
    metric_t        metric;
    vector_store_t *store;
    hnsw_index_t   *index;   /* 起步为 NULL：检索走暴力 flat_search */
    pthread_rwlock_t lock;   /* G3:保护 store+index,多 reactor 线程并发访问时用 */
};

/* ---- db 生命周期：对应 kvstore main() 里引擎的 create/destroy ----------- */

minivec_db_t *minivec_db_create(metric_t metric) {
    minivec_db_t *db = (minivec_db_t *)mv_malloc(sizeof(*db));
    if (db == NULL) return NULL;
    db->metric = metric;
    db->store  = vstore_create(MINIVEC_DIM, MINIVEC_MAX_ELEMENTS, metric);

    /* HNSW 索引:VADD 时同步建图,VSEARCH 走近似检索(≈O(log N))。
     * M=16, ef_construction=200(建图质量), ef_search=50(查询默认精度,可调)。 */
    hnsw_params_t p = {
        MINIVEC_DIM, MINIVEC_MAX_ELEMENTS, metric,
        /*M*/16, /*ef_construction*/200, /*ef_search*/50
    };
    db->index = hnsw_create(&p);

    if (db->store == NULL || db->index == NULL) {
        if (db->index) hnsw_destroy(db->index);
        if (db->store) vstore_destroy(db->store);
        mv_free(db);
        return NULL;
    }
    pthread_rwlock_init(&db->lock, NULL);   /* G3 */
    return db;
}

void minivec_db_destroy(minivec_db_t *db) {
    if (db == NULL) return;
    pthread_rwlock_destroy(&db->lock);      /* G3 */
    if (db->index) hnsw_destroy(db->index);
    if (db->store) vstore_destroy(db->store);
    mv_free(db);
}

/* ============================================================================
 * G3:引擎并发保护(读写锁)
 * ----------------------------------------------------------------------------
 * G4 的多个 reactor 工作线程会并发调 minivec_handle_command,共享同一个 db。
 * 约定:读命令(VSEARCH/VCOUNT/SAVE)用读锁(可并发);
 *       写命令(VADD/VDEL/LOAD)用写锁(互斥)。锁只罩在引擎操作上,
 *       参数解析/响应格式化都在锁外(缩短临界区)。
 *
 * ★ 留白:把下面三个函数体补上 —— 就是 pthread_rwlock 的三个调用。
 *   没填 = 空操作,单线程仍正确;但 G4 多线程下会数据竞争。所以:先填 G3 再跑 G4。
 *
 * 谈资:写锁串行化了 VADD(含 ef_construction=200 的建图,较慢)→ 会阻塞所有读。
 *       这正是"分片锁"的动机(进阶):按 id 分 N 片,各片独立锁,降低争用。
 * ============================================================================ */
static void db_read_lock(minivec_db_t *db) {
    (void)db;
    /* TODO(你填): pthread_rwlock_rdlock(&db->lock); */
}
static void db_write_lock(minivec_db_t *db) {
    (void)db;
    /* TODO(你填): pthread_rwlock_wrlock(&db->lock); */
}
static void db_unlock(minivec_db_t *db) {
    (void)db;
    /* TODO(你填): pthread_rwlock_unlock(&db->lock); */
}

/* ---- split_token：照搬 kvstore_spilt_token --------------------------------
 * 把命令行按 空格/\r/\n/\t 切成 token 数组，返回 token 个数。逻辑与 kvstore 一致。 */
static int split_token(char *line, char **tokens) {
    if (line == NULL || tokens == NULL) return -1;

    int idx = 0;
    char *tok = strtok(line, " \r\n\t");
    while (tok != NULL && idx < MV_MAX_TOKENS) {
        tokens[idx++] = tok;
        tok = strtok(NULL, " \r\n\t");
    }
    return idx;
}

/* ---- minivec_command_index：照搬 kvstore_command_index --------------------
 * 命令名字符串 → 枚举下标；找不到返回 MV_CMD_COUNT。 */
static int minivec_command_index(const char *name) {
    if (name == NULL) return MV_CMD_COUNT;
    for (int c = MV_CMD_START; c < MV_CMD_COUNT; c++) {
        if (strcmp(commands[c], name) == 0) return c;
    }
    return MV_CMD_COUNT;
}

/* ---- parse_vector：[改2] kvstore 没有这一步 --------------------------------
 * 把 token 字符串数组转成 DIM 维 float 向量。
 * 这相当于 kvstore 里"取 value 字符串"的位置，只是 value 现在是一串浮点数。
 * 返回 0 成功，-1 维度不足。 */
static int parse_vector(char **tokens, int n_tokens, vec_t *out) {
    if (n_tokens < MINIVEC_DIM) return -1;   /* token 不够 DIM 个 */
    for (int i = 0; i < MINIVEC_DIM; i++) {
        out[i] = strtof(tokens[i], NULL);
    }
    return 0;
}

/* ============================================================================
 * minivec_handle_command —— 照搬 kvstore_request + kvstore_parse_protocol
 * 流程：切 token → 命令名→枚举 → 大 switch 分发 → 把响应写进 out。
 * ============================================================================ */
int minivec_handle_command(minivec_db_t *db, char *line, char *out, int outlen) {
    if (db == NULL || line == NULL || out == NULL) return -1;

    out[0] = '\0';

    /* 1. 切 token（对应 kvstore_request 里的 spilt_token） */
    char *tokens[MV_MAX_TOKENS] = {0};
    int n = split_token(line, tokens);
    if (n <= 0) {
        snprintf(out, outlen, "ERR empty");
        return -1;
    }

    /* 2. 命令名 → 枚举（对应 kvstore_command_index） */
    int cmd = minivec_command_index(tokens[0]);
    if (cmd == MV_CMD_COUNT) {
        snprintf(out, outlen, "ERR unknown command");   /* 对应 kvstore "UNKNOWN COMMAND" */
        return -1;
    }

    /* 3. 大 switch 分发（对应 kvstore_parse_protocol 的 switch） */
    switch (cmd) {

    /* VADD <id> <v1..vDIM>  —— 对应 kvstore SET：插入一条记录
     * 改动：value 从字符串变成向量；插入后若有 index 则同步建图。 */
    case MV_CMD_VADD: {
        if (n < 2 + MINIVEC_DIM) {                       /* VADD + id + DIM 个分量 */
            snprintf(out, outlen, "ERR need id and %d floats", MINIVEC_DIM);
            return -1;
        }
        uint64_t id = strtoull(tokens[1], NULL, 10);
        vec_t vec[MINIVEC_DIM];
        if (parse_vector(&tokens[2], n - 2, vec) != 0) {
            snprintf(out, outlen, "ERR bad vector");
            return -1;
        }
        db_write_lock(db);                               /* G3:写命令 */
        int rc = vstore_add(db->store, id, vec, NULL);   /* 对应 kvstore_array_set */
        if (rc == 0 && db->index) {
            hnsw_insert(db->index, id, vec);             /* 有索引就同步插入（建图） */
        }
        db_unlock(db);
        snprintf(out, outlen, rc == 0 ? "OK" : "ERR add failed");
        break;
    }

    /* VSEARCH <topk> <v1..vDIM>  —— [改3] 对应 kvstore GET，但语义升级：
     * GET 精确取 1 个 value；VSEARCH 按相似度取最像的 topK 个（多行 "id score"）。 */
    case MV_CMD_VSEARCH: {
        if (n < 2 + MINIVEC_DIM) {
            snprintf(out, outlen, "ERR need topk and %d floats", MINIVEC_DIM);
            return -1;
        }
        int topk = atoi(tokens[1]);
        if (topk <= 0) topk = 1;
        if (topk > MV_MAX_TOPK) topk = MV_MAX_TOPK;

        vec_t query[MINIVEC_DIM];
        if (parse_vector(&tokens[2], n - 2, query) != 0) {
            snprintf(out, outlen, "ERR bad vector");
            return -1;
        }
        /* 余弦度量下查询向量也要归一化（vec_normalize 现为桩，实现 distance 后生效） */
        if (db->metric == METRIC_COSINE) {
            vec_normalize(query, MINIVEC_DIM);
        }

        /* 有 HNSW 用 HNSW，否则走暴力。两者对外接口一致——可替换索引。
         * 注意：flat_search / hnsw_search 现在是桩，会返回 0 条，等你实现。 */
        search_result_t results[MV_MAX_TOPK];
        db_read_lock(db);                                /* G3:读命令,可并发 */
        int k = db->index
                  ? hnsw_search(db->index, query, topk, results)
                  : flat_search(db->store, query, topk, db->metric, results);
        db_unlock(db);   /* 结果已拷进本地 results[],解锁后再格式化 */

        /* 把结果拼成多行 "id score"（对应 kvstore_write_get_response，但变多行） */
        int off = 0;
        if (k <= 0) {
            off += snprintf(out + off, outlen - off, "EMPTY");
        }
        for (int i = 0; i < k && off < outlen - 1; i++) {
            off += snprintf(out + off, outlen - off, "%llu %.6f\n",
                            (unsigned long long)results[i].id, results[i].score);
        }
        break;
    }

    /* VDEL <id>  —— 对应 kvstore DEL */
    case MV_CMD_VDEL: {
        if (n < 2) { snprintf(out, outlen, "ERR need id"); return -1; }
        uint64_t id = strtoull(tokens[1], NULL, 10);
        db_write_lock(db);                               /* G3:写命令 */
        int rc = vstore_del(db->store, id);              /* 对应 kvstore_array_del */
        if (rc == 0 && db->index) {
            hnsw_delete(db->index, id);                  /* G1:同步从图里软删(修 §3.3 bug) */
        }
        db_unlock(db);
        snprintf(out, outlen, rc == 0 ? "OK" : "ERR not exist");
        break;
    }

    /* VCOUNT  —— MiniVec 新增的便捷命令：返回当前向量数 */
    case MV_CMD_VCOUNT: {
        db_read_lock(db);                                /* G3:读命令 */
        size_t cnt_now = vstore_count(db->store);
        db_unlock(db);
        snprintf(out, outlen, "%zu", cnt_now);
        break;
    }

    /* SAVE <path>  —— 把库落盘(只存裸向量,见 engine/persist.c) */
    case MV_CMD_SAVE: {
        if (n < 2) { snprintf(out, outlen, "ERR need path"); return -1; }
        db_read_lock(db);                                /* G3:SAVE 只读库 */
        int rc = minivec_save(db->store, tokens[1]);
        db_unlock(db);
        if (rc < 0) snprintf(out, outlen, "ERR save failed");
        else        snprintf(out, outlen, "OK %d saved", rc);
        break;
    }

    /* LOAD <path>  —— 从盘加载并重建 HNSW(追加到当前库) */
    case MV_CMD_LOAD: {
        if (n < 2) { snprintf(out, outlen, "ERR need path"); return -1; }
        db_write_lock(db);                               /* G3:LOAD 改库 */
        int rc = minivec_load(db->store, db->index, tokens[1]);
        db_unlock(db);
        if (rc == -2)      snprintf(out, outlen, "ERR dim mismatch");
        else if (rc < 0)   snprintf(out, outlen, "ERR load failed");
        else               snprintf(out, outlen, "OK %d loaded", rc);
        break;
    }

    default:
        snprintf(out, outlen, "ERR unknown command");
        return -1;
    }

    return 0;
}
