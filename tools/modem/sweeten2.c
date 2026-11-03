/* One bounded IPv4 HTTP exchange, pinned to en2. No DNS, route changes or fallback. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return UINT64_MAX;
    return (uint64_t)t.tv_sec * 1000 + (unsigned)t.tv_nsec / 1000000;
}
static int wait_fd(int fd, short events, uint64_t end) {
    for (;;) {
        uint64_t now = now_ms();
        if (now >= end) { errno = ETIMEDOUT; return -1; }
        struct pollfd p = {fd, events, 0};
        int n = poll(&p, 1, (int)(end - now));
        if (n > 0) return 0;
        if (n < 0 && errno != EINTR) return -1;
    }
}
static int probe(const char *iface, const char *target, unsigned port) {
    int fd = -1, rc = 1;
    const char *stage = "interface";
    struct ifaddrs *list = NULL;
    struct sockaddr_in local = {.sin_len=sizeof(local), .sin_family=AF_INET};
    struct sockaddr_in remote = {.sin_len=sizeof(remote), .sin_family=AF_INET, .sin_port=htons((uint16_t)port)};
    unsigned index = if_nametoindex(iface), read_index = 0;
    uint64_t start = now_ms(), end = start + 10000;
    if (!index || start == UINT64_MAX || inet_pton(AF_INET, target, &remote.sin_addr) != 1) goto out;
    if (getifaddrs(&list)) goto out;
    for (struct ifaddrs *p=list; p; p=p->ifa_next) {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !strcmp(p->ifa_name, iface) && (p->ifa_flags & IFF_UP)) {
            local.sin_addr = ((struct sockaddr_in *)p->ifa_addr)->sin_addr;
            break;
        }
    }
    freeifaddrs(list); list = NULL;
    if (!local.sin_addr.s_addr) { errno = EADDRNOTAVAIL; goto out; }
    stage = "socket";
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) goto out;
    int one = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one)) goto out;
    stage = "interface binding";
    if (setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &index, sizeof index)) goto out;
    socklen_t n = sizeof read_index;
    if (getsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &read_index, &n) || n != sizeof read_index || read_index != index) goto out;
    if (bind(fd, (struct sockaddr *)&local, sizeof local)) goto out;
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK)) goto out;
    stage = "connect";
    if (connect(fd, (struct sockaddr *)&remote, sizeof remote)) {
        if (errno != EINPROGRESS || wait_fd(fd, POLLOUT, end)) goto out;
        int error = 0; n = sizeof error;
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &n)) goto out;
        if (error) { errno = error; goto out; }
    }
    struct sockaddr_in source; n = sizeof source;
    if (getsockname(fd, (struct sockaddr *)&source, &n) || source.sin_family != AF_INET || source.sin_addr.s_addr != local.sin_addr.s_addr) goto out;
    static const char request[] = "HEAD / HTTP/1.1\r\nHost: one.one.one.one\r\nConnection: close\r\n\r\n";
    stage = "send";
    for (size_t off=0; off < sizeof request-1;) {
        if (wait_fd(fd, POLLOUT, end)) goto out;
        ssize_t sent = send(fd, request+off, sizeof request-1-off, 0);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (sent <= 0) goto out;
        off += (size_t)sent;
    }
    stage = "HTTP status";
    char response[80] = {0}; size_t used = 0;
    while (used < sizeof response-1) {
        if (wait_fd(fd, POLLIN, end)) goto out;
        ssize_t got = recv(fd, response+used, sizeof response-1-used, 0);
        if (got < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (got <= 0) goto out;
        used += (size_t)got;
        if (memchr(response, '\n', used)) break;
    }
    if (used < 14 || !memchr(response, '\n', used) || (response[12] != ' ' && response[12] != '\r') || (memcmp(response,"HTTP/1.0 ",9) && memcmp(response,"HTTP/1.1 ",9)) ||
        response[9] < '1' || response[9] > '5' || response[10] < '0' || response[10] > '9' || response[11] < '0' || response[11] > '9') {
        errno = EPROTO; goto out;
    }
    printf("PASS interface=%s index=%u bound_index=%u HTTP=%c%c%c elapsed_ms=%llu\n", iface,index,read_index,response[9],response[10],response[11],(unsigned long long)(now_ms()-start));
    rc = 0;
out:
    if (rc) fprintf(stderr,"FAIL stage=%s errno=%d\n",stage,errno);
    if (list) freeifaddrs(list);
    if (fd >= 0) close(fd);
    return rc;
}
int main(int argc, char **argv) {
#ifdef SWEETEN2_HOST_TEST
    if (argc != 4) return 2;
    char *tail; unsigned long port = strtoul(argv[3], &tail, 10);
    if (*tail || !port || port > 65535) return 2;
    return probe(argv[1], argv[2], (unsigned)port);
#else
    (void)argv;
    if (argc != 1) return 2;
    return probe("en2", "1.1.1.1", 80);
#endif
}
