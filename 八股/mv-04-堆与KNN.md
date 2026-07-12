# 数据结构知识点 · 堆(Heap)与 KNN topK

本文件配合 `minivec/docs/讲解-02-index_flat.md` 使用:
先讲清通用堆(你贴的那份 C++ `Heap` 类)的流程树,再给出 **KNN 专用的大/小顶堆代码**(C / C++),
C 版可直接拿去写 `index_flat.c` 的 topK。

---

## 1. 堆是什么(一句话)

> 堆 = 用**数组**存的一棵**完全二叉树**,满足"父节点优先级 ≥ 两个孩子"。
> 堆顶(`data[0]`)永远是优先级最高的那个。

不用真的建树,**下标就能算父子**:

```text
当前节点 i
  父节点   = (i - 1) / 2
  左孩子   = 2*i + 1
  右孩子   = 2*i + 2
```

方向由比较函数 `cmp` 决定:
```text
小顶堆:cmp(a,b) = a < b   → 数值小的优先级高 → 堆顶是最小值
大顶堆:cmp(a,b) = a > b   → 数值大的优先级高 → 堆顶是最大值
```

---

## 2. 通用堆的流程树(对应你贴的 `Heap` 类)

### 2.1 push(插入)
```text
push(x)
  ├─ data.push_back(x)        放到数组末尾(= 完全二叉树最后一个位置)
  └─ siftUp(size-1)           从末尾向上调整
时间复杂度 O(log n)
```

### 2.2 siftUp(向上调整 / 上浮)
```text
siftUp(i)
  └─ while i > 0:
        parent = (i-1)/2
        if cmp(data[i], data[parent]):     当前比父"优先级更高"
            swap(data[i], data[parent])
            i = parent                      继续往上比
        else:
            break                           已满足堆性质,停
```
直觉:新元素从底部"冒泡"上浮,直到父节点比它更优先为止。

### 2.3 pop(弹出堆顶)
```text
pop()
  ├─ if empty: throw
  ├─ topValue = data[0]        记下堆顶(要返回的)
  ├─ data[0]  = data.back()    用最后一个元素覆盖堆顶
  ├─ data.pop_back()           删掉最后一个
  ├─ if !empty: siftDown(0)    从堆顶向下调整
  └─ return topValue
时间复杂度 O(log n)
```
关键技巧:**不是直接删堆顶**(那会留个洞),而是"末尾补到堆顶,再下沉"。

### 2.4 siftDown(向下调整 / 下沉)
```text
siftDown(i)
  └─ while true:
        left = 2i+1; right = 2i+2; best = i
        if left  < n && cmp(data[left],  data[best]): best = left
        if right < n && cmp(data[right], data[best]): best = right
        if best == i: break                 当前已比两孩子都优先,停
        swap(data[i], data[best])
        i = best                            继续往下沉
```
直觉:补上来的元素从顶部"下沉",每次和更优先的那个孩子交换,直到比两孩子都优先。

### 2.5 复杂度小结
| 操作 | 复杂度 | 原因 |
|------|--------|------|
| push / pop | O(log n) | 树高 = log n,最多走一条根到叶的路 |
| top | O(1) | 就是 `data[0]` |

---

## 3. 堆方向与 KNN 的关系(最容易绕晕,务必记住)

KNN 要"找最相似的 K 个"。把相似度统一成 **score 越大越相似**(见 `minivec/docs/讲解-02-index_flat.md` 的"度量方向统一")。

> **找最大的 K 个 → 用【小顶堆】。**(反直觉但正确)

为什么?

```text
小顶堆堆顶 = 这 K 个里 score 最小的那个 = "门槛"
新候选来了:
  ├─ 堆没满 K 个   → 直接放进去
  └─ 堆满了        → score > 堆顶(门槛) 才有资格:踢掉堆顶,放进新的
扫完全库,堆里留下的就是 score 最大的 K 个
```

一句话:**堆顶守着"当前最差的合格者"当门槛,新人打得过门槛才能进,门槛随之抬高。**

(反过来:若用 L2 且不取负,"越小越相似",那就是"找最小的 K 个 → 用大顶堆"。所以才建议统一成 score 越大越优,只用小顶堆一套逻辑。)

---

## 4. KNN 大小堆代码

### 4.1 C++ 版(idiomatic,基于 `std::priority_queue`)

