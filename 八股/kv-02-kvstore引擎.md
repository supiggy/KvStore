# 项目知识点：KVStore 存储引擎（Array / Hash / 红黑树 / 跳表）

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
| 跳表 skiplist | `Z` | `ZSET / ZGET / ZDEL / ZMOD` |

> 前缀：rbtree 取首字母 `R`、hash 取 `H`、array 默认无前缀；跳表用 `Z` 而非首字母 `S`——
> 致敬 Redis：其有序集合 zset 底层正是跳表，命令也以 `Z` 开头。

四引擎同时在线，可在同一服务里横向对比行为与性能。

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

## 4. 跳表（Skip List，概率型有序链表）

### 4.1 结构

```c
struct skipnode {
    char *key; char *value;
    struct skipnode **forward;   // 长度=本节点层高，forward[i] 是第 i 层后继
};
struct skiplist {
    struct skipnode *header;     // 哨兵头节点(key=NULL)，forward 拉满 MAX_LEVEL 层
    int level;                   // 当前实际用到的最高层数
    int count;
};
```

```text
level 3:  H ----------------------> 25 -----------> NULL
level 2:  H --------> 9 ----------> 25 -----------> NULL
level 1:  H --> 6 --> 9 --> 17 ---> 25 --> 30 ----> NULL
level 0:  H --> 6 --> 9 --> 17 ---> 25 --> 30 ----> NULL   <- 最底层是完整有序链
```

在一条**有序单链表**上，随机给部分节点"加高"建出多层稀疏索引，查找像坐电梯：从最高层往右走，走不动就下沉一层，快速跳过大段节点。

### 4.2 流程树

```text
查找通用动作：x=header；i 从 level-1 到 0：while 下个 key < 目标 则右移 x；记 update[i]=x
                最后 x=x->forward[0] 即候选节点

set(key,value)
├─ 走查找记 update[]（每层的插入前驱）
├─ 候选 key 命中 -> return 1（已存在，不覆盖）
├─ random_level() 抛硬币定层高；先 malloc node/strdup key/value/malloc forward（失败回滚）
├─ 新层高 > 当前 level -> 拔高那几层的 update[i] = header，更新 level
└─ 逐层接线：node->forward[i]=update[i]->forward[i]; update[i]->forward[i]=node；count++
get(key)  -> 查找 -> 候选命中返回 value / 否则 NULL
del(key)  -> 走查找记 update[] -> 候选未命中 return 1
          -> 逐层解链(前驱 forward 不是 x 就提前停) -> free 四块内存 -> 回收空出的高层 -> count--
mod(key)  -> 查找 -> strdup 新值 -> free 旧值 -> 替换
```

### 4.3 随机层高（跳表的灵魂）

```c
int level = 1;
while ((rand() & 1) && level < MAX_LEVEL) level++;   // P=0.5：每多一层概率减半
```

- P=0.5：约 1/2 节点只 1 层、1/4 到 2 层、1/8 到 3 层……期望层数 O(log n)。
- 不需要旋转、不需要全局重排，**插入时局部抛硬币**就维持了概率平衡。
- header 的 forward 一次开满 MAX_LEVEL，省得抬高时扩容；`level` 只记当前实际最高层。

### 4.4 核心八股

| 问题 | 答 |
|---|---|
| 复杂度 | 增删查**期望 O(log n)**（最坏 O(n)，但概率极低），天然**有序**、范围查询友好 |
| 为什么能 O(log n) | 高层稀疏（期望层数 O(log n)），自顶向下每层跳过约一半节点，类似二分 |
| 为什么 Redis zset 用它而不用红黑树 | 实现简单、范围查询/前驱后继顺底层链就走、并发改造容易；红黑树旋转复杂、并发难做 |
| 删除后为什么要回收 level | 顶部若空出整层，留着会让后续查找在空高层空跑，故 `while header->forward[level-1]==NULL` 降层 |
| 和红黑树比 | 期望复杂度相同且都有序，跳表靠概率、代码短；红黑树靠确定性平衡、最坏也 O(log n) |
| 解链为什么能提前 break | 节点只到自己的层高，某层前驱的 forward 已不是 x，更高层也一定不是，无需再看 |

---

## 5. 四引擎横向对比（总归纳）

| 维度 | 数组 array | 哈希 hash | 红黑树 rbtree | 跳表 skiplist |
|---|---|---|---|---|
| 查找 | O(n) | 平均 O(1) | O(log n) | 期望 O(log n) |
| 插入/删除 | O(n)（删要搬移） | 平均 O(1) | O(log n) | 期望 O(log n) |
| 是否有序 | 插入序 | 无序 | **有序** | **有序** |
| 范围查询 | 不支持 | 不支持 | 支持 | 支持 |
| 是否需 init | 否（静态数组） | 是（桶数组 create） | 是（哨兵 nil create） | 是（哨兵 header create） |
| 平衡方式 | — | — | 确定性（旋转+变色） | 概率（随机层高） |
| 主要风险 | 容量固定、删除搬移 | 冲突退化、需 rehash | fixup 旋转易写错 | 最坏退化（概率极低） |

---

## 6. 一句话总背诵

> KVStore 用统一的 `set/get/del/mod` 接口 + 命令前缀，把数组、哈希、红黑树、跳表四个引擎挂在同一套网络/协议层下。
> 数组顺序存储、查删 O(n)；哈希用 BKDR + 拉链解决冲突、平均 O(1) 但无序；红黑树靠五条性质和旋转保持近似平衡、O(log n) 且天然有序；跳表靠随机层高维持概率平衡、期望 O(log n)、有序且范围查询友好。
> 四者公共要点是 key/value 深拷贝、失败回滚、SET 不覆盖（改值用 MOD）。

## 7. 复习顺序

```text
1. 背引擎架构：统一接口 + 命令前缀 + 返回值约定
2. 背 Hash：结构 -> BKDR -> 拉链/开放寻址 -> 装载因子/rehash
3. 背红黑树五条性质
4. 背哨兵 nil 的作用
5. 背旋转（左旋/右旋保持有序）
6. 背插入：染红 + 3 个 fixup case
7. 背删除：后继替换 + 4 个 fixup case
8. 背跳表：多层索引 -> 随机层高(P=0.5) -> 查找 update[] -> 增删接线/解链
9. 背四引擎对比表
10. 背 红黑树 vs 跳表 的取舍（确定性平衡 vs 概率平衡）
```
