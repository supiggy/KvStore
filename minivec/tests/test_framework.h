#ifndef MV_TEST_FRAMEWORK_H
#define MV_TEST_FRAMEWORK_H

/* ============================================================
 * 极简单元测试框架(G0 骨架,已填好,直接用)
 *   MV_CHECK(cond, msg)            条件为真则通过
 *   MV_CHECK_FEQ(a, b, eps, msg)   浮点近似相等(|a-b|<=eps)
 * 每个测试套件是一个 void 函数,在 run_tests.c 里被依次调用。
 * ============================================================ */

#include <stdio.h>
#include <math.h>

extern int mv_tests_run;
extern int mv_tests_failed;

#define MV_CHECK(cond, msg) do {                                  \
    mv_tests_run++;                                               \
    if (!(cond)) {                                                \
        mv_tests_failed++;                                        \
        printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, msg);  \
    } else {                                                      \
        printf("  [ ok ] %s\n", msg);                             \
    }                                                             \
} while (0)

#define MV_CHECK_FEQ(a, b, eps, msg) MV_CHECK(fabsf((a) - (b)) <= (eps), msg)

/* 各测试套件入口(在对应 .c 实现) */
void test_distance_suite(void);
void test_vstore_suite(void);
void test_flat_suite(void);

#endif /* MV_TEST_FRAMEWORK_H */
