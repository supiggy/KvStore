# 项目知识点：TCP 多路复用服务器

## 1. 当前代码定位

来源文件：`tcp-connect-template.c`

当前生效分支：

```c
#elif 1
//epoll
```

当前代码实现的是一个基于 TCP 的 echo server：

- `socket()` 创建监听 socket。
- `bind()` 绑定本地地址和 8080 端口。
- `listen()` 让 socket 进入监听状态。
- `epoll_create()` 创建 epoll 实例。
- `epoll_ctl()` 把监听 fd 注册进 epoll。
- `epoll_wait()` 等待就绪事件。
- 监听 fd 可读时，调用 `accept()` 接收新连接。
- 客户端 fd 可读时，调用 `recv()` 读数据。
- `recv()` 返回 `0` 时，说明对端关闭连接，调用 `epoll_ctl(DEL)` 和 `close()` 清理。
- `recv()` 返回正数时，调用 `send()` 原样回显。

一句话背诵：

> 当前代码是一个单线程、阻塞 socket、默认 LT 水平触发的 epoll TCP echo server。它用 epoll 管理监听 fd 和多个客户端 fd，监听 fd 负责 accept，客户端 fd 负责 recv/send。

## 2. 当前 tcp-connect-template.c 流程树

```text
main()
├─ socket(AF_INET, SOCK_STREAM, 0)
│  └─ 创建 TCP 监听 socket，得到 sockfd
├─ 初始化 sockaddr_in serveraddr
│  ├─ sin_family = AF_INET
│  ├─ sin_port = htons(8080)
│  └─ sin_addr.s_addr = htonl(INADDR_ANY)
├─ bind(sockfd, (struct sockaddr *)&serveraddr, ...)
│  ├─ 失败
│  │  ├─ perror("bind")
│  │  └─ return -1
│  └─ 成功
│     └─ 继续
├─ listen(sockfd, 10)
│  └─ 进入监听状态，等待客户端连接
├─ epoll_create(1)
│  └─ 创建 epoll 实例，得到 epfd
├─ 初始化 epoll_event ev
│  ├─ ev.events = EPOLLIN
│  └─ ev.data.fd = sockfd
├─ epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev)
│  └─ 把监听 fd 注册到 epoll，关注可读事件
├─ struct epoll_event events[1024]
│  └─ 用来接收 epoll_wait 返回的就绪事件列表
└─ while (1)
   ├─ epoll_wait(epfd, events, 1024, -1)
   │  ├─ 阻塞等待事件
   │  └─ 返回 nready 个就绪事件
   └─ for i = 0; i < nready; i++
      ├─ connfd = events[i].data.fd
      ├─ if events[i].data.fd == sockfd
      │  ├─ 监听 socket 可读
      │  ├─ 含义：有新连接到来
      │  ├─ accept(sockfd, ...)
      │  ├─ 得到 clientfd
      │  ├─ ev.events = EPOLLIN
      │  ├─ ev.data.fd = clientfd
      │  └─ epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev)
      └─ else if events[i].events & EPOLLIN
         ├─ 客户端 socket 可读
         ├─ recv(events[i].data.fd, buffer, sizeof(buffer), 0)
         ├─ if count == 0
         │  ├─ 对端关闭连接
         │  ├─ epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)
         │  └─ close(fd)
         └─ else
            └─ send(fd, buffer, count, 0)
```

## 3. 总体背诵逻辑

背这块知识建议按这个顺序：

```text
TCP server 基础流程
  -> IO 阻塞问题
  -> select/poll/epoll 为什么出现
  -> select 的 fd_set 和缺点
  -> poll 的 pollfd 数组
  -> epoll 的 create/ctl/wait 三阶段
  -> 监听 fd 和客户端 fd 的事件分流
  -> LT/ET 触发模式
  -> 非阻塞 IO
  -> TCP 粘包和状态机
  -> epoll 线程安全和 Reactor 模型
```

一句话背诵：

> 先讲服务端怎么建立连接，再讲一个线程如何管理多个 fd，最后讲 epoll 如何减少全量拷贝和全量扫描，并补上 LT/ET、非阻塞、粘包状态机和多线程模型。

## 4. TCP 服务端基础八股

### 4.1 TCP server 标准流程

