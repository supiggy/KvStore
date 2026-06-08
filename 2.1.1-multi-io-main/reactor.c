


#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/time.h>


#define BUFFER_LENGTH		512


//这里的函数指针的命名规则是：
//1. accept_cb: 这个函数指针用于处理新的连接请求，通常会被监听套接字的回调函数指针所使用。当有新的客户端连接请求到达时，accept_cb函数会被调用来接受连接并进行相关处理。
//2. recv_cb: 这个函数指针用于处理可读事件，通常会被客户端套接字的回调函数指针所使用。当客户端套接字上有数据可读时，recv_cb函数会被调用来读取数据并进行相关处理。
//3. send_cb: 这个函数指针用于处理可写事件，通常会被客户端套接字的回调函数指针所使用。当客户端套接字上有数据可写时，send_cb函数会被调用来发送数据并进行相关处理。
typedef int (*RCALLBACK)(int fd);
//这里要注意函数指针的命名方式

// listenfd
// EPOLLIN --> 
int accept_cb(int fd);
// clientfd
// 
int recv_cb(int fd);
int send_cb(int fd);

// conn, fd, buffer, callback
struct conn_item {
	int fd;
	
	char rbuffer[BUFFER_LENGTH];
	int rlen;
	char wbuffer[BUFFER_LENGTH];
	int wlen;


	//这里使用了一个联合体来存储不同类型的回调函数指针。根据文件描述符的类型（监听套接字还是客户端套接字），可以使用不同的回调函数来处理事件。
	//当文件描述符是监听套接字时，使用accept_callback来处理新的请求
	//当文件描述符是客户端套接字时，使用recv_callback来处理可读事件
	//union就可以节省内存，因为在同一时间内，一个文件描述符只会使用其中一个回调函数指针。
	union {
		RCALLBACK accept_callback;
		RCALLBACK recv_callback;
	} recv_t;
	RCALLBACK send_callback;
};
// libevent --> 


int epfd = 0;
struct conn_item connlist[1048576] = {0}; // 1024  2G     2 * 512 * 1024 * 1024 
// list
struct timeval zvoice_king;
// 
// 1000000

#define TIME_SUB_MS(tv1, tv2)  ((tv1.tv_sec - tv2.tv_sec) * 1000 + (tv1.tv_usec - tv2.tv_usec) / 1000)

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
	set_event(clientfd, EPOLLIN, 1);

	connlist[clientfd].fd = clientfd;
	memset(connlist[clientfd].rbuffer, 0, BUFFER_LENGTH);
	connlist[clientfd].rlen = 0;
	memset(connlist[clientfd].wbuffer, 0, BUFFER_LENGTH);
	connlist[clientfd].wlen = 0;
	
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

	//send

	//send这个函数不应该在recv_cb中直接调用，因为recv_cb的主要职责是处理可读事件，即从客户端套接字读取数据。直接在recv_cb中调用send函数可能会导致一些问题，例如：

	// //set_event
	// struct epoll_event ev;
	// ev.events = EPOLLOUT;
	// ev.data.fd = fd;
	// epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);



#if 0 //echo: need to send
	memcpy(connlist[fd].wbuffer, connlist[fd].rbuffer, connlist[fd].rlen);
	connlist[fd].wlen = connlist[fd].rlen;
	connlist[fd].rlen -= connlist[fd].rlen;
#else
	//	
	kvstrore_request(&connlist[fd]);
	connlist[fd].wlen = strlen(connlist[fd].wbuffer);
	//http_request(&connlist[fd]);
	//http_response(&connlist[fd]);

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


int init_server(unsigned short port) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serveraddr;
	memset(&serveraddr, 0, sizeof(struct sockaddr_in));

	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(port);

	if (-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
		perror("bind");
		return -1;
	}

	listen(sockfd, 10);

	return sockfd;
}

// tcp 
int epoll_entry() {

	int port_count = 20;
	unsigned short port = 2048;
	int i = 0;

	
	epfd = epoll_create(1); // int size

	for (i = 0;i < port_count;i ++) {
		int sockfd = init_server(port + i);  // 2048, 2049, 2050, 2051 ... 2057
		connlist[sockfd].fd = sockfd;
		connlist[sockfd].recv_t.accept_callback = accept_cb;
		set_event(sockfd, EPOLLIN, 1);
	}

	gettimeofday(&zvoice_king, NULL);

	struct epoll_event events[1024] = {0};
	
	while (1) { // mainloop();

		int nready = epoll_wait(epfd, events, 1024, -1); // 

		int i = 0;
		for (i = 0;i < nready;i ++) {

			int connfd = events[i].data.fd;
			if (events[i].events & EPOLLIN) { //

				int count = connlist[connfd].recv_t.recv_callback(connfd);
				//printf("recv count: %d <-- buffer: %s\n", count, connlist[connfd].rbuffer);

			} else if (events[i].events & EPOLLOUT) { 
				// printf("send --> buffer: %s\n",  connlist[connfd].wbuffer);
				
				int count = connlist[connfd].send_callback(connfd);
			}

		}

	}


	//getchar();
	//close(clientfd);

}




