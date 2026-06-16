#define _GNU_SOURCE          /* 暴露 pipe/fcntl/read/write 等 POSIX/GNU 接口 */

#include "net/server.h"
#include "common/minivec.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>

/* ============================================================================
 * 网络层 —— G4:主从 Reactor(multi-reactor / one-loop-per-thread)
 * ----------------------------------------------------------------------------
 * 从 G0~G3 的【单 reactor 单线程】升级为【主从多线程】:
 *
 *   原来(单线程):  一个 epoll 既 accept 又收发,HNSW 操作阻塞整个事件循环。
 *   现在(主从):
 *       主线程   : 只 accept,把新连接【轮流】投递给某个 worker。
 *       worker×N : 各自一个 epoll,只服务分给自己的连接,调 minivec_handle_command。
 *                  因为多个 worker 会并发碰同一个 db,所以引擎必须线程安全 —— 那是 G3。
 *
 * 跨线程怎么把"新连接"交给 worker?
 *   每个 worker 有一根管道(pipe)。主线程 accept 到 connfd 后,把这个整数【写进】
 *   目标 worker 的管道;worker 的 epoll 监听着管道读端,被唤醒后【读出】connfd 并
 *   注册到自己的 epoll。—— 不直接跨线程 epoll_ctl 别人的 epoll,而是把"事件"投递给
 *   属主线程自己处理,这就是 one-loop-per-thread 解耦的经典做法。
 *
 * ★ G4 两处留白(你填,见下方 worker_loop 的留白 B 和 server_start 的留白 A):
 *     A. 主线程把 connfd 写进 worker 的管道
 *     B. worker 从管道读出 connfd 并 worker_register
 *   没填 A/B:连接能 accept 但无人接管(不会被服务)。填好后才真正跑起来。
 *   另:多线程下务必先填 G3 的读写锁,否则数据竞争。
 *
 * 改动点(相对单线程版):
 *   [改A] set_event 多了 epfd 参数(每个 worker 一个 epoll)。
 *   [改B] struct conn 多了 epfd 字段:连接记得自己属于哪个 worker 的 epoll,
 *         好在 recv/send 里重新挂 EPOLLIN/EPOLLOUT。
 *   [改C] accept 不再走回调,主线程直接阻塞 accept。
 * ============================================================================ */

typedef int (*mv_callback_t)(int fd);

#define MV_MAX_CONN     4096
#define MV_NUM_WORKERS  4

/* 一条连接:多了 epfd(所属 worker 的 epoll) */
struct conn {
    int  fd;
    int  epfd;                 /* [改B] 本连接所属 worker 的 epoll */
    char rbuffer[MINIVEC_BUFFER_LEN];
    int  rlen;
    char wbuffer[MINIVEC_BUFFER_LEN];
    int  wlen;
    union {
        mv_callback_t accept_callback;
        mv_callback_t recv_callback;
    } recv_t;
    mv_callback_t send_callback;
};

/* 全局按 fd 下标存连接(fd 全进程唯一,注册后只被属主 worker 读写,无需加锁) */
static struct conn connlist[MV_MAX_CONN];
static minivec_db_t *g_db = NULL;

/* 一个 worker(从 reactor) */
struct worker {
    int       id;
    int       epfd;            /* 自己的 epoll */
    int       pipe_r;          /* 主线程写 pipe_w,本线程从 pipe_r 读新连接 fd */
    int       pipe_w;
    pthread_t tid;
};
static struct worker workers[MV_NUM_WORKERS];

static int recv_cb(int fd);
static int send_cb(int fd);

/* [改A] set_event:带 epfd —— flag=1 添加事件,flag=0 修改事件 */
static int set_event(int epfd, int fd, int event, int flag) {
    struct epoll_event ev;
    ev.events  = event;
    ev.data.fd = fd;
    return flag ? epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)
                : epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}

