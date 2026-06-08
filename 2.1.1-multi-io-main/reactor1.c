#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>


#define ENABLE_HTTP_RESPONSE 1

#if ENABLE_HTTP_RESPONSE
	typedef struct conn_item connection_t;
	int http_request(connection_t  *conn){
		//这里是解析HTTP请求的逻辑，具体实现可以根据需要进行编写
		//例如，可以从conn->rbuffer中解析出HTTP请求的相关信息，并将结果存储在conn的相关成员中。
		
		//这里就是简单地将HTTP请求的内容存储在连接项的读缓冲区中，并设置读缓冲区的长度。
		//在实际的HTTP服务器中，http_request函数可能会更加复杂，需要处理各种HTTP请求方法、请求头、请求体等内容，并将解析结果存储在连接项的相关成员中，以供后续处理使用。
		int len = snprintf(conn->wbuffer, BUFFER_LENGTH, "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello, World!");
		conn->wlen = len;
		return 0;
	}

#endif	
#define BUFFER_LENGTH		128

typedef int (*RCALLBACK)(int fd);


// listenfd
// EPOLLIN --> 
int accept_cb(int fd);
// clientfd
// 
int recv_cb(int fd);
int send_cb(int fd);

int epfd = 0;

struct conn_item {
    int fd;
    
	//rbuffer 
    char rbuffer[BUFFER_LENGTH];
    int rlen;
    char wbuffer[BUFFER_LENGTH];
    int wlen;

    union {
        RCALLBACK accept_callback;
        RCALLBACK recv_callback;
    } recv_t;
    RCALLBACK send_callback;
};

struct conn_item connlist[1024] = {0}; // 1024  2G     2 * 512 * 1024 * 1024


//这个set_event函数的作用
//是将文件描述符fd添加到epoll实例中，并设置要监视的事件类型event。
//参数flag用于区分是添加新的事件还是修改已有事件。当flag为1时，表示添加新的事件；当flag为0时，表示修改已有事件。
int set_event(int fd, int event, int flag) {

	if (flag) { // 1 add, 0 mod
		struct epoll_event ev;
		ev.events = event ;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
	} else {
	
		struct epoll_event ev;
		ev.events = event;
		ev.data.fd = fd;
		epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
	}

}


int accept_cb(int fd) {

	struct sockaddr_in clientaddr;
	socklen_t len = sizeof(clientaddr);
	
	int clientfd = accept(fd, (struct sockaddr*)&clientaddr, &len);
	if (clientfd < 0) {
		return -1;
	}
    //
	set_event(clientfd, EPOLLIN, 1);


	//这里是初始化新连接的相关信息，包括文件描述符、读写缓冲区、回调函数等。这样在后续处理事件时，就可以根据这些信息来正确地处理每个连接。
	connlist[clientfd].fd = clientfd;
	memset(connlist[clientfd].rbuffer, 0, BUFFER_LENGTH);
	connlist[clientfd].rlen = 0;
	memset(connlist[clientfd].wbuffer, 0, BUFFER_LENGTH);
	connlist[clientfd].wlen = 0;
	
	//这里是函数指针的赋值，将accept_cb函数的地址赋值给recv_t.accept_callback成员，这样当有新的连接请求到达时，就会调用accept_cb函数来处理这个事件。
	connlist[clientfd].recv_t.recv_callback = recv_cb;
	connlist[clientfd].send_callback = send_cb;

	if ((clientfd % 1000) == 999) {
		struct timeval tv_cur;
		gettimeofday(&tv_cur, NULL);
		int time_used = TIME_SUB_MS(tv_cur, zvoice_king);

		memcpy(&zvoice_king, &tv_cur, sizeof(struct timeval));
		
		printf("clientfd : %d, time_used: %d\n", clientfd, time_used);
	}

	return clientfd;
}

//接收数据，并把buffer存到对应位置
int recv_cb(int fd) { // fd --> EPOLLIN

	char *buffer = connlist[fd].rbuffer;
	int idx = connlist[fd].rlen;
	
	int count = recv(fd, buffer+idx, BUFFER_LENGTH-idx, 0);
	if (count == 0) {
		printf("disconnect\n");

		epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);		
		close(fd);
		
		return -1;
	}
	connlist[fd].rlen += count;

