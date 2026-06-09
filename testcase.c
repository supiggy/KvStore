#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "kvstore.h"

#define MAX_MAS_LENGTH 1024


int send_msg(int connfd, const char *msg,int length) {
    int res = send(connfd, msg, length, 0);
    if (res < 0) {
        perror("send");
        return -1;
    }
    return res;
}


int recv_msg(int connfd, char *buffer, int buflen) {
    int res = recv(connfd, buffer, buflen, 0);
    if (res < 0) {
        perror("recv");
        return -1;
    }
    return res;
}

void equals(char *pattern, char *result,char *casename) {
    if (strcmp(pattern, result) != 0) {
        printf("failed: '%s' != '%s' in case: %s\n", pattern, result, casename);
    } else {
        printf("success: '%s' == '%s' in case: %s\n", pattern, result, casename);
    }
}

int test_case(int connfd,char *msg,char *pattern,char *casename) {

    if(!msg || !pattern || !casename || strlen(msg) == 0 || strlen(pattern) == 0 || strlen(casename) == 0){
        printf("invalid msg or length\n");
        return -1;
    }

    int res = send_msg(connfd, msg, strlen(msg));
    if (res < 0) {
        return -1;
    }

    char rbuffer[BUFFER_LENGTH] = {0};
    res = recv_msg(connfd, rbuffer, BUFFER_LENGTH);
    if (res < 0) {
        return -1;
    }

    equals(pattern, rbuffer, casename);
    return 0;
}


int array_test_case(int connfd) {

    test_case(connfd, "SET NAME King", "SUCCESS","SET Case");
    test_case(connfd, "GET NAME", "King","GET Case");
    test_case(connfd, "MOD NAME Queen", "SUCCESS","MOD Case");
    test_case(connfd, "GET NAME", "Queen","GET Case");
    test_case(connfd, "DEL NAME", "SUCCESS","DEL Case");
    test_case(connfd, "GET NAME", "NO EXIST","GET Case");

     return 0;  
}

int rbtree_test_case(int connfd) {
    test_case(connfd, "RSET NAME King", "SUCCESS","RSET Case");
    test_case(connfd, "RGET NAME", "King","RGET Case");
    test_case(connfd, "RMOD NAME Queen", "SUCCESS","RMOD Case");
    test_case(connfd, "RGET NAME", "Queen","RGET Case");
    test_case(connfd, "RDEL NAME", "SUCCESS","RDEL Case");
    test_case(connfd, "RGET NAME", "NO EXIST","RGET Case");

    return 0;
}

int hash_test_case(int connfd) {
    test_case(connfd, "HSET NAME King", "SUCCESS","HSET Case");
    test_case(connfd, "HGET NAME", "King","HGET Case");
    test_case(connfd, "HMOD NAME Queen", "SUCCESS","HMOD Case");
    test_case(connfd, "HGET NAME", "Queen","HGET Case");
    test_case(connfd, "HDEL NAME", "SUCCESS","HDEL Case");
    test_case(connfd, "HGET NAME", "NO EXIST","HGET Case");

    return 0;
}

int connect_tcpserver(const char *ip, unsigned short port) {

	int connfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in tcpserver_addr;
	memset(&tcpserver_addr, 0, sizeof(struct sockaddr_in));

	tcpserver_addr.sin_family = AF_INET;
	tcpserver_addr.sin_addr.s_addr = inet_addr(ip);
	tcpserver_addr.sin_port = htons(port);

	int ret = connect(connfd, (struct sockaddr*)&tcpserver_addr, sizeof(struct sockaddr_in));
	if (ret) {
		perror("connect");
		return -1;
	}

	return connfd;
}

//array : 0x01 rebtree: 0x02 hash: 0x04 skiptable: 0x08
//这里是一个测试用例, 用来测试kvstore的功能是否正确
// ./testcase -s 192.168.243.131 -p 9096 -m
int main(int argc, char *argv[]) {

	int ret = 0;

	char *ip = NULL;
    int port = 0;
    int mode = 0;

	int opt;
	while ((opt = getopt(argc, argv, "s:p:m:?")) != -1) {

		switch (opt) {

			case 's':
				printf("-s: %s\n", optarg);
				ip = optarg;
				break;

			case 'p':
				printf("-p: %s\n", optarg);

				port = atoi(optarg);
				break;
			case 'm':
				printf("-m: %s\n", optarg);
				mode = atoi(optarg);
				break;

			default:
				return -1;
		
		}
		
	}

	int connfd = connect_tcpserver(ip, port);
    if(mode & 0x01){
        ret = array_test_case(connfd);
    }
    if(mode & 0x02){
        ret = rbtree_test_case(connfd);
    }
    if(mode & 0x04){
        ret = hash_test_case(connfd);
    }

    close(connfd);
    return ret;

}