```cpp
#include <queue>
#include <vector>
#include <algorithm>
#include <cstdint>
using namespace std;

struct Result { uint64_t id; float score; };   // score 越大越相似

// 让 priority_queue 变成"小顶堆(score 最小在堆顶)"。
// priority_queue 默认大顶堆,比较器返回 a>b 即可反转成小顶堆。
struct MinByScore {
    bool operator()(const Result& a, const Result& b) const {
        return a.score > b.score;
    }
};

// 从 all 里挑出 score 最大的 K 个,按 score 从大到小返回。
vector<Result> topk(const vector<Result>& all, int K) {
    priority_queue<Result, vector<Result>, MinByScore> heap;  // 小顶堆,堆顶=门槛
    for (const auto& r : all) {
        if ((int)heap.size() < K) {
            heap.push(r);
        } else if (r.score > heap.top().score) {   // 比门槛大才进
            heap.pop();
            heap.push(r);
        }
    }
    vector<Result> out;
    while (!heap.empty()) { out.push_back(heap.top()); heap.pop(); }  // 取出是从小到大
    reverse(out.begin(), out.end());                                  // 反转成从大到小
    return out;
}
```

> 如果想用你自己写的那个泛型 `Heap` 类:把 `int` 换成 `Result`,
> 构造时传 `cmp = [](const Result&a,const Result&b){ return a.score < b.score; }`(小顶堆),
> 然后用同样的"未满就 push、满了比堆顶大就 pop+push"逻辑即可。

### 4.2 C 版(★ 直接可用于 MiniVec 的 `index_flat.c`)

存的是项目里的 `search_result_t {uint64_t id; float score;}`(定义在 `common/minivec.h`)。
这是一个**固定容量 K 的小顶堆**,专门服务 topK。

```c
#include "common/minivec.h"   /* search_result_t */

static void sr_swap(search_result_t *a, search_result_t *b) {
    search_result_t t = *a; *a = *b; *b = t;
}

/* 上浮:小顶堆,比父小就往上 */
static void heap_siftup(search_result_t *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h[i].score < h[parent].score) {
            sr_swap(&h[i], &h[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

/* 下沉:小顶堆,和更小的孩子交换 */
static void heap_siftdown(search_result_t *h, int n, int i) {
    while (1) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < n && h[l].score < h[smallest].score) smallest = l;
        if (r < n && h[r].score < h[smallest].score) smallest = r;
        if (smallest == i) break;
        sr_swap(&h[i], &h[smallest]);
        i = smallest;
    }
}

/* 尝试把一个候选 (id, score) 塞进"容量 K 的小顶堆"。
 * h:    容量 >= K 的数组
 * size: 当前元素数(传地址,会被更新)
 * K:    目标个数 */
static void topk_push(search_result_t *h, int *size, int K,
                      uint64_t id, float score) {
    if (*size < K) {                       /* 没满:直接放末尾再上浮 */
        h[*size].id = id;
        h[*size].score = score;
        heap_siftup(h, *size);
        (*size)++;
    } else if (K > 0 && score > h[0].score) {  /* 满了:比门槛大才替换堆顶 */
        h[0].id = id;
        h[0].score = score;
        heap_siftdown(h, *size, 0);
    }
}

/* 输出时按 score 从大到小排序用 */
static int cmp_desc(const void *a, const void *b) {
    float sa = ((const search_result_t *)a)->score;
    float sb = ((const search_result_t *)b)->score;
    if (sa < sb) return 1;
    if (sa > sb) return -1;
    return 0;
}
```

---

## 5. 在 `flat_search` 里怎么用(骨架,逻辑留给你填)

把上面的堆当"工具",`flat_search` 的主体逻辑(遍历、跳墓碑、度量方向)还是你来写:

```c
int flat_search(const vector_store_t *s, const vec_t *query, int topk,
                metric_t metric, search_result_t *out) {
    int size = 0;
    /* out 本身就能当堆数组用(容量 = 调用方给的 topk) */

    for (size_t i = 0; i < vstore_count(s); i++) {
        const vec_item_t *it = vstore_at(s, i);
        if (it->vec == NULL) continue;             /* 跳过墓碑 */

        /* TODO: 按 metric 算出"越大越相似"的 score
         *   COSINE/DOT: score = dist_dot(query, it->vec, DIM)
         *   L2        : score = -dist_l2sq(query, it->vec, DIM)   ← 取负统一方向 */
        float score = /* ... 你来写 ... */ 0;

        topk_push(out, &size, topk, it->id, score);
    }

    qsort(out, size, sizeof(search_result_t), cmp_desc);  /* 从大到小 */
    return size;   /* 实际个数,可能 < topk */
}
```

