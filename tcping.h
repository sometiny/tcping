#ifndef __TCPING_H__
#define __TCPING_H__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#define TCPING_VERSION "2.1.0"

#define log(verbosity, fmt, ...)                                               \
    do {                                                                       \
        if (verbosity)                                                         \
            fprintf(fmt, __VA_ARGS__);                                         \
    } while (0)

#define INET_NUMERICSERVSTRLEN 6
#define TCPING_OPEN 0
#define TCPING_CLOSED 1
#define TCPING_TIMEOUT 2
#define TCPING_ERROR 255

// 返回连接时间(毫秒)的结构体
struct tcping_result {
    int status;
    double time_ms;  // 连接时间，毫秒
};

struct hostinfo {
    struct addrinfo *ai;
    char name[INET6_ADDRSTRLEN];
    char serv[INET_NUMERICSERVSTRLEN];
};

int tcping_gethostinfo(char *node, char *serv, int ai_family,
                       struct hostinfo **hi);
void tcping_freehostinfo(struct hostinfo *hi);
int tcping_socket(struct hostinfo *host);
int tcping_connect(int sockfd, struct hostinfo *host, struct timeval *timeout,
                   double *time_ms);
int tcping_close(int sockfd);

#endif
