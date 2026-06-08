#include<stdio.h>


//这是函数指针的定义，函数指针是一种特殊的指针类型，它指向一个函数的地址，可以通过函数指针来调用函数。函数指针的定义格式如下：
//返回类型 (*函数指针变量名)(参数类型列表);
//
void *client_thread(void *arg){
    int clientfd = *(int*)arg;
    while(1){
        char buffer[1024] = 0;
        int count = recv(clientfd, buffer, sizeof(buffer), 0); // 接收数据
        send(clientfd, buffer, count, 0); // 发送数据
    }
    return NULL;
}

int main(){
    // 创建一个TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    // 定义服务器地址结构
    struct sockaddr_in serveraddr;
    memset(&serveraddr, 0, sizeof(struct sockaddr_in));


    serveraddr.sin_family = AF_INET;// 设置地址族为IPv4
    serveraddr.sin_port = htons(8080); // 设置端口号
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有

    //  绑定套接字到地址
    if(-1 == bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(struct sockaddr))) {
        perror("bind");
        return -1;
    }
   
    listen(sockfd, 10); // 监听连接请求
    //backlog参数指定了系统为该套接字排队的最大连接数. 错误响应为402

    //getchar(); // 等待用户输入，保持程序运行



   
#if 0
    // 定义客户端地址结构
    struct sockaddr_in clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr);

    int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddr_len); // 接受连接请求
    // accept函数会阻塞，直到有客户端连接请求到来

    //while 会有两个问题
    //1. close没有响应，通过count == 0 就close
    //2. 如果有多个客户端连接，while循环只能处理一个客户端，其他客户端的请求会被阻塞，无法处理

    while(1){
        char buffer[1024] = 0;
        int count = recv(clientfd, buffer, sizeof(buffer), 0); // 接收数据
    // recv函数会阻塞，直到有数据可读 
    // 这里用count来接收返回值，recv函数返回实际接收的字节数，如果连接被关闭则返回0，如果发生错误则返回-1
    //非阻塞recv函数会立即返回，如果没有数据可读，则返回-1，并将errno设置为EWOULDBLOCK或EAGAIN

        send(clientfd, buffer, count, 0); // 发送数据
    // send函数会阻塞，直到数据被发送出去
    //count参数指定了要发送的数据的字节数，send函数返回实际发送的字节数，如果发生错误则返回-1
    }

#elif 0 // 线程处理


    while(1){
         // 定义客户端地址结构
    struct sockaddr_in clientaddr;
    socklen_t clientaddr_len = sizeof(clientaddr);

    int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddr_len); // 接受连接请求
    // accept函数会阻塞，直到有客户端连接请求到来

    pthread_t tid;
    pthread_create(&tid, NULL, client_thread, &clientfd); // 创建线程处理客户端连接 
    //这个函数是POSIX线程库中的一个函数，用于创建一个新的线程。它的参数如下：
    //第一个参数是一个指向pthread_t类型的指针，用于存储新线程的ID，
    //第二个参数是一个指向pthread_attr_t类型的指针，用于指定线程的属性，如果为NULL则使用默认属性
    //第三个参数是
    //client_thread函数是线程的入口函数，第四个参数是传递给线程的参数，这里传递了客户端套接字的地址
    //第四个参数是一个void*类型的指针，可以传递任意类型的数据，在client_thread函数中需要将其转换为正确的类型才能使用


    

    }

