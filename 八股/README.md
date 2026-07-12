# 八股索引(面试复习总入口)

按 `kv-`(KVStore 基本功)/ `mv-`(MiniVec 旗舰)分两组。每份结尾都有「30 秒口头总结」可直接背。

## KVStore(存储引擎 + 网络基本功)
| 文件 | 内容 | 对应代码 |
|------|------|----------|
| [kv-01-数据结构.md](kv-01-数据结构.md) | 哈希(拉链)/ 红黑树 / 跳表 + Redis(zset 为何用跳表、双结构) | `kvstore_hash/rbtree/skiplist.c` |
| [kv-02-kvstore引擎.md](kv-02-kvstore引擎.md) | 四引擎架构、命令路由、返回值约定、横向对比 | `kvstore*.c` |
| [kv-03-网络IO.md](kv-03-网络IO.md) | select/poll/epoll、LT/ET、非阻塞、粘包 + 状态机 | `epoll_entry.c` |

## MiniVec(向量数据库,G 系列谈资)
| 文件 | 内容 | 对应代码 |
|------|------|----------|
| [mv-01-并发.md](mv-01-并发.md) | G3 读写锁、G4 主从 Reactor、惊群、Redis 为何单线程、有锁 vs 无锁 | `minivec/src/protocol/parser.c`、`minivec/src/net/server.c` |
| [mv-02-向量量化.md](mv-02-向量量化.md) | G7 int8 标量量化、G8 PQ、ADC vs SDC、k-means | `minivec/src/engine/quant.*`、`pq.*` |
| [mv-03-检索工程.md](mv-03-检索工程.md) | G1 软删墓碑(+真 bug 故事)、G6 recall@K / QPS·p99 / recall-QPS 曲线 | `minivec/src/engine/index_hnsw.c`、`minivec/bench/metrics.*` |
| [mv-04-堆与KNN.md](mv-04-堆与KNN.md) | 堆原理(siftUp/Down)、KNN topK 用大/小顶堆、priority_queue | `minivec/src/engine/topk_heap.c`、`index_flat.c` |

## 建议复习顺序
1. **先补没看过的新模块**(最近写的 G 系列,谈资最新):mv-01 并发 → mv-02 量化 → mv-03 检索工程。
2. **旗舰基础**:mv-04 堆与 KNN。
3. **临考扫一遍已有笔记**:kv-01 → kv-02 → kv-03 + 各自手写题。

> 设计取舍 / 代码逐行讲解在 `minivec/docs/`(`讲解-0*.md`、`设计决策.md`、`ROADMAP.md`),与本目录的"面试谈资"互补。
