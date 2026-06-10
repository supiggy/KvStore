#include "net/server.h"
#include "common/minivec.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/epoll.h>

/* ============================================================================
 * 网络层  ——  直接照搬 kvstore 的 epoll_entry.c
 * ----------------------------------------------------------------------------
 * 这一层你在 kvstore 已经吃透，所以这里是"搬运 + 标注改动"，不是新东西。
 *
 * 对应关系（kvstore 文件: epoll_entry.c / kvstore.h）：
 *   kvstore                         MiniVec                       说明
 *   --------------------------------------------------------------------------
 *   struct conn_item (kvstore.h)    struct conn (本文件)          一样的 fd+读写缓冲+回调
 *   RCALLBACK                       mv_callback_t                 一样的回调函数指针类型
 *   connlist[1048576] (全局)        connlist[MV_MAX_CONN] (全局)  一样按 fd 下标存连接
 *   set_event / accept_cb /         同名函数                      逻辑一字不改
 *     recv_cb / send_cb
 *   init_server / epoll_entry       init_server /                 同一套 socket→bind→listen→loop
 *                                     minivec_server_start
 *   kvstore_request(&connlist[fd])  minivec_handle_command(...)   ★ 唯一的业务分发改动
 *
 * 改动点（务必看清）：
 *   [改1] 业务分发：kvstore 在 recv_cb 里调 kvstore_request(item)（用全局引擎，
 *         读 item->rbuffer / 写 item->wbuffer）；MiniVec 改成
 *         minivec_handle_command(g_db, rbuffer, wbuffer, len)，把 db 显式传进去。
 *   [改2] 缓冲区：kvstore BUFFER_LENGTH=512；MiniVec 用 MINIVEC_BUFFER_LEN=8192，
 *         因为一条 VADD 命令含 128 个浮点文本，512 根本装不下。
 *   [改3] db 注入：kvstore 引擎是全局的；MiniVec 的 db 由 server_start 参数传入，
 *         本文件用一个 static 全局 g_db 承接，让回调函数能拿到它。
 *   [改4] 端口：kvstore demo 为压测开了 20 个端口(2048~2067)；MiniVec 简化成 1 个。
 *   [改5] recv 返回值：补了 count<0 的处理（kvstore 没处理，见 项目知识点.md 13.3）。
 * ============================================================================ */

/* 对应 kvstore RCALLBACK (kvstore.h) */
typedef int (*mv_callback_t)(int fd);

/* [改2] 对应 kvstore connlist[1048576]；因单连接缓冲变大(8192*2)，这里缩小上限 */
#define MV_MAX_CONN 4096

/* 对应 kvstore struct conn_item：同样 fd + 读写缓冲 + 回调，仅 buffer 变大 */
struct conn {
    int  fd;
    char rbuffer[MINIVEC_BUFFER_LEN];
    int  rlen;
    char wbuffer[MINIVEC_BUFFER_LEN];
    int  wlen;
    /* 监听 fd 用 accept_callback，连接 fd 用 recv_callback，二者共用一块内存（union），
     * 这正是 kvstore 那个"主循环统一调 recv_t.xxx_callback"的小技巧。 */
    union {
        mv_callback_t accept_callback;
        mv_callback_t recv_callback;
    } recv_t;
    mv_callback_t send_callback;
};

/* 对应 kvstore epoll_entry.c 里的全局 epfd / connlist */
static int epfd = 0;
static struct conn connlist[MV_MAX_CONN];

/* [改3] 对应 kvstore 的"全局引擎"。MiniVec 把 db 收在这里，供回调使用。 */
static minivec_db_t *g_db = NULL;

/* 前置声明：accept_cb 内部要引用 recv_cb / send_cb。
 * 对应 kvstore epoll_entry.c 顶部那三行 accept_cb/recv_cb/send_cb 声明。 */
static int accept_cb(int fd);
static int recv_cb(int fd);
static int send_cb(int fd);

/* set_event —— 与 kvstore 完全一致：flag=1 添加事件，flag=0 修改事件 */
static int set_event(int fd, int event, int flag) {
    struct epoll_event ev;
    ev.events  = event;
    ev.data.fd = fd;
    if (flag) {
        return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
    } else {
        return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
    }
}

