# 项目知识点：KVStore 存储引擎（Array / Hash / 红黑树）

## 0. 一句话定位

> 这个 KVStore 在一套网络层（epoll / NtyCo 协程）和一套协议解析之上，挂了多个**存储引擎**。
> 协议层把命令字符串映射成枚举再 `switch` 分发，靠**命令前缀**区分引擎，每个引擎只对外暴露
> `set / get / del / mod` 四个统一签名的接口。要加新引擎，只需实现这四个函数并在协议层接线。

## 1. 引擎架构总览

### 1.1 数据流

```text
recv -> kvstore_request
      -> kvstore_spilt_token   按 " \r\n\t" 分词
      -> kvstore_parse_protocol
           -> kvstore_command_index  命令字符串 -> 枚举下标
           -> switch(cmd) 调对应引擎的 set/get/del/mod
           -> 把结果写进 item->wbuffer
      -> send
```

### 1.2 命令前缀路由

| 引擎 | 前缀 | 命令 |
|---|---|---|
| 数组 array | 无 | `SET / GET / DEL / MOD` |
| 红黑树 rbtree | `R` | `RSET / RGET / RDEL / RMOD` |
| 哈希 hash | `H` | `HSET / HGET / HDEL / HMOD` |

三引擎同时在线，可在同一服务里横向对比行为与性能。

### 1.3 统一返回值约定（三引擎必须对齐）

```text
set: 0 成功 / 1 key 已存在(不覆盖) / -1 出错      -> 响应 SUCCESS / FAIL / ERROR
get: value 指针 / NULL                          -> 响应 值 / NO EXIST
del: 0 成功 / 1 不存在 / -1 出错                  -> 响应 SUCCESS / NO EXIST / ERROR
mod: 0 成功 / 1 不存在 / -1 出错                  -> 响应 SUCCESS / NO EXIST / ERROR
```

> 重点：**SET 只负责"新增"，遇到已存在的 key 返回 1(FAIL)，不覆盖；改值要用 MOD。**
> 这是本项目的设计选择（和 Redis SET 覆盖语义不同），三引擎保持一致。

### 1.4 两条通用工程要点

- **深拷贝**：`key/value` 都来自 `rbuffer`，会被下一次请求覆盖，所以入库前必须 `strdup`（分配新内存 + 拷贝），不能直接存外部指针。
- **失败回滚**：`set` 里逐步分配 node/key/value，任何一步失败都要释放前面已分配的，避免内存泄漏。

---

## 2. Hash 表（链地址法）

### 2.1 结构

```c
struct hashnode { char *key; char *value; struct hashnode *next; }; // 冲突链节点
struct hashtable {
    struct hashnode **nodes;   // 桶数组
    int max_slots;             // 桶个数
    int count;                 // 键值对总数
};
```

```text
nodes[0] -> NULL
nodes[1] -> {k1,v1} -> {k7,v7} -> NULL   // k1、k7 哈希冲突，串成链
nodes[2] -> {k2,v2} -> NULL
```

### 2.2 流程树

```text
set(key,value)
├─ 查重 find(key)
│  ├─ 命中 -> return 1 (已存在，不覆盖)
│  └─ 未命中
│     ├─ malloc node / strdup key / strdup value（逐步分配，失败回滚）
│     └─ 头插到 nodes[slot]，count++
get(key)   -> hash -> 定位桶 -> 遍历链 strcmp -> 命中返回 value / 否则 NULL
del(key)   -> hash -> 定位桶 -> prev 指针遍历 -> 解链 + free 三块内存 -> count--
mod(key)   -> find -> strdup 新值 -> free 旧值 -> 替换
```

### 2.3 哈希函数（BKDR）

```c
hash = hash * 131 + (unsigned char)c;   // 逐字符累乘累加
idx  = hash % max_slots;                // 映射到桶下标
```

- 131（或 31、1313）是经验上冲突较少的乘子。
- 强转 `unsigned char` 防止负 char；用 `unsigned` 让溢出按模 2^32 回绕。

### 2.4 核心八股

| 问题 | 答 |
|---|---|
| 复杂度 | 哈希均匀时增删查平均 **O(1)**；极端冲突退化成单链表 **O(n)** |
| 冲突怎么解决 | 两大流派：**链地址法**（拉链，本项目用）、**开放寻址法**（线性/二次探测、双哈希） |
| 拉链 vs 开放寻址 | 拉链：删除简单、不怕装满；开放寻址：缓存友好但删除要 tombstone、装载率高会聚集 |
| 装载因子 | `count / max_slots`，过大查询变慢，工程上超阈值（如 0.75）就 **rehash 扩容**（开新桶数组、全部重新散列）。本项目桶数固定不扩容，是教学简化 |
| 为什么 set 要先查重 | 否则同 key 会插多份，get 拿到旧值、del 删不干净 |
| 和红黑树比 | 哈希快但**无序**，不支持范围查询、前驱后继、有序遍历 |

---

## 3. 红黑树（自平衡 BST，标准 CLRS）

### 3.1 五条性质（必背）

```text
1. 每个节点非红即黑
2. 根是黑
3. 叶子(NIL)是黑          <- 本实现用哨兵 nil 代替所有 NULL 叶子
4. 红节点的孩子必须是黑    <- 不能有连续红节点
5. 任一节点到所有叶子路径的黑节点数相同（黑高一致）
=> 由 4、5 推出：最长路径 <= 2 * 最短路径，树高 O(log n)
```