```text
socket -> bind -> listen -> accept -> recv/send -> close
```

- `socket()`：创建通信端点。
- `bind()`：绑定本地 IP 和端口。
- `listen()`：进入监听状态。
- `accept()`：从已完成连接队列中取出连接，返回 `clientfd`。
- `recv()`：接收客户端数据。
- `send()`：发送数据给客户端。
- `close()`：关闭 fd，释放资源。

面试回答：

> 服务端先用 `socket` 创建监听 socket，再通过 `bind` 绑定本地地址，调用 `listen` 进入监听状态。之后通过 `accept` 获取客户端连接 fd，再用 `recv/send` 和客户端通信，最后用 `close` 关闭连接。

### 4.2 监听 fd 和连接 fd 的区别

```text
sockfd
  -> 监听 fd
  -> 只负责接收新连接
  -> 可读时调用 accept()

clientfd
  -> 连接 fd
  -> 负责和某个客户端通信
  -> 可读时调用 recv()
```

面试回答：

> 监听 fd 和连接 fd 不是一个东西。监听 fd 用来接收新连接，连接 fd 用来收发数据。监听 fd 可读表示有新连接，连接 fd 可读表示有数据或连接关闭。

### 4.3 `recv()` 返回值

```c
int count = recv(fd, buffer, sizeof(buffer), 0);
```

- `count > 0`：读到数据。
- `count == 0`：对端正常关闭连接。
- `count < 0`：发生错误。非阻塞模式下可能是 `EAGAIN` 或 `EWOULDBLOCK`。

面试回答：

> `recv` 返回正数表示读到的字节数，返回 0 表示对端关闭连接，返回负数表示出错。非阻塞 fd 没有数据时也会返回 -1，并设置 `errno` 为 `EAGAIN` 或 `EWOULDBLOCK`。

## 5. select 核心八股

### 5.1 select 做什么

`select` 是 IO 多路复用接口，可以让一个线程同时等待多个 fd 的事件。

```c
select(maxfd + 1, &rset, &wset, &eset, NULL);
```

面试回答：

> `select` 可以让一个线程同时监听多个 fd 的可读、可写和异常事件。哪个 fd 就绪，程序就处理哪个 fd。

### 5.2 `fd_set` 是什么

`fd_set` 可以理解为一个 bitset。

```text
第 3 位为 1 -> 监听 fd 3
第 4 位为 1 -> 监听 fd 4
```

常用宏：

- `FD_ZERO()`：清空集合。
- `FD_SET()`：把 fd 加入集合。
- `FD_CLR()`：把 fd 从集合移除。
- `FD_ISSET()`：判断 fd 是否在集合中。

### 5.3 `FD_CLR` 和 `close` 的区别

```c
FD_CLR(i, &rdfs);
close(i);
```

- `FD_CLR(i, &rdfs)`：从应用层维护的 `fd_set` 监听集合中移除 fd，本质是清掉 bit。
- `close(i)`：真正关闭内核里的 fd，释放 socket 资源。

顺序建议：

```c
FD_CLR(i, &rdfs);
close(i);
```

原因：

- 先 `FD_CLR`，下一轮 `select` 不再监听该 fd。
- 再 `close`，真正关闭 socket。
- 如果只 `FD_CLR` 不 `close`，会造成 fd 泄漏。
- 如果只 `close` 不 `FD_CLR`，`select` 可能监听已关闭 fd，导致 `EBADF`。

面试回答：

> `FD_CLR` 是应用层监听集合的清除，`close` 是操作系统层面的 fd 关闭。二者都要做，一般先从集合删除，再关闭 fd。

### 5.4 select 的性能缺陷

核心缺陷：

```text
1. copy
2. 遍历
```

展开：

- 每次调用前，应用要复制监听集合，例如 `rset = rdfs`。
- 每次系统调用，用户态 fd_set 要拷贝到内核态。
- 返回时，内核还要把就绪结果拷贝回用户态。
- 内核需要遍历 fd 集合检查就绪状态。
- 应用层返回后还要遍历 fd 集合，用 `FD_ISSET` 找到就绪 fd。
- `fd_set` 有 `FD_SETSIZE` 限制，常见默认 1024。

面试回答：

