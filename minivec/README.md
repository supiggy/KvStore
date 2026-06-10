# MiniVec

从零实现的向量数据库（学习项目）。在 kvstore 的网络层 / 引擎设计套路之上，把"存字符串"换成"存向量 + 近邻检索"。

## 当前状态：脚手架（骨架已搭好，核心逻辑留白）

所有模块的**接口、类型、文件结构、构建系统**都已就位并能编译。
所有**具体算法逻辑**（距离计算、检索、HNSW 建图/查询、协议解析、事件循环）
都以 `TODO` 形式留白，每个函数上方附带"流程树"注释，按它实现即可。

> 难点（尤其是 HNSW）刻意没有实现 —— 留给你逐个理解后再推进。

## 目录结构

```text
minivec/
├── Makefile
├── README.md
├── docs/
│   └── ROADMAP.md          # 架构、实现顺序、主数据流流程树、留白清单
├── src/
│   ├── main.c              # 入口：初始化引擎 -> 启动网络服务 -> 清理
│   ├── common/
│   │   └── minivec.h       # 全局配置、共享类型、内存小工具
│   ├── net/
│   │   ├── server.h
│   │   └── server.c        # epoll 服务循环（留白，你已掌握，可快速填）
│   ├── protocol/
│   │   ├── parser.h
│   │   └── parser.c        # VADD / VSEARCH / VDEL 解析与分发（留白）
│   └── engine/
│       ├── distance.h
│       ├── distance.c      # 余弦 / 内积 / L2 + 归一化（留白，热身）
│       ├── vector_store.h
│       ├── vector_store.c  # id -> 向量 存储（留白）
│       ├── index_flat.h
│       ├── index_flat.c    # 暴力检索（留白，正确性基线）
│       ├── index_hnsw.h
│       └── index_hnsw.c    # HNSW 近邻索引（★ 核心难点，重点留白）
└── tools/
    └── embed.py            # (待加) 调 embedding 模型，把文本转向量灌库
```

## 构建

```sh
# 在 gcc:14 Docker 容器内
make
./minivec
```

## 实现顺序

见 `docs/ROADMAP.md`。建议顺序：
distance → vector_store → index_flat → parser → server → index_hnsw → persist。
