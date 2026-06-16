#include "test_framework.h"

/* 测试计数器(被 MV_CHECK 宏自增) */
int mv_tests_run = 0;
int mv_tests_failed = 0;

int main(void) {
    printf("== distance ==\n"); test_distance_suite();
    printf("== vstore ==\n");   test_vstore_suite();
    printf("== flat ==\n");     test_flat_suite();
    printf("== hnsw delete (G1) ==\n"); test_hnsw_delete_suite();
    printf("== quant (G7) ==\n");        test_quant_suite();

    printf("\n%d 项检查, %d 项失败\n", mv_tests_run, mv_tests_failed);
    if (mv_tests_failed > 0) {
        printf("(标 TODO 的留白没填时会留下未覆盖的检查,属正常)\n");
    }
    return mv_tests_failed ? 1 : 0;
}
