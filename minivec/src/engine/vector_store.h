#ifndef MINIVEC_VECTOR_STORE_H
#define MINIVEC_VECTOR_STORE_H

#include "common/minivec.h"

/* 不透明类型：外部只持有指针，内部布局藏在 .c 里 */
typedef struct vector_store vector_store_t;

/* 创建 / 销毁 */
vector_store_t *vstore_create(int dim, int capacity, metric_t metric);
void            vstore_destroy(vector_store_t *s);

/* 增：拷贝一份向量进库。COSINE 度量下内部会归一化。
 * 返回 0 成功，<0 失败（满 / 重复 id 视实现而定）。 */
int vstore_add(vector_store_t *s, uint64_t id, const vec_t *vec, const char *meta);

/* 查：按 id 取记录，不存在返回 NULL。返回的指针只读、不可 free。 */
const vec_item_t *vstore_get(vector_store_t *s, uint64_t id);

/* 删：返回 0 成功，<0 不存在。起步阶段可先做"标记删除"。 */
int vstore_del(vector_store_t *s, uint64_t id);

/* 当前有效向量数 */
size_t vstore_count(const vector_store_t *s);

/* 顺序访问（暴力检索要遍历全库时用）。idx 越界返回 NULL。 */
const vec_item_t *vstore_at(const vector_store_t *s, size_t idx);

#endif /* MINIVEC_VECTOR_STORE_H */
