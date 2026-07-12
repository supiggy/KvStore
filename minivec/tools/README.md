# tools/ —— 把 MiniVec 接上 embedding,做语义搜索 demo

MiniVec 本身只存"向量",不懂"文本"。`embed.py` 负责:文本 → 向量 → 灌进 server → 语义查询。

## 0. 前置:维度要对齐

`minivec.h` 里 `MINIVEC_DIM` 必须等于 embedding 模型的输出维度。
当前 = **384**(对齐 `all-MiniLM-L6-v2`)。换模型就改这个数再重新 `make`。

## 1. 启动 server(在 gcc:14 Docker 里,映射端口)

```sh
# 在 minivec/ 目录
docker run --rm -v "$PWD:/src" -w /src gcc:14 make          # 编译出 ./minivec
docker run --rm -d --name mvsrv -p 9097:9097 -v "$PWD:/src" -w /src gcc:14 ./minivec
```
> Windows PowerShell 把 `$PWD` 换成绝对路径,`-p 9097:9097` 把端口暴露到宿主机。

## 2. 灌库 + 查询

### 方式 A:真·语义(需要 sentence-transformers)
```sh
pip install sentence-transformers          # 带 torch,几百 MB,首次联网下模型
python3 embed.py index corpus.txt          # 把每行文本转向量灌进去
python3 embed.py query "how do neural nets learn" 3
python3 embed.py query "animals as pets" 3
```
期望:第一个查询命中"训练神经网络/反向传播"那几条;第二个命中"猫/小猫"那几条 —— 即使用词不同也能命中(语义)。

### 方式 B:--fake(无依赖,验证管线)
```sh
python3 embed.py index corpus.txt --fake
python3 embed.py query "train neural network gradient" 3 --fake
```
`--fake` 是词袋哈希,只有"词面"相似(共享单词才接近),用来证明"客户端↔server"链路通。**不是语义**。

## 3. 停掉 server

```sh
docker stop mvsrv
```

## 已知坑(对应主对话里 Item 1 的问题清单)

- **维度不匹配** → VADD 解析失败。改 `MINIVEC_DIM` 并重新 make。
- **粘包**:协议是一问一答(发一条、读一行、再发下一条)。别在没读响应前连发多条。
- **原文不在 server 里**:`embed.py` 把 `id→原文` 存到 `corpus.txt.map.json`,查询时用它把 id 翻译回句子。
- **环境**:gcc:14 容器只用来跑 server(C);`embed.py` 在你自己的 Python 环境跑。
