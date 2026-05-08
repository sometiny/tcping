#include "tcping.h"

// 跨平台获取时间（毫秒）
static double get_time_ms(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / freq.QuadPart * 1000.0;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
#endif
}

int tcping_gethostinfo(char *node, char *serv, int ai_family,
                       struct hostinfo **hi)
{
    *hi = (struct hostinfo *)calloc(1, sizeof(struct hostinfo));
    /* collect all ipv4 and ipv6 address info */
    struct addrinfo hints;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = ai_family;
    hints.ai_socktype = SOCK_STREAM;
    int err;
    if ((err = getaddrinfo(node, serv, &hints, &(*hi)->ai)) != 0)
        return err;
    if ((err = getnameinfo((*hi)->ai->ai_addr, (*hi)->ai->ai_addrlen,
                           (char *)&(*hi)->name, INET6_ADDRSTRLEN,
                           (char *)&(*hi)->serv, INET_NUMERICSERVSTRLEN,
                           NI_NUMERICHOST | NI_NUMERICSERV)) != 0)
        return err;
    return 0;
}

void tcping_freehostinfo(struct hostinfo *hi)
{
    freeaddrinfo(hi->ai);
    free(hi);
}

int tcping_socket(struct hostinfo *host)
{
    int sockfd = socket(host->ai->ai_family, host->ai->ai_socktype,
                        host->ai->ai_protocol);
#ifdef _WIN32
    // Windows: 使用 ioctlsocket 设置非阻塞
    unsigned long mode = 1;
    ioctlsocket(sockfd, FIONBIO, &mode);
#else
    // Linux/Unix: 使用 fcntl 设置非阻塞
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
#endif
    return sockfd;
}

int tcping_connect(int sockfd, struct hostinfo *host, struct timeval *timeout,
                   double *time_ms)
{
    double start = get_time_ms();

    int ret;
    if ((ret = connect(sockfd, host->ai->ai_addr, host->ai->ai_addrlen)) != 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
#else
        if (errno != EINPROGRESS) {
#endif
            *time_ms = get_time_ms() - start;
            return TCPING_ERROR;
        }
        fd_set fdrset, fdwset;
        FD_ZERO(&fdrset);
        FD_SET(sockfd, &fdrset);
        fdwset = fdrset;
        if ((ret = select(sockfd + 1, &fdrset, &fdwset, NULL,
                          timeout->tv_sec + timeout->tv_usec > 0 ? timeout
                                                                 : NULL)) ==
            0) {
            /* timeout */
            *time_ms = get_time_ms() - start;
            return TCPING_TIMEOUT;
        }
        int error = 0;
        if (FD_ISSET(sockfd, &fdrset) || FD_ISSET(sockfd, &fdwset)) {
            socklen_t errlen = sizeof(error);
#ifdef _WIN32
            // Windows: getsockopt 需要 char*
            if ((ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, (char *)&error,
                                  &errlen)) != 0) {
#else
            if ((ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error,
                                  &errlen)) != 0) {
#endif
                /* getsockopt error */
                *time_ms = get_time_ms() - start;
                return TCPING_ERROR;
            }
            if (error != 0) {
                /* closed */
                *time_ms = get_time_ms() - start;
                return TCPING_CLOSED;
            }
        } else {
            *time_ms = get_time_ms() - start;
            return TCPING_ERROR;
        }
    }
    /* connection established */
    *time_ms = get_time_ms() - start;
    return TCPING_OPEN;
}

int tcping_close(int sockfd) { return close(sockfd); }