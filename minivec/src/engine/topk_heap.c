#include "engine/topk_heap.h"

/* ============================================================
 * topK 小顶堆实现
 * 数组存完全二叉树:父 (i-1)/2,左 2i+1,右 2i+2。
 * 堆顶 heap[0] = 当前 K 个里 score 最小者(门槛)。
 * 详细流程树见 docs/dui-流程树.md。
 * ============================================================ */

static void sr_swap(search_result_t *a, search_result_t *b) {
    search_result_t t = *a; *a = *b; *b = t;
}

/* 上浮:小顶堆,比父小就往上 */
static void heap_siftup(search_result_t *h, int i) {
    while (i > 0) {
        int parent = (i - 1) / 2;
        if (h[i].score < h[parent].score) {
            sr_swap(&h[i], &h[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

/* 下沉:小顶堆,和更小的孩子交换 */
static void heap_siftdown(search_result_t *h, int n, int i) {
    while (1) {
        int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
        if (l < n && h[l].score < h[smallest].score) smallest = l;
        if (r < n && h[r].score < h[smallest].score) smallest = r;
        if (smallest == i) break;
        sr_swap(&h[i], &h[smallest]);
        i = smallest;
    }
}

void topk_push(search_result_t *heap, int *size, int K, uint64_t id, float score) {
    if (*size < K) {                            /* 没满:放末尾再上浮 */
        heap[*size].id = id;
        heap[*size].score = score;
        heap_siftup(heap, *size);
        (*size)++;
    } else if (K > 0 && score > heap[0].score) {  /* 满了:比门槛大才替换堆顶 */
        heap[0].id = id;
        heap[0].score = score;
        heap_siftdown(heap, *size, 0);
    }
}

int topk_cmp_desc(const void *a, const void *b) {
    float sa = ((const search_result_t *)a)->score;
    float sb = ((const search_result_t *)b)->score;
    if (sa < sb) return 1;
    if (sa > sb) return -1;
    return 0;
}
