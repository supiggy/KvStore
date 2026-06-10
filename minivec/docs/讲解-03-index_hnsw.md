# 讲解 03 · index_hnsw(HNSW 近邻索引)

> 模块定位:本项目的招牌菜,也是最难的部分。用分层图把检索从 O(N) 降到 ≈O(log N)。
> 难度 ★★★。对应面试知识分支 4:HNSW 深入(会被往死里问)。
>
> 本文只讲"怎么理解 + 流程树 + 关键决策",**不给完整 C 代码**——留给你自己实现。

---

## 0. 一句话抓住本质:HNSW 就是跳表的升维

你刚在 kvstore 写过跳表。把跳表彻底理解了,HNSW 就理解了一半。

```text
跳表:  分层【链表】,上层稀疏当快速通道,下层完整。查找时上层大步跳,接近了下沉精找。
HNSW:  分层【图】,  上层稀疏当快速通道,Layer0 含全部点。查找时上层大步跳,接近了下沉精找。
```

唯一区别:跳表每层是"有序链表"(适合一维大小比较),HNSW 每层是"图"(适合高维"谁离谁近")。
**"上层粗导航、下层精搜"这个核心思想,两者一模一样。**

```text
Layer2  A ─────────────────── F          点最少,长程边,大跳
        │                     │
Layer1  A ──── C ──────── F ──┴── J       中等
        │      │          │        │
Layer0  A-B-C-D-E-F-G-H-I-J-K              所有点,短程边,精搜
```

---

## 1. 三个参数(必背,面试必考)

| 参数 | 含义 | 调大的影响 | 典型值 |
|------|------|-----------|--------|
| `M` | 每个点每层连几个邻居 | 召回↑、内存↑、变慢 | 8~32(Layer0 常用 2M) |
| `ef_construction` | 建图时的搜索宽度(候选集大小) | 图质量↑、建图变慢 | 100~200 |
| `ef_search` | 查询时的搜索宽度 | 召回↑、查询变慢 | 运行时可调,50~200 |

`ef_search` 是**"快 vs 准"的运行时旋钮**——这是 HNSW 的精髓。你要能画出"ef_search 调大 → recall 升、延迟升"的曲线(简历素材)。

---

## 2. 数据结构(框架已给,理解它怎么用)

```text
struct hnsw_node {           // 一个向量节点
    id, vec, level           // level = 该点最高到第几层
    neighbors                // 扁平存所有层的邻居下标
    neighbor_count           // 每层当前邻居数
}
struct hnsw_index {
    params                   // M / ef_* / dim / metric
    nodes[], node_count      // 所有节点
    entry_point              // 入口点(最高层那个),空图为 -1
    max_level                // 当前图最高层
}
```

### 邻居内存怎么布局(你要想清楚的第一件事)
建议:每层最多 `maxM` 个邻居(Layer0 用 `2*M`,其它层用 `M`)。
```text
某点 level=2 → 它在 Layer0/1/2 都有邻居表
neighbors 大小 = (level+1) * maxM
第 L 层的邻居 = neighbors + L*maxM,数量 = neighbor_count[L]
```

---

## 3. 度量方向统一(动手前先定规矩,省掉无数 if)

COSINE/DOT 越大越近,L2 越小越近,方向相反,贯穿所有比较逻辑会很乱。
**强烈建议:内部统一成"distance 越小越近"**:
```text
L2     : distance = dist_l2sq           (本来越小越近)
COSINE : distance = -dist_dot(归一化后)  (取负,于是也越小越近)
DOT    : distance = -dist_dot           (取负)
```
这样下面所有"更近 / 更远"的比较都是同一个方向。最后输出 score 时再换回去。

---

## 4. `random_level` —— 给新点抽层级

### 流程树
```text
random_level()
  ├─ r = 均匀随机 (0, 1)
  ├─ level = floor( -ln(r) * mL ),   mL = 1/ln(M)
  └─ return level
```
### 讲解
让各层点数**指数衰减**:绝大多数点只在 Layer0,少数升到高层当"高速路入口"。
和跳表"抛硬币决定上几层"是同一招,只是用指数分布,数学上更优。
这是 5 个函数里最简单的,先把它写了。

---

## 5. `search_layer` —— HNSW 的心脏(最难,重点理解)

在**单独一层**里,从入口点出发,找出离 query 最近的 `ef` 个点。
插入和查询都靠它,写对它 HNSW 就成了一大半。

