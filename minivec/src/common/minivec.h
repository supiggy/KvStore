#ifndef MINIVEC_H
#define MINIVEC_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* ============================================================
 * 全局配置
 * ============================================================ */

#define MINIVEC_DIM          128      /* 向量维度，全库统一。真实 embedding 常见 768/1536，
                                         学习阶段用 128 足够，调试快。 */
#define MINIVEC_PORT         9097     /* 服务监听端口 */
#define MINIVEC_MAX_ELEMENTS 1000000  /* 最大向量数 */
#define MINIVEC_BUFFER_LEN   8192     /* 单条请求/响应缓冲区上限 */

/* ============================================================
 * 共享类型
 * ============================================================ */

typedef float vec_t;   /* 向量元素类型。日后做量化时这里会出现 int8 版本。 */

/* 相似度 / 距离度量 */
typedef enum {
    METRIC_COSINE = 0,   /* 余弦相似度：越大越相似（向量归一化后等价于内积） */
    METRIC_DOT,          /* 内积：越大越相似 */
    METRIC_L2,           /* 欧氏距离平方：越小越相似 */
} metric_t;

/* 库里存的一条向量记录 */
typedef struct {
    uint64_t  id;
    vec_t    *vec;    /* 指向 MINIVEC_DIM 个 float；COSINE 度量下入库时已归一化 */
    char     *meta;   /* 可选：原始文本，可为 NULL */
} vec_item_t;

/* 一条检索命中结果 */
typedef struct {
    uint64_t id;
    float    score;   /* 相似度或距离，含义取决于 metric_t */
} search_result_t;

/* ============================================================
 * 内存小工具（基础设施，非"流程逻辑"，已实现）
 * ============================================================ */

static inline void *mv_malloc(size_t size) {
    void *p = malloc(size);
    if (p == NULL) {
        fprintf(stderr, "[minivec] malloc(%zu) failed\n", size);
    }
    return p;
}

static inline void *mv_calloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p == NULL) {
        fprintf(stderr, "[minivec] calloc(%zu, %zu) failed\n", n, size);
    }
    return p;
}

static inline void mv_free(void *p) {
    if (p != NULL) free(p);
}

#endif /* MINIVEC_H */
