# MiniVec 演进路线图(goal 总纲)

> 核心引擎已全部实现并可运行。这份文档**不是"怎么把功能做完"**,而是**"怎么让它从练手项目变成有工程判断力的证据"**。
> 配套设计取舍见 [../DESIGN.md](../DESIGN.md)。

---

## 0. 毕业标准(满足 = 不再是练手)

做到这 5 条,这个项目就"出师"了。**功能更少但满足这 5 条 > 功能很多但一条不沾。**

- [ ] 仓库 README 第一屏是**一张性能表**(已就位,数字待 G6 填),不是"学习项目"四个字。
- [ ] 有 `bench/`,**一条命令能复现**那些数字,并产出一张图(recall–ef / QPS–线程)。
- [ ] 有 `DESIGN.md`(已就位):为什么这么选 / 已知局限 / 10 亿向量下怎么改。
- [ ] 至少 **1 个有 before/after 数字的优化故事**写进文档(war story)。
- [ ] 跟 **Faiss / hnswlib 做过一次诚实对标**,知道自己差在哪、为什么。

---

## 1. 优先级:为什么不是"把 goal 全做完"

| 层级 | goal | 理由 |
|------|------|------|
| **核心交付**(先做) | G0 基准 + G6 recall | 直接产出毕业标准里的数字和曲线。没有它,后面所有改动都无法验证对错/快慢 |
| **挑 1 个深做**(出 war story) | G3/G4(并发,"阻塞 IO 线程"是天然故事) 或 G7/G8(量化,"内存降 4x recall 只掉 2%"是天然取舍) | 深度故事 > 功能数量 |
| **正确性补全** | G1(删除 bug)、G2(WAL) | G1 是真 bug(见 DESIGN §3.3),优先级高 |
| **后置**(不产出数字/故事就先放着) | G5 协议、G9 过滤 | 纯功能,锦上添花 |

> 黄金法则:**改之前先有基线(G0)。** 删除/WAL/并发/量化全会改坏正确性或性能,没基线就是盲改。

---

## 2. goal 清单

每个 goal 的协作约定:**【骨架】= 我搭**(接口/类型/流程树注释,能编译、测试绿) · **【核心】= 你填**(留白,自己啃) · **【谈资】= 填完写成一段面试话术**(仿 `项目知识点.md`)。

### G0 · 测试 + 压测基线 〔核心交付,先做〕
- **目标:** 一键验证"引擎正确"和"性能数字";顺手清理代码里过时的"还是桩/TODO"注释。
- **【骨架】** 单测框架(distance/vstore/flat 正确性用例)、造数脚本、QPS/p99 压测客户端、flat-vs-hnsw recall sanity check。
- **【核心】** 具体断言、p99 百分位统计、压测并发逻辑。
- **【谈资】** 为什么重构前先立基线;p99 为什么比平均值有意义。
- 状态:**骨架已搭**(其 9 条用例全绿;`make bench` 端到端可跑。注:整体 `make test` 现含 G1/G7 的 TDD 红,各为对应目标的待办信号)。**留白待你填**:`bench/metrics.c` 的 `percentile()` 与 `recall_at_k()`(G6 已抽成共享,填一次两个基准都用);`tests/` 里 5 处 `TODO` 断言(distance 归一化、cosine=归一化后 dot;vstore 删除墓碑、重复 id 拒绝;flat top-2 顺序)。
- 文件:`tests/`(框架+3 套单测)、`bench/`(gen + metrics + 引擎基准)、`Makefile`(test/bench 目标,只链引擎不依赖 epoll)。

### G6 · recall@K 基准 + 曲线 〔核心交付〕
- **目标:** 用 flat 当 ground truth 量 HNSW recall,扫 M/ef 出 recall–latency 曲线,填进 README 性能表。
- **【骨架】** benchmark 框架、ground-truth 生成、参数扫描骨架、出图脚本。
- **【核心】** recall@K 计算、参数扫描循环。
- **【谈资】** recall 定义、recall–latency 三角、M/ef 调参。
- 状态:**骨架已搭**(`make sweep` 可跑、产 `bench/results.csv`;`plot_recall.py` 出 recall–QPS 曲线)。**留白待你填**:`bench/sweep.c` main 里的【扫描循环】(外层 M 重建图、内层 ef_search 复用),外加 G0 共享的 `percentile`/`recall_at_k`。
- 文件:`bench/metrics.{h,c}`(共享指标)、`bench/sweep.c`(`build_hnsw`+`measure_queries` 已填)、`bench/plot_recall.py`(出图)、`Makefile` `sweep` 目标。

### G1 · HNSW 删除 〔正确性,优先〕
- **目标:** 修 DESIGN §3.3 的 bug——VDEL 后 VSEARCH 仍返回已删向量。tombstone 软删 + VDEL 接通索引 + 空间回收/重建。
- **【骨架】** vstore 删除标记接口(已有墓碑)、HNSW `delete` API 桩、重建触发骨架。
- **【核心】** 搜索时过滤 tombstone、重建/compaction、重建时邻居重连。
- **【谈资】** 图索引删除为何难、软删 vs 硬删、墓碑堆积与重建时机。
- 状态:**骨架已搭**(`make all` 全量编译过;`make test` 14 项里 **1 项故意红** = 复现了 §3.3 的 bug)。已填:`hnsw_delete`/`hnsw_deleted_count`、节点 `deleted` 标记、parser `VDEL` 同步软删、TDD 红测试。**留白待你填**:`hnsw_search` 里加一行"跳过墓碑节点"(填完 `test_hnsw_delete` 转绿 = G1 完成)。**进阶留白**:墓碑堆积后的重建/compaction(用 `hnsw_deleted_count` 判定阈值,在 db 层 destroy+重建+重灌 live,参考 `minivec_load`)。
- 文件:`src/engine/index_hnsw.{c,h}`、`src/protocol/parser.c`、`tests/test_hnsw_delete.c`。

