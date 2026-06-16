# MiniVec

一个用 C 从零实现的向量数据库:支持余弦 / 内积 / L2 度量,内置 **HNSW** 近似最近邻索引与暴力检索基线,通过 epoll TCP 服务对外提供 `VADD / VSEARCH / VDEL` 等命令,支持快照持久化与文本→向量的 embedding 灌库。

> 设计取舍与已知局限见 [DESIGN.md](DESIGN.md);演进路线与"毕业标准"见 [docs/ROADMAP.md](docs/ROADMAP.md)。

---

## 性能

> 数据集:`待填`(N 条 × 384 维) · 硬件:`待填` · 复现:`make bench`(见 ROADMAP G0/G6)
>
> ⚠️ 下表数字由基准 harness 自动产出。**在 G6 跑出来之前留空,不手填、不估算。**

| 指标 | 暴力 (flat) | HNSW | 备注 |
|------|-------------|------|------|
| recall@10 | 100%(基线) | `待填` | ef_search=50 |
| 单查询 p99 | `待填` ms | `待填` ms | |
| QPS(单线程) | `待填` | `待填` | |
| 内存 / 向量 | `待填` B | `待填` B | float32,量化后见 G7/G8 |

recall–ef 曲线、QPS–线程数曲线见 `bench/`(待 G6 产出)。

---

## 架构

```text
common/minivec.h        全局类型与配置(DIM=384, 度量, id→向量记录)
        │
   engine/distance      距离与归一化(最底层,纯数学)
        │
   engine/vector_store  id → 向量 存储(依赖 distance 做归一化)
        │
   ┌────┴─────────────┐
engine/index_flat   engine/index_hnsw   两种检索索引(基线 vs 加速)
   └────┬─────────────┘
   engine/persist       快照落盘 / 重启加载(原子 rename)
        │
   protocol/parser      文本命令 → 引擎调用,大 switch 分发
        │
   net/server           epoll 事件循环 + 行分帧(解决 ~3.5KB/条的粘包)
        │
   main.c               组装:create db → start server → cleanup
```

---

## 命令协议

文本行协议,以 `\n` 分帧(一条命令一行)。默认监听端口 **9097**,度量默认 **余弦**。

| 命令 | 语义 |
|------|------|
| `VADD <id> <v1> <v2> ... <v384>` | 插入一条向量(余弦度量下入库即归一化),同步建 HNSW 图 |
| `VSEARCH <topk> <v1> ... <v384>` | 返回最相似的 topk 条,多行 `id score` |
| `VDEL <id>` | 删除一条 |
| `VCOUNT` | 当前向量数(含墓碑,见 DESIGN) |
| `SAVE <path>` / `LOAD <path>` | 全量快照落盘 / 加载并重建图 |

手动验证示例:

```sh
# 终端 A
./minivec
# 终端 B(384 维示例用 tools/embed.py 生成,这里用占位)
printf 'VADD 1 0.1 0.2 ... 0.9\n'      | nc 127.0.0.1 9097
printf 'VSEARCH 5 0.1 0.2 ... 0.88\n'  | nc 127.0.0.1 9097
printf 'VCOUNT\n'                       | nc 127.0.0.1 9097
```

---

## 构建 & 运行

网络层依赖 Linux(epoll),**在 `gcc:14` Docker 容器里构建,不要原生 Windows 编译。**

```sh
make            # 产出 ./minivec
./minivec       # 监听 9097

# 文本 → 向量 灌库流水线
python3 tools/embed.py   # 见 tools/README.md
```

换 embedding 模型 → 改 `src/common/minivec.h` 里的 `MINIVEC_DIM` 后重新 `make`。

---

## 现状

核心引擎(distance / vector_store / index_flat / index_hnsw / persist / parser / server)**均已实现并可运行**。当前工作重心是"把它从练手项目推到能用数字和取舍说话的工程项目"——见 [docs/ROADMAP.md](docs/ROADMAP.md) 的毕业标准与 goal 清单。
