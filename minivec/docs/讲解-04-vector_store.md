# 讲解 04 · vector_store(向量存储层)

> 模块定位:**只管"存",不管"搜"**。拥有向量内存、维护 `id → 记录` 映射、提供遍历。
> 数据结构:定长数组 + 线性扫描(照搬 kvstore 的 array 引擎,最朴素那个)。
> 难度 ★★☆。涉及"数组 / 映射"知识点 → 配套 LeetCode 示例 `leetcode-706-hashmap.c`。

---

## 0. 它在整张图里的位置

```text
parser ──► vector_store(存)      ←── 你在这
              │
              ├──► index_flat(搜:遍历它)
              └──► index_hnsw(搜:建图引用它)
```
**存储和检索分家**:同一份 `vector_store`,上面挂"暴力"或"HNSW"两种检索。
所以本模块只提供 增/删/查/遍历,不提供"找最相似"。

---

## 1. 内部结构

```c
struct vector_store {
    int         dim;        // 向量维度,全库统一
    int         capacity;   // 最多存多少条
    metric_t    metric;     // 度量(COSINE 时入库要归一化)
    vec_item_t *items;      // 定长数组,items[slot] 是第 slot 条
    size_t      count;      // 已用物理槽位数(含墓碑)
};
// vec_item_t { uint64_t id; vec_t *vec; char *meta; }
```
- `key` = `id`(uint64,用 `==` 比较);`value` = `vec`(一段 float)。
- 对照 kvstore:`kvs_array_item{key,value}` → `vec_item_t{id,vec,meta}`,key 字符串变 id 整数,value 字符串变 float 数组。

---

## 2. 逐函数流程树

### 2.1 vstore_add —— 对应 kvstore_array_set
```text
vstore_add(s, id, vec, meta)
  ├─ find_slot(id) >= 0 ?  → 已存在,return 1(拒绝重复)
  ├─ count >= capacity ?   → 满,return -1
  ├─ slot = count           追加到末尾(最简单的找空位)
  ├─ copy = malloc(dim*float); memcpy(vec)      ★ 拷贝一份(库自己拥有)
  ├─ if metric==COSINE: vec_normalize(copy)     ★ 入库归一化
  ├─ items[slot] = { id, copy, dup(meta) }
  ├─ count++
  └─ return 0
```
**两个关键点**:
- **拷贝**:不能只存调用方传来的指针(那是临时的,会悬空),必须 malloc 一份自己的。
- **归一化**:COSINE 下入库就归一化,之后检索用内积即可(见 `讲解-01`)。

### 2.2 find_slot(内部)—— 对应 kvstore_array_get 的扫描
```text
find_slot(s, id)
  └─ for i in [0, count):
        if items[i].vec != NULL && items[i].id == id: return i   // 跳墓碑 + 比 id
     return -1
```
线性扫描 O(N)。和 kvstore array 引擎一样朴素。

### 2.3 vstore_get
```text
vstore_get(s, id)
  ├─ slot = find_slot(id)
  └─ return slot<0 ? NULL : &items[slot]
```

### 2.4 vstore_del —— 墓碑删除
```text
vstore_del(s, id)
  ├─ slot = find_slot(id);  找不到 return -1
  ├─ free(items[slot].vec); free(items[slot].meta)
  ├─ items[slot].vec = NULL      ★ 墓碑标记:这个槽位作废
  └─ return 0
```
**为什么用墓碑而不真删**:物理删除要搬移数组、还要顾及 HNSW 图连通性,太麻烦。
起步标记 `vec=NULL`,遍历时跳过即可。count 不变。

### 2.5 vstore_at —— 给检索层遍历用
```text
vstore_at(s, idx)
  └─ idx<count ? &items[idx] : NULL     // 墓碑位 .vec==NULL,调用方自行跳过
```
kvstore array 引擎没有这个口子;MiniVec 加它,是因为检索层(flat/hnsw)要遍历全库。

---

## 3. 坑清单

| 坑 | 说明 |
|----|------|
| 只存指针不拷贝 | 调用方的 vec 是临时栈数组,存指针会悬空 → 必须 memcpy |
| 忘了归一化 | COSINE 下入库不归一化,检索用内积就 ≠ 余弦 |
| 遍历不跳墓碑 | `vec==NULL` 不跳 → 对空指针算距离崩溃 |
| 用 strdup | 非 C11 标准,某些环境没有 → 用自己的 dup_str(malloc+memcpy) |

---

## 4. 性能 & 升级路线(知识点所在)

现在 `find_slot` 是**线性扫描 O(N)**,和 kvstore array 引擎一样慢。
**升级:把 `id → slot` 换成哈希表,查/删降到 O(1)。** 这正是 kvstore 的 hash 引擎做的事。

> 配套 LeetCode 示例 `leetcode-706-hashmap.c`(设计哈希表)演示了怎么从零实现这个
> `id → slot` 的 O(1) 映射 —— 看懂它,就知道怎么把 vector_store 提速。

---

## 5. 一句话背诵 & 面试

> **背诵**:vector_store 只管存:定长数组 + `id→slot` 线性扫描(像 kvstore array 引擎)。add 拷贝向量(COSINE 归一化)、追加末尾;del 用墓碑标记(vec=NULL);检索层靠 vstore_at 遍历跳墓碑。想提速就把线性扫描换成哈希表。

**Q:为什么存储要和检索分开成两个模块?**
> 因为相似度检索策略多(暴力、HNSW、IVF…)且复杂,不像精确匹配能塞进存储里。把"存"独立成 vector_store,"搜"做成可替换的 index,同一份数据能挂不同索引,职责清晰。

**Q:删除为什么用墓碑标记?**
> 物理删除要搬移数组、还会破坏 HNSW 图的连通性,代价大。墓碑(把 vec 置 NULL)是 O(1) 的,遍历时跳过即可;积累多了再做一次性的 compaction/重建。