### 3.2 结构与哨兵

```c
#define RBTREE_RED 0
#define RBTREE_BLACK 1
struct rbtree_node { int color; struct rbtree_node *left,*right,*parent; char *key,*value; };
struct rbtree { struct rbtree_node *root, *nil; };  // nil 是哨兵，恒黑
```

哨兵 nil 的作用：把所有空指针统一成一个黑节点，旋转/修复时不用到处判 NULL；它的 `parent` 也可被临时写入，`delete_fixup` 从 nil 往上回溯就靠这个。

### 3.3 旋转（重新平衡的基础动作）

```text
    x              y
     \    左旋     / \
      y   ==>     x   c
     / \           \
    b   c           b
```

左旋：右孩子 y 转上来当父亲，x 成为 y 的左孩子，y 原左子树 b 改挂到 x 右边。右旋是镜像。旋转**保持中序有序不变**，只调结构。

### 3.4 插入：新节点先染红 + insert_fixup

新节点一律染**红**（插红不破坏黑高性质5，只可能破坏性质4=连续红）。若父也红则修复，以"父是祖父左孩子"为例看叔叔 y：

```text
case1 叔叔红：父、叔变黑，祖父变红，问题上移到祖父（z=祖父）继续
case2 叔叔黑且 z 在内侧(右)：对父左旋，转成 case3
case3 叔叔黑且 z 在外侧(左)：父变黑、祖父变红，对祖父右旋，结束
```

"父是右孩子"为左右镜像。最后强制根为黑。**插入最多 2 次旋转。**

### 3.5 删除：transplant + 后继替换 + delete_fixup

```text
del(z)
├─ z 至多一个孩子 -> 用那个孩子(可能是 nil)顶替 z
└─ z 有两个孩子   -> 找右子树最小值 y(中序后继)顶替 z，y 的右孩子 x 顶替 y
y_original_color = 真正消失的颜色
若为黑 -> delete_fixup(x)   // 删黑才破坏黑高
```

delete_fixup 围绕兄弟 w 分四种情况（x 是左孩子，右孩子镜像）：

```text
case1 兄弟红：旋转+变色，把兄弟变黑，转成 case2/3/4
case2 兄弟黑且两孩子都黑：兄弟变红，额外黑上移到父亲(x=父)
case3 兄弟黑、远侄黑：旋转兄弟，转成 case4
case4 兄弟黑且远侄红：变色 + 旋转父亲，补上黑高，结束(x=root)
```

**删除最多 3 次旋转。**

### 3.6 核心八股

| 问题 | 答 |
|---|---|
| 复杂度 | 增删查均 **O(log n)**，天然**有序**，支持范围查询/前驱后继/中序输出有序 |
| 哨兵 nil 干嘛的 | 统一所有 NULL 叶子为一个黑节点，免边界判空，简化 fixup；其 parent 可被写入供回溯 |
| 新节点为什么染红 | 插红只可能破坏"无连续红"(性质4)，不动黑高(性质5)，修复代价小 |
| 为什么用红黑树而非 AVL | AVL 更严格平衡、查询略快，但插删旋转次数多；红黑树插入≤2、删除≤3 次旋转，**写多场景更划算**（Linux CFS 调度、epoll 内核就绪树、C++ map/set 都用它） |
| 为什么 Redis 有序集合用跳表 | 跳表实现简单、范围查询友好、并发改造容易，红黑树旋转复杂、并发难做 |

---

## 4. 三引擎横向对比（总归纳）

| 维度 | 数组 array | 哈希 hash | 红黑树 rbtree |
|---|---|---|---|
| 查找 | O(n) | 平均 O(1) | O(log n) |
| 插入/删除 | O(n)（删要搬移） | 平均 O(1) | O(log n) |
| 是否有序 | 插入序 | 无序 | **有序** |
| 范围查询 | 不支持 | 不支持 | 支持 |
| 是否需 init | 否（静态数组） | 是（桶数组 create） | 是（哨兵 nil create） |
| 主要风险 | 容量固定、删除搬移 | 冲突退化、需 rehash | fixup 旋转易写错 |

---

## 5. 一句话总背诵

> KVStore 用统一的 `set/get/del/mod` 接口 + 命令前缀，把数组、哈希、红黑树三个引擎挂在同一套网络/协议层下。
> 数组顺序存储、查删 O(n)；哈希用 BKDR + 拉链解决冲突、平均 O(1) 但无序；红黑树靠五条性质和旋转保持近似平衡、O(log n) 且天然有序。
> 三者公共要点是 key/value 深拷贝、失败回滚、SET 不覆盖（改值用 MOD）。

## 6. 复习顺序

```text
1. 背引擎架构：统一接口 + 命令前缀 + 返回值约定
2. 背 Hash：结构 -> BKDR -> 拉链/开放寻址 -> 装载因子/rehash
3. 背红黑树五条性质
4. 背哨兵 nil 的作用
5. 背旋转（左旋/右旋保持有序）
6. 背插入：染红 + 3 个 fixup case
7. 背删除：后继替换 + 4 个 fixup case
8. 背三引擎对比表
9. 背 红黑树 vs AVL vs 跳表 的取舍
```