/* recv_cb —— 收数据 + 行分帧(逻辑同单线程版,只是 set_event 用 c->epfd) */
static int recv_cb(int fd) {
    struct conn *c = &connlist[fd];
    int count = recv(fd, c->rbuffer + c->rlen, MINIVEC_BUFFER_LEN - c->rlen, 0);
    if (count == 0) {
        epoll_ctl(c->epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        epoll_ctl(c->epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        return -1;
    }
    c->rlen += count;

    /* 行分帧:按 '\n' 切完整命令逐条处理,半行留到下次 recv 续上 */
    char resp[MINIVEC_BUFFER_LEN];
    int start = 0;
    c->wlen = 0;
    for (int i = 0; i < c->rlen; i++) {
        if (c->rbuffer[i] != '\n') continue;
        c->rbuffer[i] = '\0';
        resp[0] = '\0';
        minivec_handle_command(g_db, c->rbuffer + start, resp, (int)sizeof(resp));
        int rl = (int)strlen(resp);
        if (c->wlen + rl + 1 < MINIVEC_BUFFER_LEN) {
            memcpy(c->wbuffer + c->wlen, resp, rl);
            c->wlen += rl;
            c->wbuffer[c->wlen++] = '\n';
        }
        start = i + 1;
    }
    int leftover = c->rlen - start;
    if (leftover > 0 && start > 0) memmove(c->rbuffer, c->rbuffer + start, leftover);
    c->rlen = leftover;

    if (c->wlen > 0) set_event(c->epfd, fd, EPOLLOUT, 0);
    return count;
}

/* send_cb —— 发完响应切回关注可读 */
static int send_cb(int fd) {
    struct conn *c = &connlist[fd];
    int count = send(fd, c->wbuffer, c->wlen, 0);
    set_event(c->epfd, fd, EPOLLIN, 0);
    return count;
}

/* 把新连接注册到 worker 的 epoll(已填,机械活)。
 * 由 worker 线程自己调用(在留白 B 里),所以 connlist[connfd] 只被属主线程写。 */
static void worker_register(struct worker *w, int connfd) {
    if (connfd < 0 || connfd >= MV_MAX_CONN) {
        if (connfd >= 0) close(connfd);
        return;
    }
    struct conn *c = &connlist[connfd];
    c->fd   = connfd;
    c->epfd = w->epfd;
    memset(c->rbuffer, 0, MINIVEC_BUFFER_LEN); c->rlen = 0;
    memset(c->wbuffer, 0, MINIVEC_BUFFER_LEN); c->wlen = 0;
    c->recv_t.recv_callback = recv_cb;
    c->send_callback        = send_cb;
    set_event(w->epfd, connfd, EPOLLIN, 1);
}

/* worker(从 reactor)主循环:自己的 epoll 上跑 recv/send,并接收主线程投递的新连接 */
static void *worker_loop(void *arg) {
    struct worker *w = (struct worker *)arg;
    struct epoll_event events[1024];
    while (1) {
        int nready = epoll_wait(w->epfd, events, 1024, -1);
        for (int i = 0; i < nready; i++) {
            int fd = events[i].data.fd;

            if (fd == w->pipe_r) {
                /* ★ 留白 B(G4):主线程把新连接 fd 写进管道,这里读出来注册到本 worker。
                 * 流程树:
                 *   int connfd;
                 *   while (read(w->pipe_r, &connfd, sizeof(connfd)) == (ssize_t)sizeof(connfd))
                 *       worker_register(w, connfd);
                 *   (pipe_r 非阻塞:一次可能积压多个 fd,循环读到读空为止)
                 * TODO(你填) */
                (void)worker_register;   /* 填完留白 B 后这行可删 */
                continue;
            }

            if (events[i].events & EPOLLIN)        connlist[fd].recv_t.recv_callback(fd);
            else if (events[i].events & EPOLLOUT)  connlist[fd].send_callback(fd);
        }
    }
    return NULL;
}

/* init_server —— socket→bind→listen(同单线程版) */
static int init_server(unsigned short port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family      = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("bind"); close(sockfd); return -1;
    }
    listen(sockfd, 128);
    return sockfd;
}

int minivec_server_start(int port, minivec_db_t *db) {
    g_db = db;

    int sockfd = init_server((unsigned short)port);
    if (sockfd < 0) return -1;

    /* 起 N 个 worker:各自 epoll + 一根接新连接的管道 */
    for (int i = 0; i < MV_NUM_WORKERS; i++) {
        struct worker *w = &workers[i];
        w->id   = i;
        w->epfd = epoll_create(1);
        if (w->epfd < 0) { perror("epoll_create"); return -1; }

        int pp[2];
        if (pipe(pp) < 0) { perror("pipe"); return -1; }
        w->pipe_r = pp[0];
        w->pipe_w = pp[1];
        fcntl(w->pipe_r, F_SETFL, O_NONBLOCK);       /* 非阻塞,便于循环读干净 */
        set_event(w->epfd, w->pipe_r, EPOLLIN, 1);   /* worker 监听管道读端 */

        pthread_create(&w->tid, NULL, worker_loop, w);
    }

    printf("[minivec] %d workers, listening on port %d\n", MV_NUM_WORKERS, port);

    /* 主线程:只 accept,然后把连接【轮流】(round-robin)分给 worker */
    int rr = 0;
    while (1) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);
        int connfd = accept(sockfd, (struct sockaddr *)&cli, &len);
        if (connfd < 0) continue;
        if (connfd >= MV_MAX_CONN) { close(connfd); continue; }

        struct worker *w = &workers[rr];
        rr = (rr + 1) % MV_NUM_WORKERS;

        /* ★ 留白 A(G4):把 connfd 交给 worker w —— 写进它的管道。
         * 流程树:
         *   write(w->pipe_w, &connfd, sizeof(connfd));
         *   worker 的 epoll 会因管道可读而醒,在留白 B 里 worker_register 它。
         * 为什么不在主线程直接 set_event(w->epfd, connfd,...)?
         *   跨线程改别人 epoll/连接状态容易竞争;把 fd "投递"给属主线程自己注册更干净。
         * TODO(你填) */
        (void)w;   /* 填完留白 A 后这行可删 */
        /* 提示:留白 A/B 都没填时,连接被 accept 后无人接管(也没人服务) */
    }
    return 0;
}
