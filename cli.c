/*
 * tcping command line utility
 *
 * Copyright (c) 2002-2023 Marc Kirchner
 *
 */

#include "tcping.h"

void usage(char *prog)
{
    fprintf(stderr,
            "error: Usage: %s [-q] [-f <4|6>] [-t timeout_sec] [-u "
            "timeout_usec] [-n count] <host> <port>\n",
            prog);
    exit(-1);
}

// 跨平台睡眠函数（毫秒）
static void sleep_ms(int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

// 跨平台获取最后错误信息
static const char* get_last_error_str(void)
{
#ifdef _WIN32
    static char err_buf[256];
    int err = WSAGetLastError();
    snprintf(err_buf, sizeof(err_buf), "WSAError %d", err);
    return err_buf;
#else
    return strerror(errno);
#endif
}

#ifdef _WIN32
// Windows: 简单的参数解析（替代 getopt）
static int win_optind = 1;
static char *win_optarg = NULL;

static int win_getopt(int argc, char *argv[], const char *optstring)
{
    static char *nextchar = NULL;
    
    if (nextchar == NULL || *nextchar == '\0') {
        if (win_optind >= argc) return -1;
        
        char *arg = argv[win_optind];
        if (arg[0] != '-' || arg[1] == '\0') return -1;
        
        nextchar = arg + 1;
    }
    
    int opt = *nextchar++;
    if (*nextchar == '\0') win_optind++;
    
    // 查找选项是否需要参数
    const char *p = strchr(optstring, opt);
    if (p == NULL) return '?';  // 未知选项
    
    if (p[1] == ':') {  // 需要参数
        if (*nextchar != '\0') {
            win_optarg = nextchar;
            nextchar = NULL;
            win_optind++;
        } else if (win_optind < argc) {
            win_optarg = argv[win_optind++];
        } else {
            return '?';  // 缺少参数
        }
    } else {
        win_optarg = NULL;
    }
    
    return opt;
}

#define getopt(argc, argv, optstring) win_getopt(argc, argv, optstring)
#define optarg win_optarg
#define optind win_optind
#endif

int main(int argc, char *argv[])
{
#ifdef _WIN32
    // Windows: 初始化 Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "error: WSAStartup failed\n");
        return 255;
    }
#endif

    int force_ai_family = AF_UNSPEC;
    long timeout_sec = 0, timeout_usec = 0;
    struct timeval timeout;
    int verbosity = 1;
    int count = 1;  // 默认发送1次

    if (argc < 3) {
        usage(argv[0]);
    }

    char *cptr;
    int c;
    while ((c = getopt(argc, argv, "qf:t:u:n:")) != -1) {
        switch (c) {
        case 'q':
            verbosity = 0;
            break;
        case 'f':
            cptr = NULL;
            long fam = strtol(optarg, &cptr, 10);
            if (cptr == optarg || (fam != 4 && fam != 6))
                usage(argv[0]);
            force_ai_family = fam == 4 ? AF_INET : AF_INET6;
            break;
        case 't':
            cptr = NULL;
            timeout_sec = strtol(optarg, &cptr, 10);
            if (cptr == optarg)
                usage(argv[0]);
            break;
        case 'u':
            cptr = NULL;
            timeout_usec = strtol(optarg, &cptr, 10);
            if (cptr == optarg)
                usage(argv[0]);
            break;
        case 'n':
            cptr = NULL;
            count = strtol(optarg, &cptr, 10);
            if (cptr == optarg || count <= 0)
                usage(argv[0]);
            break;
        default:
            usage(argv[0]);
            break;
        }
    }
    if (!argv[optind + 1]) {
        usage(argv[0]);
    }

    timeout.tv_sec = timeout_sec + timeout_usec / 1000000;
    timeout.tv_usec = timeout_usec % 1000000;

    struct hostinfo *host;
    int err;
    int retval = 0;
    if ((err = tcping_gethostinfo(argv[optind], argv[optind + 1],
                                  force_ai_family, &host)) != 0) {
        log(verbosity, stderr, "error: %s\n", gai_strerror(err));
        retval = 255;
        goto quit;
    }

    // 统计变量
    int success_count = 0;
    int fail_count = 0;
    double total_time = 0.0;
    double min_time = -1.0, max_time = -1.0;

    for (int i = 0; i < count; i++) {
        int sockfd = tcping_socket(host);
        double time_ms = 0.0;
        int result = tcping_connect(sockfd, host, &timeout, &time_ms);
        tcping_close(sockfd);

        switch (result) {
        case TCPING_ERROR:
            log(verbosity, stderr, "error: %s port %s: %s (%.3f ms)\n", host->name,
                host->serv, get_last_error_str(), time_ms);
            fail_count++;
            retval = 255;
            break;
        case TCPING_OPEN:
            log(verbosity, stdout, "%s port %s open. time=%.3f ms\n", host->name, host->serv, time_ms);
            success_count++;
            total_time += time_ms;
            if (min_time < 0 || time_ms < min_time) min_time = time_ms;
            if (max_time < 0 || time_ms > max_time) max_time = time_ms;
            break;
        case TCPING_CLOSED:
            log(verbosity, stdout, "%s port %s closed. (%.3f ms)\n", host->name, host->serv, time_ms);
            fail_count++;
            retval = 1;
            break;
        case TCPING_TIMEOUT:
            log(verbosity, stdout, "%s port %s user timeout. (%.3f ms)\n", host->name,
                host->serv, time_ms);
            fail_count++;
            retval = 2;
            break;
        default:
            log(verbosity, stderr, "error: invalid return value\n");
            retval = 255;
            break;
        }

        // 如果不是最后一次，稍微延迟一下
        if (i < count - 1) {
            sleep_ms(100);  // 100ms 间隔
        }
    }

    // 输出统计信息
    if (verbosity && count > 1) {
        double success_rate = (double)success_count / count * 100.0;
        double avg_time = success_count > 0 ? total_time / success_count : 0.0;
        printf("\n--- %s tcping statistics ---\n", host->name);
        printf("%d requests: %d success, %d failed, %.1f%% success rate\n",
               count, success_count, fail_count, success_rate);
        if (success_count > 0) {
            printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n", min_time, avg_time, max_time);
        }
    }

quit:
    tcping_freehostinfo(host);
#ifdef _WIN32
    WSACleanup();
#endif
    return retval;
}