#elif 0
//select
    //select(maxfd + 1,rset, wset, eset, timeout); // 监视文件描述符集合，等待可读事件发生
    //select函数会阻塞，直到有文件描述符可读或者发生错误
    //maxfd 是文件描述符集合中最大的文件描述符加1。+1是因为文件描述符是从0开始的，所以需要加1来表示集合的大小。
    //rset是一个指向fd_set类型的指针，表示要监视的可读文件描述符集合
    //wset表示要监视的可写文件描述符集合，
    //eset表示要监视的异常文件描述符集合
    //timeout是一个指向struct timeval类型的指针，表示等待的时间，如果为NULL则表示无限等待
    
    //select 的核心就是 这三个set集合，
    //每次调用select函数之前，需要将要监视的文件描述符添加到对应的集合中，并设置maxfd的值
    //当select函数返回时，可以通过检查rset、wset和eset集合来确定哪些文件描述符发生了可读、可写或异常事件，然后进行相应的处理

    fd_set rdfs,rset;
    FD_ZERO(&rdfs); // 初始化文件描述符集合
    FD_SET(sockfd, &rdfs); // 将监听套接字添加到可读集合中


    fd_set wset;
    FD_ZERO(&wset);
    fd_set eset;
    FD_ZERO(&eset);

    int maxfd = sockfd; // 初始化maxfd为监听套接字的文件描述符
    
    while(1){
        rset = rdfs; // 每次调用select函数之前，需要将rdfs复制到rset中，因为select函数会修改rset的内容
        int nready = select(maxfd + 1, &rset, &wset, &eset, NULL); // 监视文件描述符集合，等待可读事件发生
        if(FD_ISSET(sockfd, &rset)){ // 如果监听套接字可读，说明有新的连接请求到来
            //FD_ISSET函数用于检查文件描述符是否在集合中，如果返回非零值
            //表示文件描述符在集合中，即发生了可读事件
            struct sockaddr_in clientaddr;
            socklen_t clientaddr_len = sizeof(clientaddr);
            int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddr_len); // 接受连接请求
            
            FD_SET(clientfd, &rdfs); // 将新的客户端套接字添加到可读集合中
            if(clientfd > maxfd){ // 更新maxfd的值
                maxfd = clientfd;
            }
        }

        int i;
        for(i = sockfd + 1; i <= maxfd; i++){ // 遍历所有文件描述符，检查哪些发生了可读事件
            if(FD_ISSET(i, &rset)){ // 如果文件描述符i可读，说明有数据可读
                char buffer[1024] = 0;
                int count = recv(i, buffer, sizeof(buffer), 0); // 接收数据
                if(count == 0){ // 如果连接被关闭，count为0

                    FD_CLR(i, &rdfs); // 从可读集合中移除该文件描述符
                    close(i); // 关闭套接字
                } else {
                    send(i, buffer, count, 0); // 发送数据
                }
            }
        }

    }

#elif 0
//poll
    //poll函数是一个系统调用，用于监视多个文件描述符的事件。它的函数原型如下：
    //int poll(struct pollfd *fds, nfds_t nfds, int timeout);
    //poll函数会阻塞，直到有文件描述符发生事件或者超时
    //第一个参数是一个指向pollfd结构体数组的指针，每个pollfd结构体表示一个要监视的文件描述符和事件类型
    //第二个参数是pollfd结构体数组的大小，即要监视的文件描述符数量
    //第三个参数是等待的时间，单位为毫秒，如果为-1则表示无限等待

    struct pollfd fds[1024]; // 定义一个pollfd结构体数组，用于存储要监视的文件描述符和事件类型
    int nfds = 0; // 当前要监视的文件描述符数量

    fds[nfds].fd = sockfd; // 将监听套接字添加到pollfd数组中
    fds[nfds].events = POLLIN; // 设置要监视的事件类型为可读事件
    nfds++; // 增加要监视的文件描述符数量
    //

    while(1){
        int nready = poll(fds, nfds, -1); // 监视文件描述符，等待事件发生
        //第一个参数是pollfd结构体数组的指针
        //第二个参数是pollfd结构体数组的大小，第三个参数是等待的时间，单位为毫秒，如果为-1则表示无限等待 
        if(nready < 0){ // 如果发生错误，返回-1
            perror("poll");
            break;
        }

        //这里 0号位置是监听套接字，其他位置是客户端套接字，所以需要分别处理
        if(fds[0].revents & POLLIN){ // 如果监听套接字发生可读事件，说明有新的连接请求到来
            //fds.revents是一个短整型变量，用于表示发生的事件类型。//它是一个位掩码，可以同时表示多个事件类型
            //fds 有两个参数 events 和 revents，
            //events是用户设置的事件类型，revents是内核返回的事件类型。
            //当poll函数返回时，可以通过检查revents来确定哪些事件发生了。
            
            //& POLLIN是一个位掩码，用于检查revents中是否包含可读事件。如果revents & POLLIN的结果非零，说明发生了可读事件，即有新的连接请求到来。
            //& 还能与其他事件类型进行检查，例如POLLOUT表示可写事件，POLLERR表示错误事件等，可以根据需要进行相应的处理
            struct sockaddr_in clientaddr;
            socklen_t clientaddr_len = sizeof(clientaddr);
            int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddr_len); // 接受连接请求
            
            fds[nfds].fd = clientfd; // 将新的客户端套接字添加到pollfd数组中
            fds[nfds].events = POLLIN; // 设置要监视的事件类型为可读事件
            nfds++; // 增加要监视的文件描述符数量

            //这里nfds 和 maxfd = clientfd 没有关系，因为poll函数是通过遍历pollfd数组来检查事件的，而不是通过maxfd来限制检查的范围，所以不需要更新maxfd的值
        }

        //这里是遍历所有要监视的文件描述符，检查哪些发生了事件
        for(int i = 1; i < nfds; i++){ // 遍历所有要监视的文件描述符，检查哪些发生了事件

            //
            if(fds[i].revents & POLLIN){ // 如果文件描述符发生可读事件，说明有数据可读
                char buffer[1024] = 0;
                int count = recv(fds[i].fd, buffer, sizeof(buffer), 0); // 接收数据
                if(count == 0){ // 如果连接被关闭，count为0
                    fds[i] = fds[nfds - 1]; // 将最后一个文件描述符移动到当前位置，覆盖掉已关闭的文件描述符
                    nfds--; // 减少要监视的文件描述符数量
                    close(fds[i].fd); // 关闭套接字
                    
                    // //第二种是“原地标记为空位”
                    // fds[i].fd = -1; // 将文件描述符设置为-1，表示该位置不再有效
                    // fds[i].events = 0; // 将事件类型设置为0，表示不再监视该文件描述符
                    // close(i); // 关闭套接字
                } else {
                    send(fds[i].fd, buffer, count, 0); // 发送数据
                }
            }
        }