> `select` 的性能问题主要是每次都要拷贝 fd 集合，并且内核和用户态都要线性遍历 fd 集合。连接数大时，即使只有少量 fd 就绪，也要扫描大量无效 fd。

## 6. poll 核心八股

### 6.1 poll 做什么

`poll` 和 `select` 一样也是 IO 多路复用，但它不用 `fd_set`，而是用 `struct pollfd` 数组。

```c
struct pollfd {
    int fd;
    short events;
    short revents;
};
```

- `fd`：要监听的文件描述符。
- `events`：用户关心的事件。
- `revents`：内核返回的实际发生事件。

### 6.2 `fds[nfds]` 和 `fds[fd]` 的区别

标准紧凑写法：

```c
fds[nfds].fd = sockfd;
fds[nfds].events = POLLIN;
nfds++;
poll(fds, nfds, -1);
```

含义：

```text
fds 数组按连续下标保存有效连接
nfds 表示当前有效元素个数
```

类 select 写法：

```c
fds[sockfd].fd = sockfd;
fds[sockfd].events = POLLIN;
int maxfd = sockfd;
poll(fds, maxfd + 1, -1);
```

含义：

```text
直接用 fd 的值当数组下标
```

对比：

| 写法 | 数组下标含义 | poll 第二个参数 | 是否紧凑 | 推荐度 |
|---|---|---|---|---|
| `fds[nfds]` | 第几个有效连接 | `nfds` | 是 | 推荐 |
| `fds[fd]` | fd 数字本身 | `maxfd + 1` | 否 | 不推荐 |

面试回答：

> `poll` 本身遍历的是 pollfd 数组，所以更适合用 `nfds` 管理连续数组。用 fd 当数组下标会产生空洞，fd 值很大但连接少时会浪费扫描成本。

### 6.3 poll 删除 fd 的两种方式

方式一：紧凑删除。

```c
int fd = fds[i].fd;
fds[i] = fds[nfds - 1];
nfds--;
close(fd);
```

含义：

```text
把最后一个有效元素移动到当前位置
数组继续保持连续
poll 只扫描 [0, nfds)
```

方式二：原地标记。

```c
close(fds[i].fd);
fds[i].fd = -1;
fds[i].events = 0;
```

含义：

```text
不移动数组
把当前位置标记为无效
poll 会忽略 fd < 0 的元素
```

当前代码注意点：

```c
fds[i] = fds[nfds - 1];
nfds--;
close(fds[i].fd);
```

这段顺序有风险，因为 `fds[i]` 已经被最后一个元素覆盖，之后 `close(fds[i].fd)` 关闭的可能不是原来要关闭的 fd。正确做法是先保存原 fd 或先 close 原 fd。

面试回答：

> poll 删除 fd 可以压缩数组，也可以把 fd 置为 -1。压缩数组性能更好，但要先保存原 fd 再覆盖，否则可能关闭错 fd。

## 7. epoll 核心八股

### 7.1 epoll 三个核心阶段

```c
epoll_create();
epoll_ctl();
epoll_wait();
```

对应关系：

```text
epoll_create
  -> 创建 epoll 实例
  -> 返回 epfd

epoll_ctl
  -> 注册、修改、删除 fd
  -> 维护内核中的关注列表

epoll_wait
  -> 等待事件发生
  -> 返回就绪事件列表
```

面试回答：

> `epoll_create` 创建 epoll 内核对象，`epoll_ctl` 把 fd 和事件注册进 epoll，`epoll_wait` 阻塞等待并返回已经就绪的事件列表。

### 7.2 epoll 的事件分流

epoll 里都叫可读事件，但含义要按 fd 类型区分。

```text
监听 fd 可读
  -> 有新连接
  -> accept()

客户端 fd 可读
  -> 有数据或连接关闭
  -> recv()
```

当前代码：

```c
if (events[i].data.fd == sockfd) {
    accept(...);
} else if (events[i].events & EPOLLIN) {
    recv(...);
}
```

面试回答：

> 服务端事件循环里必须区分监听 fd 和连接 fd。监听 fd 的 `EPOLLIN` 表示可以 `accept`，连接 fd 的 `EPOLLIN` 表示可以 `recv`。

### 7.3 epoll 为什么比 select/poll 高效

`select/poll`：

```text
每次调用都传入完整 fd 集合
每次返回后都要遍历完整集合
```

