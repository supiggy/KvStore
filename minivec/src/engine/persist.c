#include "engine/persist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 文件头魔数 "MVEC" 和版本 */
#define MV_MAGIC   0x4D564543u
#define MV_VERSION 1u

/* minivec_save 流程树
 * ----------------------------------------------------------
 *   ├─ 打开 path.tmp
 *   ├─ 数一遍 live 条数(跳墓碑 vec==NULL)
 *   ├─ 写头:magic, version, dim, count
 *   ├─ for 每条 live:  写 id, dim×float, meta_len, meta
 *   ├─ fclose
 *   └─ rename(tmp -> path)   原子替换,写一半崩了也不会污染旧文件
 */
int minivec_save(const vector_store_t *store, const char *path) {
    if (store == NULL || path == NULL) return -1;

    char tmp[1100];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) return -1;

    uint32_t magic = MV_MAGIC, ver = MV_VERSION, dim = MINIVEC_DIM, live = 0;

    /* 先数 live 条数(墓碑不算) */
    for (size_t i = 0; i < vstore_count(store); i++) {
        const vec_item_t *it = vstore_at(store, i);
        if (it && it->vec) live++;
    }

    fwrite(&magic, 4, 1, f);
    fwrite(&ver,   4, 1, f);
    fwrite(&dim,   4, 1, f);
    fwrite(&live,  4, 1, f);

    for (size_t i = 0; i < vstore_count(store); i++) {
        const vec_item_t *it = vstore_at(store, i);
        if (it == NULL || it->vec == NULL) continue;          /* 跳墓碑 */
        fwrite(&it->id, sizeof(uint64_t), 1, f);
        fwrite(it->vec, sizeof(vec_t), dim, f);
        uint32_t mlen = it->meta ? (uint32_t)strlen(it->meta) : 0;
        fwrite(&mlen, 4, 1, f);
        if (mlen) fwrite(it->meta, 1, mlen, f);
    }

    if (fclose(f) != 0) return -1;
    if (rename(tmp, path) != 0) { remove(tmp); return -1; }   /* POSIX 下原子覆盖 */
    return (int)live;
}

/* minivec_load 流程树
 * ----------------------------------------------------------
 *   ├─ 打开 path,读头,校验 magic / dim
 *   └─ for count 条:读 id, dim×float, meta
 *         vstore_add(store, id, vec, meta)
 *         if index: hnsw_insert(index, id, vec)   ← 顺便重建图
 */
int minivec_load(vector_store_t *store, hnsw_index_t *index, const char *path) {
    if (store == NULL || path == NULL) return -1;

    FILE *f = fopen(path, "rb");
    if (f == NULL) return -1;

    uint32_t magic = 0, ver = 0, dim = 0, count = 0;
    if (fread(&magic, 4, 1, f) != 1 || magic != MV_MAGIC) { fclose(f); return -1; }
    if (fread(&ver,   4, 1, f) != 1) { fclose(f); return -1; }
    if (fread(&dim,   4, 1, f) != 1) { fclose(f); return -1; }
    if (fread(&count, 4, 1, f) != 1) { fclose(f); return -1; }
    if (dim != (uint32_t)MINIVEC_DIM) { fclose(f); return -2; }   /* 维度不匹配 */

    vec_t vec[MINIVEC_DIM];
    int loaded = 0;

    for (uint32_t i = 0; i < count; i++) {
        uint64_t id;
        if (fread(&id, sizeof(uint64_t), 1, f) != 1) break;
        if (fread(vec, sizeof(vec_t), dim, f) != dim) break;

        uint32_t mlen = 0;
        if (fread(&mlen, 4, 1, f) != 1) break;
        char *meta = NULL;
        if (mlen) {
            meta = (char *)malloc((size_t)mlen + 1);
            if (meta && fread(meta, 1, mlen, f) == mlen) meta[mlen] = '\0';
            else { free(meta); meta = NULL; }
        }

        if (vstore_add(store, id, vec, meta) == 0) {
            if (index) hnsw_insert(index, id, vec);
            loaded++;
        }
        free(meta);
    }

    fclose(f);
    return loaded;
}
