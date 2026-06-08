/*
 *  Author : WangBoJing , email : 1989wangbojing@gmail.com
 * 
 *  Copyright Statement:
 *  --------------------
 *  This software is protected by Copyright and the information contained
 *  herein is confidential. The software may not be copied and the information
 *  contained herein may not be used or disclosed except with the written
 *  permission of Author. (C) 2017
 * 
 *

****       *****                                      *****
  ***        *                                       **    ***
  ***        *         *                            *       **
  * **       *         *                           **        **
  * **       *         *                          **          *
  *  **      *        **                          **          *
  *  **      *       ***                          **
  *   **     *    ***********    *****    *****  **                   ****
  *   **     *        **           **      **    **                 **    **
  *    **    *        **           **      *     **                 *      **
  *    **    *        **            *      *     **                **      **
  *     **   *        **            **     *     **                *        **
  *     **   *        **             *    *      **               **        **
  *      **  *        **             **   *      **               **        **
  *      **  *        **             **   *      **               **        **
  *       ** *        **              *  *       **               **        **
  *       ** *        **              ** *        **          *   **        **
  *        ***        **               * *        **          *   **        **
  *        ***        **     *         **          *         *     **      **
  *         **        **     *         **          **       *      **      **
  *         **         **   *          *            **     *        **    **
*****        *          ****           *              *****           ****
                                       *
                                      *
                                  *****
                                  ****



 *
 */
 
#include "kvstore.h"
#include "nty_coroutine.h"

#include <arpa/inet.h>

#define MAX_CLIENT_NUM			1000000
#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)


void server_reader(void *arg) {
	int fd = *(int *)arg;
	int ret = 0;

 
	struct pollfd fds;
	fds.fd = fd;
	fds.events = POLLIN;

	while (1) {
#if 0
		char buf[1024] = {0};
		ret = recv(fd, buf, 1024, 0);
		if (ret > 0) {
			if(fd > MAX_CLIENT_NUM) 
			printf("read from server: %.*s\n", ret, buf);

			ret = send(fd, buf, strlen(buf), 0);
			if (ret == -1) {
				close(fd);
				break;
			}

		} else if (ret == 0) {	
			close(fd);
			break;
		}
#else	
		//为啥从buf[]改成item.rbuffer了呢？
		//因为我们需要把从客户端收到的数据存储在conn_item结构体的rbuffer中，以便后续处理使用。
		struct conn_item item = {0};
		ret = recv(fd, item.rbuffer, BUFFER_LENGTH, 0);
		if (ret > 0) {
			if(fd > MAX_CLIENT_NUM) 
			printf("read from server: %.*s\n", ret, item.rbuffer);

			kvstore_request(&item);
			
			//这里是send响应
			//这里的item.wbuffer是kvstore_request函数中写入的响应结果, item.wlen是响应结果的长度
			item.wlen = strlen(item.wbuffer);//这个计算是为了确保发送的数据长度正确,
			//因为item.wbuffer是一个字符串, 需要计算它的实际长度来发送
			ret = send(fd, item.wbuffer, item.wlen, 0);
			if (ret == -1) {
				close(fd);
				break;
			}

		} else if (ret == 0) {	
			close(fd);
			break;
		}	
		
#endif

	}
}


void server(void *arg) {

	unsigned short port = *(unsigned short *)arg;
	free(arg);

	int fd = nty_socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return ;

	struct sockaddr_in local, remote;
	local.sin_family = AF_INET;
	local.sin_port = htons(port);
	local.sin_addr.s_addr = INADDR_ANY;
	bind(fd, (struct sockaddr*)&local, sizeof(struct sockaddr_in));

	listen(fd, 20);
	printf("listen port : %d\n", port);

	
	struct timeval tv_begin;
	gettimeofday(&tv_begin, NULL);

	while (1) {
		socklen_t len = sizeof(struct sockaddr_in);
		int cli_fd = accept(fd, (struct sockaddr*)&remote, &len);
		if (cli_fd % 1000 == 999) {

			struct timeval tv_cur;
			memcpy(&tv_cur, &tv_begin, sizeof(struct timeval));
			
			gettimeofday(&tv_begin, NULL);
			int time_used = TIME_SUB_MS(tv_begin, tv_cur);
			
		}

		nty_coroutine *read_co;
		nty_coroutine_create(&read_co, server_reader, &cli_fd);

	}
	
}


#if 0
int main(int argc, char *argv[]) {
	nty_coroutine *co = NULL;

	int i = 0;
	unsigned short base_port = 9096;
	for (i = 0;i < 1;i ++) {
		unsigned short *port = calloc(1, sizeof(unsigned short));
		*port = base_port + i;
		nty_coroutine_create(&co, server, port); ////////no run
	}

	nty_schedule_run(); //run

	return 0;
}

#else

int ntyco_entry(void) {
	nty_coroutine *co = NULL;

	int i = 0;
	unsigned short base_port = 9096;
	for (i = 0;i < 1;i ++) {
		unsigned short *port = calloc(1, sizeof(unsigned short));
		*port = base_port + i;
		nty_coroutine_create(&co, server, port); ////////no run
	}

	nty_schedule_run(); //run

	return 0;
}


#endif