`epoll`：

```text
epoll_ctl 注册一次
内核长期维护关注列表
epoll_wait 直接返回就绪事件列表
```

面试回答：

> epoll 的优势在于 fd 关注列表由内核维护，不需要每次调用都传完整集合；同时 `epoll_wait` 返回的是就绪事件数组，不需要像 select/poll 一样全量扫描所有 fd。

### 7.4 epoll 有没有 mmap

`mmap` 是内存映射，作用是把文件、设备或共享内存映射到进程虚拟地址空间，使程序可以像访问内存一样访问它。

关于 epoll：

```text
标准 Linux epoll 用户态 API 没有让用户 mmap 事件队列
epoll_wait 返回事件时，仍然会把就绪事件 copy 到用户传入的 events 数组
```

面试回答：

> epoll 不是通过 mmap 把就绪事件队列映射给用户态。它减少的是每次传入完整 fd 集合的开销，但 `epoll_wait` 返回事件时仍然要把就绪事件从内核拷贝到用户态。

## 8. LT 和 ET 八股

### 8.1 LT 水平触发

LT 是 epoll 默认模式。

```text
只要 fd 还有数据没读完
epoll_wait 下次还会继续通知
```

示例：

```c
ev.events = EPOLLIN;
```

面试回答：

> LT 是水平触发，只要条件仍然满足，比如接收缓冲区还有数据，epoll 就会反复通知。它更宽容，适合分阶段读取和状态机解析。

### 8.2 ET 边缘触发

ET 只在状态变化时通知。

```text
无数据 -> 有数据
  -> 通知一次

还有数据但状态没变化
  -> 不一定继续通知
```

示例：

```c
ev.events = EPOLLIN | EPOLLET;
```

面试回答：

> ET 是边缘触发，只在状态变化时通知一次。使用 ET 时必须把 fd 设置为非阻塞，并在一次事件中循环读到 `EAGAIN`，否则可能还有数据留在内核缓冲区，但 epoll 不再提醒。

### 8.3 LT 和 ET 对比

| 模式 | 触发条件 | 没读完会怎样 | 使用要求 | 适合场景 |
|---|---|---|---|---|
| LT | 只要条件满足就通知 | 下次继续通知 | 简单 | 普通网络服务、状态机解析 |
| ET | 状态变化时通知 | 可能不再通知 | 非阻塞，读到 EAGAIN | 高并发、减少重复通知 |

一句话背诵：

> LT 是没处理完就一直提醒，ET 是状态变化提醒一次，所以 ET 必须非阻塞并一次读到 EAGAIN。

### 8.4 ET 的使用场景

ET 优化的是事件通知路径。

```text
LT:
  缓冲区还有数据
  -> 反复 epoll_wait 返回同一个 fd

ET:
  事件来了通知一次
  -> 程序循环 recv 到 EAGAIN
```

注意：

- ET 减少的是重复事件通知。
- ET 不减少真正的数据读取成本。
- `recv()` 仍然要把数据从内核缓冲区拷贝到用户态。

面试回答：

> ET 适合高并发非阻塞服务器，用来减少同一个 fd 的重复通知。它优化的是事件通知次数，不是消除 `recv/read` 的数据拷贝。

## 9. 非阻塞 IO 八股

### 9.1 如何设置非阻塞

使用 `fcntl()`：

```c
#include <fcntl.h>

int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

使用：

```c
set_nonblock(sockfd);
set_nonblock(clientfd);
```

面试回答：

> 设置非阻塞通常用 `fcntl` 先获取原 flags，再加上 `O_NONBLOCK` 写回。ET 模式必须配合非阻塞 fd，否则循环读到没数据时会阻塞整个事件循环。

### 9.2 ET 下 recv 的标准模型

```c
while (1) {
    int n = recv(fd, buf, sizeof(buf), 0);

    if (n > 0) {
        // 处理数据
    } else if (n == 0) {
        close(fd);
        break;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        close(fd);
        break;
    }
}
```

一句话背诵：

> ET 收到一次可读通知后，必须循环 `recv`，直到读出 `EAGAIN`，才说明当前内核接收缓冲区被读空。

## 10. TCP 粘包和状态机八股

### 10.1 TCP 为什么有粘包

TCP 是字节流协议。

```text
它保证可靠、有序、不丢、不重复
但不保证一次 send 对应一次 recv
```

可能出现：

```text
send("hello")
send("world")

