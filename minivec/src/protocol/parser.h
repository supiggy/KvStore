#ifndef MINIVEC_PARSER_H
#define MINIVEC_PARSER_H

#include "common/minivec.h"

/* minivec_db：应用层上下文，把"存储 + 索引"捆在一起。
 * parser 是协议层，往下调 vector_store / index_*，往上被 server 调用。 */
typedef struct minivec_db minivec_db_t;

minivec_db_t *minivec_db_create(metric_t metric);
void          minivec_db_destroy(minivec_db_t *db);

/* 处理一条完整文本命令行（已去掉换行）：
 *   解析 -> 调引擎 -> 把结果写进 out（最多 outlen 字节，含结尾'\0'）。
 * 返回 0 正常（out 里是要回给客户端的内容），<0 协议错误（out 里是错误信息）。
 *
 * 注意：line 会被原地切分（strtok），调用方不要复用。 */
int minivec_handle_command(minivec_db_t *db, char *line, char *out, int outlen);

#endif /* MINIVEC_PARSER_H */