#elif 1
//epoll
    //epoll函数是Linux内核提供的一种高效的I/O事件通知机制，适用于处理大量并发连接的场景。它的核心函数包括：
    //1. epoll_create(int size)：创建一个epoll实例，返回一个文件
    //描述符。size参数指定了epoll实例的大小，但在现代Linux内核中已经被忽略，可以设置为任意值。
    //2. epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)：控制epoll实例的行为。epfd是epoll实例的文件描述符，op是操作类型，可以是EPOLL_CTL_ADD（添加文件描述符）、EPOLL_CTL_MOD（修改文件描述符）或EPOLL_CTL_DEL（删除文件描述符）。fd是要操作的文件描述符，event是一个指向epoll_event结构体的指针，用于指定要监视的事件类型和相关数据。
    //3. epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout)：等待事件发生。epfd是epoll实例的文件描述符，events是一个指向epoll_event结构体数组的指针，用于存储发生事件的文件描述符和相关数据。maxevents是events数组的大小，timeout是等待的时间，单位为毫秒，如果为-1则表示无限等待。

    int epfd = epoll_create(1); // 创建一个epoll实例，返回一个文件描述符.参数无作用 >0就行
    
    struct epoll_event ev;
    ev.events = EPOLLIN; // 设置要监视的事件类型为可读
    ev.data.fd = sockfd; // 将监听套接字的文件描述符存储在event的data字段中
    epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev); //
    //将监听套接字添加到epoll实例中，开始监视可读事件
    //先监听

    //这里就是KVstore，key是文件描述符，value是事件类型和相关数据，Key = sockfd，value = ev;

    struct epoll_event events[1024]; // 定义一个epoll_event结构体数组，用于存储发生事件的文件描述符和相关数据
    while(1){
        int nready = epoll_wait(epfd, events, 1024, -1); // 等待事件发生
        if(nready < 0){ // 如果发生错误，返回-1
            perror("epoll_wait");
            break;
        }

        for(int i = 0; i < nready; i++){ // 遍历所有发生事件的文件描述符，检查哪些发生了事件

            int connfd = events[i].data.fd; // 获取发生事件的文件描述符
            if(events[i].data.fd == sockfd){ // 如果监听套接字发生可读事件，说明有新的连接请求到来
                struct sockaddr_in clientaddr;
                socklen_t clientaddr_len = sizeof(clientaddr);
                int clientfd = accept(sockfd, (struct sockaddr*)&clientaddr, &clientaddr_len); // 接受连接请求
                
                ev.events = EPOLLIN; // 设置要监视的事件类型为可读
                // | EPOLLET; // 设置为边缘触发模式，默认是水平触发模式
                ev.data.fd = clientfd; // 将新的客户端套接字的文件描述符存储在event的data字段中
                epoll_ctl(epfd, EPOLL_CTL_ADD, clientfd, &ev); // 将新的客户端套接字添加到epoll实例中，开始监视可读事件
            } else if(events[i].events & EPOLLIN){ // 如果文件描述符发生可读事件，说明有数据可读
                char buffer[1024] = 0;
                int count = recv(events[i].data.fd, buffer, sizeof(buffer), 0); // 接收数据
                if(count == 0){ // 如果连接被关闭，count为0
                    epoll_ctl(epfd, EPOLL_CTL_DEL, events[i].data.fd, NULL); // 从epoll实例中删除该文件描述符
                    close(events[i].data.fd); // 关闭套接字
                } else {
                    send(events[i].data.fd, buffer, count, 0); // 发送数据
                }
            }
        }
    }


#endif
    
    getchar(); // 等待用户输入，保持程序运行
    close(clientfd); // 关闭客户端套接字
    
    
    return 0;
}