### G2 · 崩溃安全持久化 / WAL
- **目标:** persist 现在只有手动全量快照(原子 rename 已做),无 WAL → 未 SAVE 即崩溃丢增量。加 WAL + 启动回放 + 快照截断。
- **【骨架】** WAL 文件格式、append 接口、replay 主循环、快照+WAL 衔接骨架。
- **【核心】** WAL 记录编解码、崩溃回放、fsync 时机。
- **【谈资】** WAL 原理、RDB vs AOF、fsync/OS 缓冲、崩溃一致性。
- 状态:`未开始`

### G3 · 并发(读写锁 → 分片锁) 〔war story 候选〕
- **目标:** 引擎零锁、单线程。先全局读写锁,再分片锁降争用。
- **【骨架】** RW 锁封装、分片结构骨架、路由桩。
- **【核心】** 锁粒度、分片路由、并发不变量、死锁规避。
- **【谈资】** 读写锁、分片降争用、Redis 为何单线程、有锁 vs 无锁。
- 状态:**骨架已搭**(`make all` 编译过)。已填:db 加 `pthread_rwlock_t` + init/destroy;读命令(VSEARCH/VCOUNT/SAVE)与写命令(VADD/VDEL/LOAD)分别在引擎操作【外层】加读/写锁(call site 已布好,临界区最小,锁外解析/格式化)。**留白待你填**:`db_read_lock`/`db_write_lock`/`db_unlock` 三个函数体(即 `pthread_rwlock_rdlock`/`wrlock`/`unlock` 三行)。**进阶留白**:分片锁(按 id 分 N 片各一把锁,降低写串行化争用)。
- 文件:`src/protocol/parser.c`。

### G4 · 主从 Reactor 多线程 〔war story 候选〕
- **目标:** server 现在单 reactor 单线程,HNSW 操作阻塞事件循环。改主 reactor accept + 从 reactor 线程池处理 IO(配合 G3)。
- **【骨架】** 线程模型骨架、accept→分发、每线程一个 epoll。
- **【核心】** 连接分发/负载均衡、线程间唤醒、连接归属。
- **【谈资】** 主从 reactor、惊群与 EPOLLEXCLUSIVE、one-loop-per-thread。
- 状态:**骨架已搭**(`make all` 编译过)。已填:N 个 worker 各自 epoll + 一根管道、`worker_loop`、`worker_register`、主线程 round-robin accept、recv/send 行分帧改用连接自带的 `epfd`。**留白待你填**:跨线程把新连接交给 worker —— 留白 A(主线程 `write(w->pipe_w, &connfd, ...)`)+ 留白 B(worker 从非阻塞 `pipe_r` 循环读出 connfd 并 `worker_register`)。填完 A/B + G3 的锁,多线程才真正跑通。
- 文件:`src/net/server.c`。配套并发压测工具:`bench/loadgen.c`(`make loadgen`)——多线程连真实服务端,产出端到端 QPS/p99(留白:一次请求-响应往返的计时)。

### G7 · int8 标量量化 〔war story 候选〕
- **目标:** float32 → int8,内存 1/4(minivec.h 已埋点),用 G6 量精度损失。
- **【骨架】** codec 接口、量化版 vstore 桩、量化距离桩。
- **【核心】** 量化/反量化、int8 距离、(选)SIMD。
- **【谈资】** 标量量化、精度 vs 内存、SIMD。
- 状态:**骨架已搭**(`make all` 编译过;`test_quant` 2 条 TDD 红)。已填:`quant.{h,c}` 对称量化接口 + TDD 测试(反量化误差、量化域内积精度)。**留白待你填**:`q8_encode`/`q8_decode`/`q8_dot` 三个函数体。**进阶**:把量化向量接进 flat/HNSW 检索,用 G6 recall 曲线量"内存 4x↓ / recall 掉几个点"。
- 文件:`src/engine/quant.{c,h}`、`tests/test_quant.c`。

### G8 · PQ 乘积量化 〔招牌〕
- **目标:** 子空间 + k-means 码本 + 距离查表(ADC)。
- **【骨架】** PQ 结构、码本训练接口、查表骨架。
- **【核心】** k-means、距离表、ADC 搜索。
- **【谈资】** PQ 原理、IVF-PQ、为何能大幅压缩还能搜。
- 状态:`未开始`

### G5 · 二进制协议 〔后置〕
- **目标:** length-prefixed 二进制替代 float-as-text(单条 ~3.5KB),保留文本兼容。
- **【骨架】** 协议头、编解码桩、半包状态机骨架。
- **【核心】** float 数组打包/解包、半包状态机、字节序。
- **【谈资】** 文本 vs 二进制、粘包三解法、协议向后兼容。
- 状态:`未开始`

### G9 · 过滤检索 〔后置〕
- **目标:** metadata 过滤 + ANN(pre/post-filter)。
- **【骨架】** metadata 存储扩展、filter 接口。
- **【核心】** filter 与 HNSW 遍历结合策略。
- **【谈资】** filtered search 为何难、pre vs post filter。
- 状态:`未开始`

---

## 3. 收尾大叙事(选做)

`sim/robotdog_slam_kv` 可把整个项目串成"端侧 AI 状态存储 + 向量检索"的应用故事,当面试收尾。不占核心 goal。

---

## 建议节奏

`G0 → G6`(立住数字)→ 从 `G3/G4/G7/G8` 挑 **1 个深做出 war story** → `G1`(修 bug)→ 对标 Faiss/hnswlib(达成毕业标准)→ 其余按需。
