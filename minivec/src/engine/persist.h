#ifndef MINIVEC_PERSIST_H
#define MINIVEC_PERSIST_H

#include "common/minivec.h"
#include "engine/vector_store.h"
#include "engine/index_hnsw.h"

/* ============================================================
 * 持久化:把库落盘 / 从盘加载。
 * 策略:只存"裸向量(id + vec + meta)",不存 HNSW 图;
 *       加载时重新插入,顺便重建 HNSW(简单健壮,代价是加载要重建图)。
 * 文件格式:magic+version+dim+count 头,后接 count 条 (id, dim×float, meta)。
 * ============================================================ */

/* 存盘:遍历 store 写出所有 live 记录(跳墓碑)。先写 tmp 再 rename(原子)。
 * 返回写出的条数,<0 失败。 */
int minivec_save(const vector_store_t *store, const char *path);

/* 加载:读文件,逐条 vstore_add + (若 index 非空)hnsw_insert。
 * 返回成功加载的条数;-1 打不开/格式错,-2 维度不匹配。 */
int minivec_load(vector_store_t *store, hnsw_index_t *index, const char *path);

#endif /* MINIVEC_PERSIST_H */