recv() 可能收到：
  hello
  world

也可能收到：
  helloworld
```

面试回答：

> TCP 没有消息边界，所谓粘包和拆包，本质是应用层协议没有定义清楚消息边界。

### 10.2 粘包解决方案

常见三种：

```text
1. 固定长度
2. 分隔符
3. 长度字段
```

对比：

| 方案 | 思路 | 优点 | 缺点 |
|---|---|---|---|
| 固定长度 | 每条消息固定 N 字节 | 解析简单 | 浪费空间，不灵活 |
| 分隔符 | 用 `\n` 或 `\r\n` 结尾 | 适合文本协议 | 内容里有分隔符要转义 |
| 长度字段 | 包头记录 body 长度 | 通用，适合二进制 | 要维护缓冲区和状态 |

### 10.3 长度字段协议

协议格式：

```text
[2字节 length][length 字节 body]
```

示例代码：

```c
short length = 0;
recv(fd, &length, 2, 0);

length = ntohs(length);
recv(fd, buffer, length, 0);
```

注意：

- 这只是演示思路。
- `recv(fd, &length, 2, 0)` 不保证一次读满 2 字节。
- `recv(fd, buffer, length, 0)` 不保证一次读满 body。
- 工程里要用缓冲区和状态机处理半包。

### 10.4 `ntohs(length)` 是什么

```c
length = ntohs(length);
```

含义：

```text
network to host short
把 2 字节整数从网络字节序转换成本机字节序
```

发送时对应：

```c
short net_len = htons(length);
```

面试回答：

> 网络字节序统一使用大端，而主机字节序可能是小端，所以收到网络中的 2 字节长度字段后，要用 `ntohs` 转成本机能正确理解的整数。

### 10.5 什么是状态机

状态机就是记录当前任务执行到哪个阶段，下次事件来了从上次位置继续。

对于协议：

```text
[2字节 length][length 字节 body]
```

可以拆成两个状态：

```text
STATE_READ_LENGTH
  -> 正在读 2 字节长度头

STATE_READ_BODY
  -> 正在读 length 字节正文
```

连接对象需要保存：

```c
struct conn {
    int fd;
    int state;
    int read_bytes;
    unsigned short length;
    char len_buf[2];
    char buffer[1024];
};
```

状态推进：

```text
STATE_READ_LENGTH
  ├─ length 没读够
  │  └─ 保存 read_bytes，下次继续
  └─ length 读够
     ├─ ntohs 解析长度
     ├─ read_bytes = 0
     └─ 切换到 STATE_READ_BODY

STATE_READ_BODY
  ├─ body 没读够
  │  └─ 保存 read_bytes，下次继续
  └─ body 读够
     ├─ handle_packet()
     ├─ read_bytes = 0
     └─ 切回 STATE_READ_LENGTH
```

面试回答：

> 状态机就是给每个连接保存协议解析进度。TCP 数据可能分多次到达，状态机可以记录当前是在读包头还是包体、已经读了多少、还差多少，从而在下一次可读事件中继续解析。

### 10.6 为什么状态机适合 LT

LT 的特点：

```text
只要缓冲区还有数据没处理完
epoll_wait 下次还会继续通知
```

所以状态机可以：

```text
这次只读到半个 length
  -> 保存状态，返回
  -> 下次 LT 继续通知

这次只读到部分 body
  -> 保存 body 进度，返回
  -> 下次继续读剩余 body
```

面试回答：

> 状态机适合 LT，因为 LT 没读完会继续通知，程序可以分阶段读取并保存进度。ET 也能用状态机，但必须在一次通知里把内核缓冲区读到 `EAGAIN`，否则容易漏事件。

### 10.7 Redis 协议和 LT

Redis 使用 RESP 协议，属于典型的输入缓冲区加状态机解析。

示例：

```text
*3\r\n
$3\r\n
SET\r\n
$4\r\n
name\r\n
$3\r\n
tom\r\n
```

解析过程：

```text
读数组参数个数
  -> 读 bulk string 长度
  -> 读 bulk string 内容
  -> 继续下一个参数
  -> 命令完整后执行
