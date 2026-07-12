#ifndef MINIVEC_SERVER_H
#define MINIVEC_SERVER_H

#include "protocol/parser.h"

/* 启动 epoll TCP 服务，阻塞运行事件循环。
 * 收到的每条命令行交给 minivec_handle_command(db, ...) 处理。
 * 返回 <0 表示启动失败。 */
int minivec_server_start(int port, minivec_db_t *db);

#endif /* MINIVEC_SERVER_H */
