# MiniVec 实现路线图 & 主数据流

这份文档是「脚手架 → 成品」的施工图。每个模块的细节流程树写在对应 `.c` 文件的函数注释里，
这里只给**全局架构、模块依赖、实现顺序、主数据流、留白难点清单**。

---

## 1. 模块依赖（自底向上）

```text
common/minivec.h        全局类型与配置（被所有模块依赖）
        │
   engine/distance      距离与归一化（最底层，纯数学）
        │
   engine/vector_store  id -> 向量 存储（依赖 distance 做归一化）
        │
   ┌────┴─────────────┐
engine/index_flat   engine/index_hnsw   两种检索索引（基线 vs 加速）
   └────┬─────────────┘
   protocol/parser      把文本命令翻译成对引擎的调用
        │
   net/server           epoll 事件循环，收发字节流
        │
   main.c               组装：init engine -> start server -> cleanup
```

---

## 2. 推荐实现顺序（每步都能独立验证）

| 步 | 模块 | 目标 | 验证方式 |
|----|------|------|----------|
| 1 | `distance.c` | 实现 dot / l2 / cosine / normalize | 单元测试：和手算结果对比 |
| 2 | `vector_store.c` | id→向量 的增删查 | 插入几条再 get 出来 |
| 3 | `index_flat.c` | 暴力 topK 检索（**正确性基线**） | 小数据集肉眼验证 |
| 4 | `parser.c` | VADD/VSEARCH/VDEL 解析分发 | 拼命令字符串调 handle |
| 5 | `server.c` | epoll 收发（你已掌握，快速填） | nc 连上跑命令 |
| 6 | `index_hnsw.c` | ★ HNSW 建图 + 查询 | **用 flat 当标准答案算 recall@K** |
| 7 | `persist`（待加） | 落盘 / 重启加载 | 重启后数据还在 |

> 黄金法则：**第 6 步 HNSW 之前，第 3 步暴力检索必须先正确**。
> 因为 HNSW 是近似算法，"错"和"对"看起来都"差不多对"，没有基线你无法判断对错。

---

## 3. 主数据流（两条核心链路）

### 3.1 VADD 写入链路

```text
客户端发 "VADD 1 0.1 0.2 ... 0.9\r\n"
   │
net/server: epoll 读到字节
   │
protocol/parser: 切 token
   ├─ tokens[0] = "VADD"
   ├─ tokens[1] = id
   └─ tokens[2..] = DIM 个 float  ── 解析成 vec_t[DIM]
   │
engine/vector_store: vstore_add(id, vec, meta)
   ├─ (COSINE 度量) vec_normalize(vec)        ← 调 distance
   ├─ 拷贝一份向量到内部存储
   └─ 记录 id -> 下标 映射
   │
engine/index_hnsw: hnsw_insert(id, vec)       ← 同时插入索引（建图）
   │
parser 写响应 "OK" -> server 发回客户端
```

### 3.2 VSEARCH 查询链路

```text
客户端发 "VSEARCH 10 0.1 0.2 ... 0.88\r\n"   (找最相似的 10 个)
   │
net/server -> protocol/parser
   ├─ tokens[1] = topk = 10
   └─ tokens[2..] = 查询向量 query[DIM]
   │
(COSINE) vec_normalize(query)
   │
检索（二选一）：
   ├─ index_flat.flat_search(query, topk)     ← 暴力：和全库算距离 + topK 堆
   └─ index_hnsw.hnsw_search(query, topk)     ← HNSW：分层图贪心 + ef 候选集
   │
得到 search_result_t out[topk]  (id + score)
   │
parser 格式化成多行 "id score" -> server 发回
```

---

## 4. 留白难点清单（按难度排序，刻意未实现）

> 这些都是你要亲手做的"学习目标"。每个函数体目前是 TODO，上方有流程树。

| 难度 | 位置 | 要点 | 对应知识分支 |
|------|------|------|--------------|
| ★☆☆ | `distance.c` | 循环点积 / L2；后续 SIMD 优化 | 分支2 相似度度量 |
| ★★☆ | `vector_store.c` | 向量拷贝、id 映射、删除标记 | 分支5 系统/工程 |
| ★★☆ | `index_flat.c` | topK 小顶堆维护 | 分支3 KNN/ANN |
| ★★☆ | `parser.c` | float 数组解析、粘包边界 | 旧八股(粘包状态机) |
| ★★☆ | `server.c` | epoll/LT/ET 事件循环 | 旧八股(epoll) |
| ★★★ | `index_hnsw.c` | **分层图、贪心下降、ef 搜索、邻居选择、随机层级** | 分支4 HNSW（招牌菜） |

---

## 5. 配套知识点

每个难点对应的"面试背诵分支"见上一轮对话整理的 10 个知识分支。
建议每实现完一个模块，就把它对应的分支写成一段「面试回答」话术（仿 `项目知识点.md` 风格）。
