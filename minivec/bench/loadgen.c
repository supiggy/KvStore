#define _GNU_SOURCE          /* clock_gettime / inet_pton / pthread */

#include "metrics.h"
#include "common/minivec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* ============================================================================
 * 并发压测客户端(loadgen)—— 验证 G3/G4,产出"并发下的 QPS / p99"
 * ----------------------------------------------------------------------------
 * 多个线程各开一条 TCP 连接,对 ./minivec 反复发命令,逐次记往返延迟,
 * 汇总成 QPS + p50/p99。这是 G0 单线程引擎基准之外的【端到端网络】数字,
 * 也是 G3/G4 那条 war story 的数字来源("并发起来 p99 还稳不稳")。
 *
 * 依赖(重要):
 *   - 先启动服务端:./minivec(且 G4 的留白 A/B、G3 的锁都已填,否则不服务/会竞争)。
 *   - p50/p99 复用 bench/metrics.c 的 percentile —— 没填它就显示 -1。
 *
 * 流程:
 *   1) populate:用一条连接 VADD 一批向量,让库里有数据(否则 VSEARCH 测的是空库)。
 *   2) load   :起 T 个线程,每线程一条连接,循环发 VSEARCH 并计时。
 *   3) report :合并所有延迟样本,算 QPS / p50 / p99。
 *
 * ★ 留白(你填):client_thread 里"一次请求-响应往返的计时"(见下方 TODO)。
 * 用法: ./run_loadgen [host] [port] [threads] [reqs/线程] [populate] [topk]
 *        默认       127.0.0.1 9097   8        1000        2000      10
 * ============================================================================ */

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

/* 连到服务端,返回 fd(失败 -1) */
static int connect_to(const char *host, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &a.sin_addr) <= 0) { close(fd); return -1; }
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}

/* 把 dim 个 [-1,1] 随机 float 追加进命令行(确定性,靠线程私有 seed) */
static int append_vec(char *buf, int cap, int off, int dim, unsigned int *st) {
    for (int i = 0; i < dim; i++) {
        unsigned int x = *st; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *st = x;
        float f = (float)(x & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
        off += snprintf(buf + off, cap - off, " %.4f", f);
    }
    off += snprintf(buf + off, cap - off, "\n");
    return off;
}

/* populate:一条连接灌 n 条 VADD(已填) */
static void populate(const char *host, int port, int n, int dim) {
    int fd = connect_to(host, port);
    if (fd < 0) { fprintf(stderr, "populate: connect failed\n"); return; }
    char cmd[MINIVEC_BUFFER_LEN], resp[1024];
    unsigned int st = 20240611u;
    for (int i = 0; i < n; i++) {
        int off = snprintf(cmd, sizeof(cmd), "VADD %d", i + 1);
        off = append_vec(cmd, sizeof(cmd), off, dim, &st);
        if (write(fd, cmd, (size_t)off) <= 0) break;
        if (recv(fd, resp, sizeof(resp), 0) <= 0) break;   /* 等 "OK" */
    }
    close(fd);
    printf("populate: VADD %d 条完成\n", n);
}

struct client_ctx {
    const char *host;
    int    port, requests, topk, dim;
    unsigned int seed;
    double *lat;     /* 长度 requests,记每次往返延迟(us) */
    int     ok;      /* 连接是否成功 */
};

static void *client_thread(void *arg) {
    struct client_ctx *c = (struct client_ctx *)arg;
    int fd = connect_to(c->host, c->port);
    if (fd < 0) { c->ok = 0; return NULL; }
    c->ok = 1;

    char cmd[MINIVEC_BUFFER_LEN], resp[MINIVEC_BUFFER_LEN];
    for (int i = 0; i < c->requests; i++) {
        int off = snprintf(cmd, sizeof(cmd), "VSEARCH %d", c->topk);
        off = append_vec(cmd, sizeof(cmd), off, c->dim, &c->seed);

        /* ★ 留白:计时一次完整的"请求 → 响应"往返。
         * 流程树:
         *   double t0 = now_us();
         *   if (write(fd, cmd, (size_t)off) <= 0) break;     // 发命令(已含结尾'\n')
         *   ssize_t n = recv(fd, resp, sizeof(resp), 0);     // 读响应(假设一次 recv 收齐)
         *   double t1 = now_us();
         *   c->lat[i] = t1 - t0;
         *   if (n <= 0) break;                               // 对端断开
         * 说明:这里简化为"一发一收";VSEARCH 多行响应通常一个 recv 能取完,
         *       严谨做法是读到约定的结束标记 —— 进阶再说。
         * TODO(你填) */
        (void)resp;
        c->lat[i] = -1.0;   /* 未填:占位 */
    }
    close(fd);
    return NULL;
}

int main(int argc, char **argv) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port     = (argc > 2) ? atoi(argv[2]) : MINIVEC_PORT;
    int threads  = (argc > 3) ? atoi(argv[3]) : 8;
    int requests = (argc > 4) ? atoi(argv[4]) : 1000;
    int pop      = (argc > 5) ? atoi(argv[5]) : 2000;
    int topk     = (argc > 6) ? atoi(argv[6]) : 10;
    int dim      = MINIVEC_DIM;

    printf("loadgen: %s:%d  threads=%d reqs/线程=%d populate=%d topk=%d\n",
           host, port, threads, requests, pop, topk);

    if (pop > 0) populate(host, port, pop, dim);

    struct client_ctx *ctx = calloc((size_t)threads, sizeof(*ctx));
    pthread_t *tid = calloc((size_t)threads, sizeof(*tid));
    for (int t = 0; t < threads; t++) {
        ctx[t].host = host; ctx[t].port = port;
        ctx[t].requests = requests; ctx[t].topk = topk; ctx[t].dim = dim;
        ctx[t].seed = 1000u + (unsigned int)t;          /* 每线程不同 seed */
        ctx[t].lat  = malloc(sizeof(double) * (size_t)requests);
    }

    double t0 = now_us();
    for (int t = 0; t < threads; t++) pthread_create(&tid[t], NULL, client_thread, &ctx[t]);
    for (int t = 0; t < threads; t++) pthread_join(tid[t], NULL);
    double elapsed = now_us() - t0;

    /* 合并所有延迟样本 */
    int total = threads * requests, m = 0;
    double *all = malloc(sizeof(double) * (size_t)total);
    int connected = 0;
    for (int t = 0; t < threads; t++) {
        if (!ctx[t].ok) continue;
        connected++;
        for (int i = 0; i < requests; i++) all[m++] = ctx[t].lat[i];
    }

    double qps = (elapsed > 0) ? (double)(connected * requests) / (elapsed / 1e6) : 0;
    printf("\n连接成功线程: %d/%d   总请求: %d   墙钟: %.1f ms\n",
           connected, threads, connected * requests, elapsed / 1000.0);
    printf("QPS(并发): %.0f   p50: %.1f us   p99: %.1f us\n",
           qps, percentile(all, m, 0.50), percentile(all, m, 0.99));
    printf("提示:p50/p99 显示 -1 = bench/metrics.c 的 percentile 或本文件的往返留白还没填。\n");

    for (int t = 0; t < threads; t++) free(ctx[t].lat);
    free(all); free(ctx); free(tid);
    return 0;
}