/* accept_cb —— 对应 kvstore accept_cb：监听 fd 可读 = 有新连接 */
static int accept_cb(int fd) {
    struct sockaddr_in clientaddr;
    socklen_t len = sizeof(clientaddr);

    int clientfd = accept(fd, (struct sockaddr *)&clientaddr, &len);
    if (clientfd < 0) {
        return -1;
    }
    /* MiniVec 安全补丁：kvstore 直接 connlist[clientfd]，fd 超界会越界写。
     * 连接数受 MV_MAX_CONN 限制，超了就拒绝。 */
    if (clientfd >= MV_MAX_CONN) {
        close(clientfd);
        return -1;
    }

    set_event(clientfd, EPOLLIN, 1);

    /* 初始化该连接：清空读写缓冲、挂上 recv/send 回调（与 kvstore 一致） */
    connlist[clientfd].fd   = clientfd;
    memset(connlist[clientfd].rbuffer, 0, MINIVEC_BUFFER_LEN);
    connlist[clientfd].rlen = 0;
    memset(connlist[clientfd].wbuffer, 0, MINIVEC_BUFFER_LEN);
    connlist[clientfd].wlen = 0;
    connlist[clientfd].recv_t.recv_callback = recv_cb;
    connlist[clientfd].send_callback        = send_cb;

    return clientfd;
}

/* recv_cb —— 对应 kvstore recv_cb：连接 fd 可读 = 收数据 */
static int recv_cb(int fd) {
    char *buffer = connlist[fd].rbuffer;
    int   idx    = connlist[fd].rlen;

    int count = recv(fd, buffer + idx, MINIVEC_BUFFER_LEN - idx, 0);
    if (count == 0) {
        /* 对端关闭：DEL + close，与 kvstore 一致 */
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
    /* [改5] kvstore 没处理 count<0；非阻塞下 EAGAIN 表示读空，其它错误则断开 */
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
    connlist[fd].rlen += count;

    /* ★[改1] 业务分发：这是与 kvstore 唯一不同的一行。
     * kvstore:  kvstore_request(&connlist[fd]);   // 全局引擎，读写都在 item 里
     * MiniVec:  把读缓冲当命令行、写缓冲当响应，连同 db 一起交给协议层。 */
    minivec_handle_command(g_db, connlist[fd].rbuffer,
                           connlist[fd].wbuffer, MINIVEC_BUFFER_LEN);
    connlist[fd].wlen = (int)strlen(connlist[fd].wbuffer);

    /* 处理完一条命令清空读缓冲，否则下一条会和旧数据粘连（与 kvstore 注释同因）。
     * 同样假设"一次 recv = 一条完整命令"，生产环境要上粘包状态机（项目知识点.md 第10节）。 */
    connlist[fd].rlen = 0;
    memset(connlist[fd].rbuffer, 0, MINIVEC_BUFFER_LEN);

    /* 切到关注可写，待会儿在 send_cb 里把响应发出去（与 kvstore 一致） */
    set_event(fd, EPOLLOUT, 0);
    return count;
}

/* send_cb —— 与 kvstore 一致：发完响应切回关注可读 */
static int send_cb(int fd) {
    int count = send(fd, connlist[fd].wbuffer, connlist[fd].wlen, 0);
    set_event(fd, EPOLLIN, 0);
    return count;
}

/* init_server —— 对应 kvstore init_server：socket→bind→listen */
static int init_server(unsigned short port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    /* MiniVec 小改进：开 SO_REUSEADDR，方便反复重启调试（kvstore 没开） */
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    listen(sockfd, 10);
    return sockfd;
}

/* minivec_server_start —— 对应 kvstore epoll_entry()
 * 改动：单端口；db 用参数注入（存进 g_db）。主循环逻辑与 kvstore 一字不差。 */
int minivec_server_start(int port, minivec_db_t *db) {
    g_db = db;                       /* [改3] 承接注入的 db */

    epfd = epoll_create(1);
    if (epfd < 0) {
        perror("epoll_create");
        return -1;
    }

    int sockfd = init_server((unsigned short)port);   /* [改4] 单端口 */
    if (sockfd < 0 || sockfd >= MV_MAX_CONN) {
        return -1;
    }
    connlist[sockfd].fd = sockfd;
    connlist[sockfd].recv_t.accept_callback = accept_cb;  /* 监听 fd 挂 accept 回调 */
    set_event(sockfd, EPOLLIN, 1);

    /* 主循环 —— 与 kvstore epoll_entry 的 while(1) 完全一致：
     * 可读就调 recv_t.recv_callback（监听 fd 是 accept_cb，连接 fd 是 recv_cb），
     * 可写就调 send_callback。 */
    struct epoll_event events[1024] = {0};
    while (1) {
        int nready = epoll_wait(epfd, events, 1024, -1);
        for (int i = 0; i < nready; i++) {
            int connfd = events[i].data.fd;
            if (events[i].events & EPOLLIN) {
                connlist[connfd].recv_t.recv_callback(connfd);
            } else if (events[i].events & EPOLLOUT) {
                connlist[connfd].send_callback(connfd);
            }
        }
    }
    return 0;
}