> 堆是通用工具,我给你了;**"算 score + 方向统一 + 串起来"才是你这个模块要练的判断**。

---

## 6. 一句话背诵 & 面试

> **背诵**:堆是数组存的完全二叉树,父 `(i-1)/2`、左 `2i+1`、右 `2i+2`;push 末尾入再 siftUp,pop 末尾补顶再 siftDown,都是 O(log n)。**找最大 K 个用小顶堆**,堆顶是门槛,新人打得过门槛才进。

**Q:从海量数据里取最大的 K 个,为什么用小顶堆而不是大顶堆?**
> 维护一个大小为 K 的小顶堆,堆顶是这 K 个里最小的,相当于"入选门槛"。每来一个数,只要比堆顶大就替换堆顶并下沉,否则丢弃。扫完保留的就是最大的 K 个。整体 O(N log K),优于全排序 O(N log N),且只用 O(K) 额外空间。

**Q:pop 为什么要"末尾补到堆顶再下沉",不能直接删堆顶?**
> 直接删堆顶会在根部留下空洞、破坏完全二叉树结构。把最后一个元素补到堆顶,数组仍是完全二叉树,再用 siftDown 把它沉到正确位置,O(log n) 就恢复了堆性质。

---

## 7. 附录:`std::priority_queue` 用法速查

`priority_queue` 是 STL 封装好的堆(C++ only,C 没有)。底层也是 siftUp/siftDown,你不用自己写。

### 7.1 头文件 & 声明
```cpp
#include <queue>
using namespace std;

priority_queue<int> pq;                                // 默认:大顶堆(堆顶最大!)
priority_queue<int, vector<int>, greater<int>> minpq;  // 小顶堆(堆顶最小)
```

### 7.2 三个核心操作
```cpp
pq.push(x);     // 入堆        O(log n)
pq.top();       // 看堆顶,不删  O(1)
pq.pop();       // 删堆顶,返回 void —— 不给你值!  O(log n)
```
⚠️ **`pop()` 不返回值**。要拿堆顶值,先 `top()` 再 `pop()`:
```cpp
int x = pq.top();   // 先拿值
pq.pop();           // 再删除
```

### 7.3 其它
```cpp
pq.empty();   // 是否空
pq.size();    // 元素个数
```
注意:priority_queue **没有迭代器/不能遍历**,只能从堆顶一个个 pop 出来。

### 7.4 存自定义类型 + 自定义比较器(仿函数,非 lambda)
```cpp
struct Result { uint64_t id; float score; };

struct MinByScore {                       // score 小的在堆顶 → 小顶堆
    bool operator()(const Result& a, const Result& b) const {
        return a.score > b.score;         // greater 风格
    }
};
priority_queue<Result, vector<Result>, MinByScore> heap;   // KNN topK 用的就是这个
```

### 7.5 完整例子:int 小顶堆
```cpp
priority_queue<int, vector<int>, greater<int>> pq;
pq.push(5); pq.push(1); pq.push(9);
while (!pq.empty()) {
    cout << pq.top() << " ";   // 输出: 1 5 9 (从小到大)
    pq.pop();
}
```

### 7.6 和手写 Heap 的 API 对照
| 操作 | 手写 Heap | priority_queue |
|------|-----------|----------------|
| 入堆 | `push(x)` | `push(x)` |
| 看顶 | `top()` | `top()` |
| 弹顶 | `pop()` 返回值 | `pop()` 返回 **void** |
| 判空 | `empty()` | `empty()` |
| 个数 | `size()` | `size()` |
| 指定方向 | 构造传 `cmp` | 模板参 `Compare` |
| 默认方向 | 由你传的 cmp 定 | **大顶堆** |

### 7.7 三个必记的坑
1. **默认是大顶堆**;小顶堆要传 `greater<T>` 或反向仿函数。
2. **`pop()` 不返回值**;先 `top()` 拿值再 `pop()`。
3. **比较器约定和手写 Heap 相反**(见第 2/3 节):手写 `a<b`→小顶堆,priority_queue `a<b`(less,默认)→大顶堆。