```

面试回答：

> Redis 协议天然适合 LT 加状态机。因为命令可能半包到达，Redis 会把数据读入输入缓冲区，再按 RESP 协议逐步解析。命令不完整时保存状态，等下次可读事件继续。

## 11. epoll 线程安全和 Reactor 八股

### 11.1 epoll 是否线程安全

更准确的问法是分三阶段看：

```text
epoll_create
  -> 谁创建 epfd，谁持有 epfd

epoll_ctl
  -> 谁添加、修改、删除 fd
  -> 谁负责 close fd

epoll_wait
  -> 几个线程等待事件
  -> 同一个 fd 会不会被多个线程同时处理
```

面试回答：

> epoll 系统调用本身在内核层面是线程安全的，但业务上不等于随便多线程操作都安全。真正要关注的是同一个 fd 的注册、事件处理、删除和关闭是否存在竞态。

### 11.2 多线程同时 epoll_wait

多个线程可以同时阻塞在同一个 `epfd` 上：

```c
epoll_wait(epfd, events, 1024, -1);
```

风险：

```text
线程 A 处理 fd 10
线程 B 也处理 fd 10
```

可能导致：

- 两个线程同时 `recv` 同一个连接。
- 业务数据顺序混乱。
- 一个线程 close fd，另一个线程还在使用 fd。

常见解决：

- 一个 Reactor 线程统一 `epoll_wait` 和 IO。
- 多线程共享 epoll 时使用 `EPOLLONESHOT`。
- 给连接对象加锁。
- fd 生命周期由一个线程统一管理。

### 11.3 pthread_create 放在 epoll_create 后是什么情况

如果代码类似：

```c
int epfd = epoll_create(1);
// pthread_create(...)
epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);
```

含义通常是：

```text
创建一个专门负责 epoll_wait 的 IO 线程
```

更清晰的顺序通常是：

```text
epoll_create
  -> epoll_ctl 注册监听 fd
  -> pthread_create 启动 epoll_wait 线程
```

面试回答：

> 在 epoll 初始化阶段创建线程，通常表示把事件循环交给独立 IO 线程，也就是 Reactor 线程。需要注意 epfd、fd 注册和关闭的归属，避免多个线程同时处理同一个 fd。

### 11.4 常见 Reactor 模型

模型一：单线程 Reactor。

```text
一个线程负责：
epoll_wait
accept
recv
业务处理
send
epoll_ctl
close
```

优点：简单。  
缺点：业务慢会阻塞 IO。

模型二：IO 线程加 worker 线程池。

```text
IO 线程：
epoll_wait
accept
recv
send
epoll_ctl
close

worker 线程：
业务计算
命令执行
```

关键点：

```text
worker 尽量不直接 close fd
worker 把结果交回 IO 线程
IO 线程统一管理 fd 生命周期
```

模型三：多线程共享同一个 epfd。

```text
线程 A：epoll_wait(epfd)
线程 B：epoll_wait(epfd)
线程 C：epoll_wait(epfd)
```

关键点：

```text
要避免同一个 fd 被多个线程同时处理
常配合 EPOLLONESHOT 或连接级锁
```

模型四：多 Reactor。

```text
Main Reactor
  -> 监听 sockfd
  -> accept 新连接
  -> 把 clientfd 分配给 Sub Reactor

Sub Reactor 1
  -> epoll_wait(epfd1)
  -> 管理一批 clientfd

Sub Reactor 2
  -> epoll_wait(epfd2)
  -> 管理另一批 clientfd