### 三个数据结构(理解它们的分工是关键)
```text
visited     已访问集合     →  避免重复扩展同一个点
candidates  最小堆         →  "待扩展"的点,离 query 最近的先扩展(贪心方向)
result      最大堆         →  "当前最优的 ef 个",堆顶是这 ef 个里最远的(守门槛)
```

### 流程树
```text
search_layer(query, entry_points, ef, layer)
  ├─ 把 entry_points 放入 visited、candidates、result
  │
  ├─ while candidates 非空:
  │     c = candidates 弹出【离 query 最近】的点
  │     if dist(c,query) > result 堆顶距离:
  │         break                       ← 剪枝:连最近的待扩展点都比已知最差的还远,停
  │     for each 邻居 n of c (在 layer 层):
  │         if n 未访问:
  │             visited.add(n)
  │             if result 未满 ef  或  dist(n,query) < result 堆顶距离:
  │                 candidates.push(n)
  │                 result.push(n)
  │                 if result 大小 > ef: result 弹出【最远】的
  │
  └─ return result 里的点
```

### 讲解(把这段读三遍)
- **candidates 用最小堆**:永远先扩展"目前看起来最近"的点 → 贪心地往 query 方向走。
- **result 用最大堆**:守住"目前最好的 ef 个",堆顶是其中最差的,当门槛。
- **那个 break 是性能关键**:当"最有希望的待扩展点"都比"已知最差的结果"还远,再找下去不可能更好,直接停。这就是它比暴力快的原因——**不扫全图,只扫 query 附近**。

### 实现提示
- 两个堆方向相反,容易写错。起步可以先用"排序数组"模拟堆把逻辑跑对,再换真堆提速。
- `visited` 数据量大时用 bitset / 数组标记 + "版本号"避免每次 memset。

---

## 6. `select_neighbors` —— 给新点挑 M 个邻居

### 简单版流程树
```text
select_neighbors_simple(candidates, M)
  └─ 从 candidates 里取【最近的 M 个】返回
```

### 启发式版(HNSW 论文 Algorithm 4,召回更高)
```text
select_neighbors_heuristic(base, candidates, M)
  ├─ 候选按"离 base 由近到远"排序
  ├─ selected = 空
  └─ for each 候选 c(由近到远):
        if selected 已满 M: break
        if c 离 base 比 c 离【任何已选邻居】都更近:
            selected.add(c)            ← 只保留"开辟新方向"的邻居
  return selected
```
### 讲解
简单版会让邻居都挤在同一个方向(一团),图不好导航。
启发式版让邻居**朝不同方向散开**,图更"四通八达",召回更高。
**起步先写简单版跑通,再写启发式版对比 recall 提升**——这个对比本身就是很好的简历点。

---

## 7. `hnsw_insert` —— 建图(两阶段)

### 流程树
```text
hnsw_insert(id, vec)
  ├─ l = random_level()                         新点最高层
  ├─ 建 node(分配邻居表,level=l)
  ├─ if 空图: entry_point=该点; max_level=l; return
  │
  ├─ ep = entry_point
  ├─ 【阶段A:粗定位】从顶层下降到 l+1 层,只挪动 ep,不连边
  │     for layer from max_level down to l+1:
  │         ep = search_layer(vec, {ep}, ef=1, layer) 的最近点
  │
  ├─ 【阶段B:逐层连边】从 min(max_level,l) 到 0
  │     for layer from min(max_level,l) down to 0:
  │         cand = search_layer(vec, {ep}, ef=ef_construction, layer)
  │         nbrs = select_neighbors(vec, cand, M)
  │         给新点在该层连上 nbrs
  │         for each n in nbrs:                  ★ 双向边
  │             给 n 也连上新点
  │             if n 的邻居超过 maxM: 用 select_neighbors 裁剪 n 的邻居
  │         ep = nbrs                            作为下一层入口
  │
  └─ if l > max_level: entry_point=新点; max_level=l
```

### 讲解
- **阶段 A** 只为把入口点 `ep` 从顶层"挪"到离新点近的地方,`ef=1` 走最省。
- **阶段 B** 才真正连边,`ef=ef_construction` 找一批好候选。
- **双向边**别忘:你连了 n,n 也要连你,否则查询时走不回来。
- 连完要**裁剪邻居**:n 的邻居数有上限,超了用 select_neighbors 留最好的 M 个。

---

## 8. `hnsw_search` —— 查询(两阶段)