#if 0 //echo：need to send
	//memcpy是一个标准的C库函数，用于将一块内存区域的内容复制到另一块内存区域。
	memcpy(connlist[fd].wbuffer, connlist[fd].rbuffer, connlist[fd].rlen);
	//这里wlen被设置为rlen的值，是因为在这个例子中，发送的数据就是接收到的数据的原样复制，所以发送数据的长度应该和接收数据的长度相同。
	connlist[fd].wlen = connlist[fd].rlen;	
#else
	//这里是处理HTTP请求的逻辑
	//http_request函数用于解析HTTP请求并将结果存储在连接项的相关成员中
	//http_response函数用于根据解析结果生成HTTP响应并将其存储在连接项的写缓冲区中。
	http_request(&connlist[fd]);
	http_response(&connlist[fd]);

#endif

    set_event(fd, EPOLLOUT, 0);


	return count;
}

int send_cb(int fd) {

	char *buffer = connlist[fd].wbuffer;
	int idx = connlist[fd].wlen;

	int count = send(fd, buffer, idx, 0);

	set_event(fd, EPOLLIN, 0);

	return count;
}
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

	// listenfd
	connlist[sockfd].fd = sockfd;
	connlist[sockfd].recv_t.accept_callback = accept_cb;


 // epoll
    epfd = epoll_create(1);//创建一个epoll实例，返回一个文件描述符//注意这里传参已经没有意义了

	set_event(sockfd, EPOLLIN, 1);//将监听套接字添加到epoll实例中，监视可读事件
    

    struct epoll_event events[1024] = {0};//定义一个epoll_event数组，用于存储发生事件的文件描述符和相关数据
    while(1){
        int nready = epoll_wait(epfd, events, 1024, -1);//调用epoll_wait函数等待事件发生

        int i = 0;
        for (i = 0; i < nready; i++) {//遍历所有发生事件的文件描述符，检查哪些有可读事件
			int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) { //
				//这里的recv_cb函数是通过函数指针调用的
				//connlist[connfd].recv_t.recv_callback就是recv_cb函数的地址，所以当有可读事件发生时，就会调用recv_cb函数来处理这个事件。
				//也就是相当于调用recv_cb(connfd)来处理这个事件。
				int count = connlist[connfd].recv_t.recv_callback(connfd);
				//printf("recv count: %d <-- buffer: %s\n", count, connlist[connfd].rbuffer);

			} else if (events[i].events & EPOLLOUT) { 
				// printf("send --> buffer: %s\n",  connlist[connfd].wbuffer);
				
				int count = connlist[connfd].send_callback(connfd);
			}

			
			/*  int connfd = events[i].data.fd;//获取发生事件的文件描述符
            if (connfd == sockfd) {//如果发生事件的文件描述符是监听套接字，说明有新的客户端连接请求

               //int clientfd = accept_cb(connfd);//调用accept_cb函数处理新的连接请求，返回新的客户端套接字文件描述符
			   int clientfd = conlist[sockfd].recv_t.accept_callback(sockfd);
               printf("new clientfd: %d\n", clientfd);

            }else if(events[i].events & EPOLLIN) {//如果发生事件的文件描述符不是监听套接字，说明有数据可读或者连接关闭
               // int count = recv_cb(connfd);//调用recv_cb函数处理可读事件，返回读取到的数据长度
                
                //调用recv_cb函数处理可读事件，返回读取到的数据长度
                int count = connlist[connfd].recv_t.recv_callback(connfd);
                if (count == 0) {//如果读取到的数据长度为0，说明连接被关闭了
                    printf("disconnect\n");
                    
                    
                    epoll_ctl(epfd, EPOLL_CTL_DEL,connfd, NULL);
                    //将发生事件的文件描述符从epoll实例中删除
                    
                    close(events[i].data.fd);//关闭发生事件的文件描述符
                    break;
                }
                send(connfd, buffer, count, 0);//将读取到的数据原样发送回去
                printf("clientfd: %d, count: %d, buffer: %s\n", connfd, count, buffer);
                
            } */
        }
    }

    getchar();

}