```

面试回答：

> 多 Reactor 不是主要针对多协议，而是为了把大量连接分散到多个 IO 线程上。一个连接通常只归一个 Reactor 管，避免多个线程同时处理同一个 fd。

## 12. select、poll、epoll 对比八股

| 模型 | 数据结构 | fd 数量限制 | 是否每次传完整集合 | 返回后是否全量扫描 | 适合场景 |
|---|---|---|---|---|---|
| select | `fd_set` 位图 | 有，常见 1024 | 是 | 是 | 少量 fd，教学 demo |
| poll | `pollfd` 数组 | 无固定 1024 限制 | 是 | 是 | 中等连接，兼容性场景 |
| epoll | 内核关注列表和就绪队列 | 无 select 的 fd_set 限制 | 否 | 否，只遍历就绪事件 | Linux 高并发 |

面试回答：

> `select` 用位图，有 fd 数量限制，并且每次都要拷贝和扫描。`poll` 用数组，解决了固定 1024 限制，但仍然要每次传数组和线性扫描。`epoll` 把 fd 注册到内核，通过 `epoll_wait` 返回就绪事件列表，减少全量拷贝和全量扫描，更适合高并发。

## 13. 当前代码风险点

### 13.1 头文件不完整

当前代码只显式包含：

```c
#include <stdio.h>
```

真实编译通常还需要：

```c
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
```

如果使用线程：

```c
#include <pthread.h>
```

如果使用非阻塞：

```c
#include <fcntl.h>
```

### 13.2 没有处理 accept 失败

当前：

```c
int clientfd = accept(...);
epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
```

风险：

```text
accept 可能返回 -1
后续把 -1 注册进 epoll 会出错
```

建议：

```c
if (clientfd < 0) {
    perror("accept");
    continue;
}
```

### 13.3 没有处理 recv 小于 0

当前：

```c
if (count == 0) {
    epoll_ctl(...);
    close(...);
} else {
    send(...);
}
```

风险：

```text
recv 返回 -1 时会进入 else
send 的长度参数会收到错误值
```

建议区分：

```text
count > 0
count == 0
count < 0
```

### 13.4 没有处理 send 部分发送

`send()` 不保证一次发完所有数据，工程代码需要维护输出缓冲区，处理部分发送和 `EAGAIN`。

### 13.5 当前 epoll 是 LT，不是 ET

当前代码：

```c
ev.events = EPOLLIN;
```

这是默认 LT。

如果改成 ET：

```c
ev.events = EPOLLIN | EPOLLET;
```

必须同时：

```text
fd 设置非阻塞
accept 循环到 EAGAIN
recv 循环到 EAGAIN
send 处理部分发送
```

### 13.6 循环后代码不可达

当前代码末尾：

```c
while (1) {
    ...
}