### 流程树
```text
hnsw_search(query, topk, out)
  ├─ if 空图: return 0
  ├─ ep = entry_point
  ├─ 【阶段A:粗导航】顶层一路下降到第 1 层,ef=1
  │     for layer from max_level down to 1:
  │         ep = search_layer(query, {ep}, ef=1, layer) 的最近点
  │
  ├─ 【阶段B:精搜】Layer0 用大 ef
  │     cand = search_layer(query, {ep}, ef=max(ef_search, topk), layer=0)
  │
  ├─ 从 cand 取最近 topk 个,按相似度排序写 out[]
  └─ return 个数
```

### 讲解
和插入的阶段 A 同理:**上层快速逼近(ef=1),Layer0 精细搜索(大 ef)**。
这正是面试问"HNSW 查询过程"的标准答案:**顶层贪心下降做粗导航 + 底层 ef 候选集做精搜。**
查询时间 ≈ O(log N),log 来自分层结构,和跳表同源。

---

## 9. 已知难点 / 坑清单(提前知道别踩)

| 坑 | 说明 |
|----|------|
| 近似性 | HNSW 找的是近似最近邻 → **必须先有 index_flat 当标准答案**,否则对错难辨 |
| 度量方向不统一 | COSINE 越大越近、L2 越小越近,不统一会到处写反 → 内部统一成"距离越小越近" |
| 忘了双向边 | 只连单向,查询时回不去,召回暴跌 |
| 忘了裁剪邻居 | 邻居无上限增长 → 内存爆、变慢 |
| 两个堆方向写反 | candidates 最小堆、result 最大堆,搞反逻辑全错 |
| 删除 | 删点破坏图连通性,很麻烦 → 起步只做墓碑标记,定期重建,别做物理删除 |
| 入口点更新 | 新点层级超过 max_level 时,要更新 entry_point 和 max_level |

---

## 10. 子任务顺序(强烈建议照这个走)

```text
1. random_level                       几行,先跑通
2. 邻居内存布局 + node 分配/释放
3. search_layer                       最难,先用排序数组模拟堆把逻辑跑对
4. hnsw_search(基于 search_layer)     ★ 此时就能用暴力对比 recall@K
5. select_neighbors 简单版
6. hnsw_insert(基于 3+5)              完整建图
7. select_neighbors 启发式版           对比 recall 提升
8. 在 parser.c 把 db->index = hnsw_create(...) 接上,VSEARCH 自动走 HNSW
```
> 注意第 4 步:先能查再能建?不行,查需要图。实际是:先实现 insert 的简化版能把点连起来,再调 search。
> 更稳的做法:3→5→6(先能建图)→ 4(再查)→ 用 flat 对 recall。按你习惯微调即可。

---

## 11. 验证方法(也是简历素材)

```text
1. 随机生成 N 个向量,全部 VADD 入库(同时进 flat 和 hnsw)
2. 随机若干 query:
     gt   = flat_search(query, 10)      标准答案
     ann  = hnsw_search(query, 10)      HNSW 结果
     recall@10 = |gt ∩ ann| / 10
3. 调 ef_search = 10,20,50,100,200,记录 recall 和平均延迟
4. 画出 recall-延迟曲线
```
目标:recall@10 到 0.95+,同时比暴力快一两个数量级。

---

## 12. 一句话背诵 & 面试回答

> **背诵**:HNSW 是跳表的升维(分层图)。查询=顶层 ef=1 粗导航贪心下降 + Layer0 大 ef 精搜;建图=抽层级、逐层 search_layer 找候选、select_neighbors 选 M 个连双向边。三参数 M/ef_construction/ef_search,ef_search 是快准旋钮。心脏是 search_layer 的"最小堆扩展 + 最大堆守门槛 + 剪枝"。

**Q:讲讲 HNSW 的查询过程。**
> 从最高层的入口点开始,每层用贪心找离查询最近的点然后下沉,上层 ef=1 快速逼近目标区域;到最底层 Layer0 时用较大的 ef 做一次精细的近邻搜索,返回 topK。整体类似跳表的"上层大步跳、下层精找",时间复杂度约 O(log N)。

**Q:M、ef_construction、ef_search 分别是什么?**
> M 是每个节点每层连的邻居数,影响图的密度、内存和召回;ef_construction 是建图时的搜索宽度,越大图质量越高但建得越慢;ef_search 是查询时的候选集宽度,是运行时调节"召回 vs 延迟"的旋钮,调大召回升、延迟升。

**Q:HNSW 有什么缺点?**
> 一是内存占用大,每个点每层都要存邻居表;二是构建慢,插入要逐层搜索;三是删除困难,删点会破坏图连通性,工业界一般用墓碑标记加定期重建。
