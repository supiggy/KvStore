# 讲解 05 · topk_heap(topK 小顶堆工具)

> 模块定位:通用工具。按 score 维护"当前最优的 K 个",供 index_flat / index_hnsw 取 topK。
> 数据结构:小顶堆(数组存完全二叉树)。难度 ★★☆。
>
> 本文是**模块级速览**;完整流程树 + 执行 trace 见 `dui-流程树.md`,
> 堆原理 + priority_queue 对照见 `数据结构知识点.md`,LeetCode 示例见 `pq.c`(LC347)。

---

## 1. 为什么单独成模块

`index_flat` 和 `index_hnsw` 都要"从一堆候选里取 score 最大的 K 个"。
这是通用能力,抽成 `engine/topk_heap.{h,c}`,两边 include 共用(DRY)。

```text
index_flat ─┐
            ├──► topk_heap(topk_push / topk_cmp_desc)
index_hnsw ─┘   (HNSW 内部另有自己的 cand 堆,但对外 topK 用这套)
```

## 2. 对外 API(topk_heap.h)

```c
// 把一个候选 (id, score) 塞进容量 K 的小顶堆。
// heap: 容量>=K 的数组;  size: 当前元素数(传地址,初值0);  K: 目标个数
void topk_push(search_result_t *heap, int *size, int K, uint64_t id, float score);

// qsort 比较器:按 score 从大到小。输出前用。
int  topk_cmp_desc(const void *a, const void *b);
```
约定:**堆数组和 size 由调用方持有**,本模块不分配内存,只提供"塞一个"和"排序"。

## 3. 核心:为什么"找最大 K 个"用小顶堆

```text
小顶堆堆顶 heap[0] = 当前 K 个里 score 最小的 = 门槛
topk_push:
  ├─ size < K        → 直接放(末尾入,siftUp 上浮)
  └─ size == K 且 score > 门槛 → 覆盖堆顶,siftDown 下沉
扫完,堆里就是 score 最大的 K 个
```
复杂度 O(N log K),空间 O(K)。详细 trace 见 `dui-流程树.md` 第 4 节。

## 4. 内部函数流程树(简版)

```text
topk_push      ── 主入口,两分支(见上)
  ├─ heap_siftup(h,i)    while i>0: 比父小就 swap 上浮
  └─ heap_siftdown(h,n,i) while: 和更小的孩子 swap 下沉(l<n,r<n 边界保护)
sr_swap        ── 交换两个 search_result_t
topk_cmp_desc  ── qsort:score 大的排前
```
> 下标:父 `(i-1)/2`、左 `2i+1`、右 `2i+2`。

## 5. 谁在用它

```c
// index_flat.c
topk_push(out, &size, topk, it->id, score);
qsort(out, size, sizeof(search_result_t), topk_cmp_desc);
```
注意:`out`(调用方给的结果数组)**直接当堆用**,省一块内存。

## 6. 一句话背诵

> topK 工具:小顶堆,堆顶守门槛(K 个里最小)。没满就 siftUp 入,满了 score 超门槛就覆盖堆顶 siftDown。O(N log K),把 out 数组当堆用。比较器 topk_cmp_desc 最后按 score 降序排好再输出。
