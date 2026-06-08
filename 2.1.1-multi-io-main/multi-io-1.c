
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>

// tcp 
int main() {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(2048);

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}

	listen(sockfd, 10);

#if 0 

#else
    //解释一下，5个参数
    //第一个参数是监视的文件描述符数量，通常是最大文件描述符加1
    //第二个参数是一个指向fd_set类型的指针，表示要监视的可读事件集合
    //第三个参数是一个指向fd_set类型的指针，表示要监视的可写事件集合
    //第四个参数是一个指向fd_set类型的指针，表示要监视的异常事件集合
    //第五个参数是一个指向struct timeval类型的指针，表示等待事件发生的最长时间，如果为NULL表示无限等待
    //int nready = select(maxfd + 1, rset, wset, eset, timeout);
    //

    fd_set rfds, rset;
    FD_ZERO(&rfds);//清空文件描述符集合
    FD_SET(sockfd, &rfds);//将监听套接字加入到可读事件集合中 sockfd 对应 bit 置 1

    int maxfd = sockfd;//初始化最大文件描述符为监听套接字
    while (1) {
        rset = rfds;//每次循环前都要重新设置监视的文件描述符集合，因为select会修改它们

        int nready = select(maxfd + 1, &rset, NULL, NULL, NULL);//调用select函数等待事件发生

        if (FD_ISSET(sockfd, &rset)) {//如果监听套接字有可读事件，说明有新的客户端连接请求
            struct sockaddr_in clientaddr;
            socklen_t len = sizeof(clientaddr);
            int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);//接受连接请求，得到新的客户端套接字

            printf("sockfd: %d\n", clientfd);

            FD_SET(clientfd, &rfds);//将新的客户端套接字加入到可读事件集合中
            if (clientfd > maxfd) {//更新最大文件描述符
                maxfd = clientfd;
            }
        }

        int i = 0;
        for (i = sockfd + 1; i <= maxfd; i++) {//遍历所有可能的文件描述符，检查哪些有可读事件
            if (FD_ISSET(i, &rset)) {//如果文件描述符i有可读事件，说明有数据可读或者连接关闭
                char buffer[128] = {0};
                int count = recv(i, buffer, 128, 0);//从文件描述符i读取数据
                if (count == 0) {//如果读取到的数据长度为0，说明连接被关闭了
                    printf("disconnect\n");
                    
                    FD_CLR(i, &rfds);//将文件描述符i从监视集合中移
                    close(i);//关闭文件描述符i
                    continue;//继续下一次循环
                }else {
                    send(i, buffer, count, 0);//将读取到的数据原样发送回去
                    printf("clientfd: %d, count: %d, buffer: %s\n", i, count, buffer);
                }
            }
        }
    }


#elif 0 // poll
    struct pollfd fds[1024] = {0};//定义一个pollfd数组，用于存储要监视的文件描述符和事件
    
    fds[sockfd].fd = sockfd;//将监听套接字的文件描述符存储在数组中
    fds[sockfd].events = POLLIN;//设置监听套接字的事件为可读事件

    int maxfd = sockfd;//初始化最大文件描述符为监听套接字的文件描述符

    while (1) {
        int nready = poll(fds, maxfd + 1, -1);//调用poll函数等待事件发生

        if (fds[sockfd].revents & POLLIN) {//如果监听套接字有可读事件，说明有新的客户端连接请求
            struct sockaddr_in clientaddr;
            socklen_t len = sizeof(clientaddr);
            int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);//接受连接请求，得到新的客户端套接字

            printf("sockfd: %d\n", clientfd);

            fds[clientfd].fd = clientfd;//将新的客户端套接字的文件描述符存储在数组中
            fds[clientfd].events = POLLIN;//设置新的客户端套接字的事件为可读事件

            if (clientfd > maxfd) {//更新最大文件描述符
                maxfd = clientfd;
            }
        }

        int i = 0;
        for (i = sockfd + 1; i <= maxfd; i++) {//遍历所有可能的文件描述符，检查哪些有可读事件
            if (fds[i].revents & POLLIN) {//如果文件描述符i有可读事件，说明有数据可读或者连接关闭
                char buffer[128] = {0};
                int count = recv(i, buffer, 128, 0);//从文件描述符i读取数据
                if (count == 0) {//如果读取到的数据长度为0，说明连接被关闭了
                    printf("disconnect\n");
                    
                    fds[i].fd = -1;//将文件描述符i标记为无效
                    fds[i].events = 0;//清除文件描述符i的事件
                    
                    close(i);//关闭文件描述符i
                    break;
                }
                else {
                    send(i, buffer, count, 0);//将读取到的数据原样发送回去
                    printf("clientfd: %d, count: %d, buffer: %s\n", i, count, buffer);
                }
            }
        }
    }

#elif 0 // epoll
    int epfd = epoll_create(1);//创建一个epoll实例，返回一个文件描述符
    struct epoll_event ev;//定义一个epoll_event结构体，用于存储要监视的事件和相关数据
    ev.data.fd = sockfd;//将监听套接字的文件描述符存储在结构体中
    ev.events = EPOLLIN;//设置监听套接字的事件为可读事件

    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev);//将监听套接字添加到epoll实例中，监视可读事件

    struct epoll_event events[1024] = {0};//定义一个epoll_event数组，用于存储发生事件的文件描述符和相关数据
    while(1){
        int nready = epoll_wait(epfd, events, 1024, -1);//调用epoll_wait函数等待事件发生

        int i = 0;
        for (i = 0; i < nready; i++) {//遍历所有发生事件的文件描述符，检查哪些有可读事件
            int connfd = events[i].data.fd;//获取发生事件的文件描述符
            if (connfd == sockfd) {//如果发生事件的文件描述符是监听套接字，说明有新的客户端连接请求
                struct sockaddr_in clientaddr;
                socklen_t len = sizeof(clientaddr);
                int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);//接受连接请求，得到新的客户端套接字

                printf("sockfd: %d\n", clientfd);

                ev.data.fd = clientfd;//将新的客户端套接字的文件描述符存储在结构体中
                ev.events = EPOLLIN;//设置新的客户端套接字的事件为可读事件，这里默认是水平触发模式，如果需要使用边缘触发模式，可以将事件设置为EPOLLIN | EPOLLET
                //边缘触发
                //ev.events = EPOLLIN | EPOLLET;//设置新的客户端套接字的事件为可读事件，并且使用边缘触发模式

                epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev);
                //将新的客户端套接字添加到epoll实例中，监视可读事件
            }else if(events[i].events & EPOLLIN) {//如果发生事件的文件描述符不是监听套接字，说明有数据可读或者连接关闭
                char buffer[128] = {0};
                int count = recv(connfd, buffer, 128, 0);//从发生事件的文件描述符读取数据
                if (count == 0) {//如果读取到的数据长度为0，说明连接被关闭了
                    printf("disconnect\n");
                    
                    epoll_ctl(epfd, EPOLL_CTL_DEL,connfd, NULL);
                    //将发生事件的文件描述符从epoll实例中删除
                    
                    close(events[i].data.fd);//关闭发生事件的文件描述符
                    break;
                }
                send(connfd, buffer, count, 0);//将读取到的数据原样发送回去
                printf("clientfd: %d, count: %d, buffer: %s\n", connfd, count, buffer);
                
            }
        }
    }
    
#endif

    getchar();

}