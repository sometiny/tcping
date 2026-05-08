#include "tcping.h"

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
    fcntl(sockfd, F_SETFL, O_NONBLOCK);
    return sockfd;
}

int tcping_connect(int sockfd, struct hostinfo *host, struct timeval *timeout,
                   double *time_ms)
{
    struct timeval start, end;
    gettimeofday(&start, NULL);

    int ret;
    if ((ret = connect(sockfd, host->ai->ai_addr, host->ai->ai_addrlen)) != 0) {
        if (errno != EINPROGRESS) {
            gettimeofday(&end, NULL);
            *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                       (end.tv_usec - start.tv_usec) / 1000.0;
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
            gettimeofday(&end, NULL);
            *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                       (end.tv_usec - start.tv_usec) / 1000.0;
            return TCPING_TIMEOUT;
        }
        int error = 0;
        if (FD_ISSET(sockfd, &fdrset) || FD_ISSET(sockfd, &fdwset)) {
            socklen_t errlen = sizeof(error);
            if ((ret = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error,
                                  &errlen)) != 0) {
                /* getsockopt error */
                gettimeofday(&end, NULL);
                *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                           (end.tv_usec - start.tv_usec) / 1000.0;
                return TCPING_ERROR;
            }
            if (error != 0) {
                /* closed */
                gettimeofday(&end, NULL);
                *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                           (end.tv_usec - start.tv_usec) / 1000.0;
                return TCPING_CLOSED;
            }
        } else {
            gettimeofday(&end, NULL);
            *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                       (end.tv_usec - start.tv_usec) / 1000.0;
            return TCPING_ERROR;
        }
    }
    /* connection established */
    gettimeofday(&end, NULL);
    *time_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
               (end.tv_usec - start.tv_usec) / 1000.0;
    return TCPING_OPEN;
}

int tcping_close(int sockfd) { return close(sockfd); }