getchar();
close(clientfd);
```

问题：

- `while(1)` 没有退出条件，后面代码通常不可达。
- `clientfd` 是局部变量，循环外不可直接使用。

## 14. 高频面试问答

### Q1：当前代码是什么模型？

答：

当前代码是单线程 epoll 模型的 TCP echo server。它用 `epoll_create` 创建 epoll 实例，用 `epoll_ctl` 注册监听 fd 和客户端 fd，用 `epoll_wait` 等待就绪事件。监听 fd 可读时 `accept`，客户端 fd 可读时 `recv/send`。

### Q2：epoll 的三个核心函数是什么？

答：

`epoll_create` 创建 epoll 实例，`epoll_ctl` 注册、修改或删除 fd，`epoll_wait` 等待事件并返回就绪事件列表。

### Q3：监听 fd 的 EPOLLIN 和客户端 fd 的 EPOLLIN 有什么区别？

答：

监听 fd 的 `EPOLLIN` 表示有新连接到来，需要调用 `accept`。客户端 fd 的 `EPOLLIN` 表示有数据可读或连接关闭，需要调用 `recv`。

### Q4：select 的性能缺陷是什么？

答：

核心是 copy 和遍历。每次调用都要把 fd 集合从用户态拷贝到内核态，返回时再拷贝结果；内核要扫描集合，用户态还要扫描集合找就绪 fd。同时 `fd_set` 还有数量限制。

### Q5：poll 相比 select 改进了什么，又没改进什么？

答：

poll 用 `pollfd` 数组替代 `fd_set`，没有固定的 1024 位图限制。但它仍然需要每次传入完整数组，返回后仍然要线性扫描，所以性能问题没有根本解决。

### Q6：epoll 为什么更适合高并发？

答：

epoll 通过 `epoll_ctl` 把 fd 关注列表维护在内核里，不需要每次调用都传完整集合。`epoll_wait` 返回的是就绪事件列表，应用只遍历就绪事件，不用全量扫描所有 fd。

### Q7：epoll 有没有 mmap？

答：

标准 epoll 用户态接口没有通过 mmap 映射事件队列。`epoll_wait` 返回事件时，内核仍然会把就绪事件拷贝到用户态的 `events` 数组。epoll 优化的是避免每次传完整 fd 集合和全量扫描。

### Q8：LT 和 ET 的区别？

答：

LT 是水平触发，只要 fd 仍然满足条件就会反复通知。ET 是边缘触发，只在状态变化时通知一次。ET 必须配合非阻塞 IO，并在一次事件中读到 `EAGAIN`。

### Q9：为什么 ET 必须设置非阻塞？

答：

ET 要求循环读到 `EAGAIN`。如果 fd 是阻塞的，数据读完后下一次 `recv` 会阻塞，导致整个事件循环卡住。非阻塞 fd 在没数据时会返回 `-1` 和 `EAGAIN`，程序才能停止本轮读取。

### Q10：TCP 粘包是什么？

答：

TCP 是字节流协议，没有消息边界。一次 `send` 不一定对应一次 `recv`，多次 `send` 可能被一次 `recv` 收到，一次 `send` 也可能被多次 `recv` 收到。这就是粘包和拆包问题。

### Q11：如何解决 TCP 粘包？

答：

常见方案有固定长度、分隔符和长度字段。工程中常用长度字段，即包头记录 body 长度，接收端先读包头，再按长度读取完整 body。

### Q12：`ntohs(length)` 是什么？

答：

`ntohs` 是 network to host short，把网络字节序的 2 字节整数转换成本机字节序。网络字节序统一是大端，所以长度字段从网络读到本机后要转换。

### Q13：什么是状态机？

答：

状态机就是记录当前处理到哪个阶段。比如长度字段协议可以分成读 length 和读 body 两个状态，每个连接保存状态、已读字节数和目标长度。数据没读完整时保存进度，下次事件来了继续处理。

### Q14：为什么状态机适合 LT？

答：

LT 没读完会继续通知，所以状态机可以分阶段读取。读 length 没够就保存进度，读 body 没够也保存进度，下次可读事件继续推进。ET 也能做，但必须一次读到 `EAGAIN`。

### Q15：Redis 协议为什么适合 LT？

答：

Redis RESP 协议是分阶段解析的，命令可能分多次到达。Redis 会把数据放入输入缓冲区，用状态机逐步解析参数个数、参数长度和参数内容。LT 模式下没读完会继续通知，适合这种解析模型。

### Q16：epoll 是否线程安全？

答：

epoll 系统调用本身在内核层面是线程安全的，但业务使用不一定安全。真正要看 `epoll_ctl`、`epoll_wait`、`close` 和 fd 处理是否存在竞态，尤其是同一个 fd 是否会被多个线程同时处理。

### Q17：多线程同时 epoll_wait 可以吗？

答：

可以，但要防止多个线程同时处理同一个 fd。常见做法是使用 `EPOLLONESHOT`、连接级锁，或者采用单 Reactor 加 worker 线程池，让 IO 线程统一管理 fd。

### Q18：多 Reactor 是为了多协议吗？

答：

不是。多 Reactor 主要是为了多线程和多核扩展，把大量连接分散到多个 IO 线程。多个协议只是 handler 不同，不是多 Reactor 的本质。

## 15. 一句话总背诵

> 这份代码从阻塞 TCP server 发展到 epoll 多路复用模型。核心是监听 fd 和连接 fd 都注册到 epoll，`epoll_wait` 返回就绪事件后，监听 fd 执行 `accept`，连接 fd 执行 `recv/send`。select 和 poll 的问题在于每次都要传完整集合并线性扫描，epoll 通过内核维护关注列表和返回就绪事件列表减少开销。实际工程还要处理 LT/ET、非阻塞、TCP 粘包、状态机解析、send 部分发送和多线程 fd 生命周期管理。

## 16. 复习顺序

```text
1. 背 TCP server 标准流程
2. 背 sockfd 和 clientfd 的区别
3. 背 recv 返回值
4. 背 select 的 fd_set 和性能缺陷
5. 背 poll 的 pollfd 数组和删除方式
6. 背 epoll_create/epoll_ctl/epoll_wait
7. 背监听 fd 可读和客户端 fd 可读的区别
8. 背 LT/ET 和非阻塞
9. 背 TCP 粘包三种解决方案
10. 背长度字段协议和状态机
11. 背 Redis 协议为什么适合 LT
12. 背 epoll 线程安全和 Reactor 模型
```
