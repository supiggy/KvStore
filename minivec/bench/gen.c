#include "gen.h"

/* xorshift32:小巧、确定性的伪随机数发生器。基准只需可复现,不需要密码学强度。 */
static unsigned int g_state = 88172645u;

void gen_seed(unsigned int seed) {
    g_state = seed ? seed : 1u;   /* 0 是 xorshift 的不动点,换成 1 */
}

static unsigned int xorshift32(void) {
    unsigned int x = g_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return g_state = x;
}

void gen_vector(vec_t *out, int dim) {
    for (int i = 0; i < dim; i++) {
        float u = (float)(xorshift32() & 0xFFFFFFu) / (float)0xFFFFFFu; /* [0,1] */
        out[i] = u * 2.0f - 1.0f;                                       /* [-1,1] */
    }
}
