/* The K5 measurement's phone side (plans/modem/audit-2285-report.md, ruling b), pinned to en2 like sweeten2. No DNS, routes or
 * fallback; numeric addresses only.
 *   sweetk5 udp  HOST PORT TOKEN WAIT_S PPS SIZE LISTEN_S [MS COUNT GAP_MS]
 *                                                          one opener asks the server to wait WAIT_S, then send PPS packets/s of
 *                                                          SIZE bytes back to this mapping, for 1 s or as COUNT bursts of MS ms
 *                                                          GAP_MS apart; counts what arrives in LISTEN_S, per burst (a gap of
 *                                                          500 ms or more starts the next count)
 *   sweetk5 down HOST PORT TOKEN SECONDS                   bulk TCP download for SECONDS, counted
 *   sweetk5 up   HOST PORT TOKEN SECONDS                   bulk TCP upload for SECONDS, counted
 * Prints one PASS line with the counts, or FAIL with the stage and errno. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifndef K5_IF
#define K5_IF "en2"   /* the host test builds with -DK5_IF=\"lo0\"; the phone build never sets it */
#endif

static uint64_t now_ms(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return UINT64_MAX;
    return (uint64_t)t.tv_sec * 1000 + (unsigned)t.tv_nsec / 1000000;
}
static const char *stage = "arguments";
static int fail(void) { fprintf(stderr, "FAIL stage=%s errno=%d\n", stage, errno); return 1; }
static unsigned long number(const char *s, unsigned long lo, unsigned long hi) {
    char *tail; errno = 0; unsigned long v = strtoul(s, &tail, 10);
    if (errno || *tail || v < lo || v > hi) { errno = EINVAL; return 0; }
    return v;
}
/* a socket of the given type bound to K5_IF's IPv4 address and pinned to that interface */
static int en2_socket(int type, struct sockaddr_in *remote, const char *host, unsigned port) {
    unsigned index = if_nametoindex(K5_IF), read_index = 0;
    struct sockaddr_in local = {.sin_len = sizeof local, .sin_family = AF_INET};
    *remote = (struct sockaddr_in){.sin_len = sizeof *remote, .sin_family = AF_INET, .sin_port = htons((uint16_t)port)};
    stage = "interface";
    if (!index || inet_pton(AF_INET, host, &remote->sin_addr) != 1) { errno = EINVAL; return -1; }
    struct ifaddrs *list = NULL;
    if (getifaddrs(&list)) return -1;
    for (struct ifaddrs *p = list; p; p = p->ifa_next)
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !strcmp(p->ifa_name, K5_IF) && (p->ifa_flags & IFF_UP)) {
            local.sin_addr = ((struct sockaddr_in *)p->ifa_addr)->sin_addr;
            break;
        }
    freeifaddrs(list);
    if (!local.sin_addr.s_addr) { errno = EADDRNOTAVAIL; return -1; }
    stage = "socket";
    int fd = socket(AF_INET, type, 0), one = 1;
    if (fd < 0) return -1;
    stage = "interface binding";
    socklen_t n = sizeof read_index;
    if (setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one) || setsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &index, sizeof index) ||
        getsockopt(fd, IPPROTO_IP, IP_BOUND_IF, &read_index, &n) || read_index != index || bind(fd, (struct sockaddr *)&local, sizeof local)) {
        close(fd); return -1;
    }
    if (type == SOCK_DGRAM) { int big = 4 << 20; (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big); }
    return fd;
}
static int udp(char **a, int train) {
    unsigned long port = number(a[1], 1, 65535), wait_s = number(a[3], 1, 30), pps = number(a[4], 1, 200000), size = number(a[5], 28, 1472),
                  listen_s = number(a[6], 2, 120), ms = train ? number(a[7], 1, 1000) : 1, count = train ? number(a[8], 1, 10) : 1,
                  gap = train ? number(a[9], 1, 5000) : 1;
    if (!port || !wait_s || !pps || !size || !listen_s || !ms || !count || !gap || strlen(a[2]) > 64) { stage = "arguments"; return fail(); }
    struct sockaddr_in remote;
    int fd = en2_socket(SOCK_DGRAM, &remote, a[0], (unsigned)port);
    if (fd < 0) return fail();
    char opener[160];
    int len = train ? snprintf(opener, sizeof opener, "K5 %s udp %lu %lu %lu %lu %lu %lu", a[2], wait_s, pps, size, ms, count, gap)
                    : snprintf(opener, sizeof opener, "K5 %s udp %lu %lu %lu", a[2], wait_s, pps, size);
    stage = "opener";
    if (sendto(fd, opener, (size_t)len, 0, (struct sockaddr *)&remote, sizeof remote) != len) { close(fd); return fail(); }
    uint64_t start = now_ms(), end = start + listen_s * 1000, first = 0, last = 0, bytes = 0;
    unsigned long got = 0, from_other = 0, groups[10] = {0}; unsigned ngroups = 0; char report[160] = "none";
    static char buf[2048];
    for (uint64_t t; (t = now_ms()) < end;) {  /* one clock read per pass, so the wait can't wrap (audit-k5-usb-report.md N2) */
        struct pollfd p = {fd, POLLIN, 0};
        int n = poll(&p, 1, (int)(end - t));
        if (n <= 0) { if (n < 0 && errno != EINTR) break; continue; }
        struct sockaddr_in from; socklen_t fl = sizeof from;
        ssize_t r = recvfrom(fd, buf, sizeof buf - 1, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
        if (r < 0) continue;
        if (from.sin_addr.s_addr != remote.sin_addr.s_addr || from.sin_port != remote.sin_port) { from_other++; continue; }
        if (r >= 9 && !memcmp(buf, "K5REPORT ", 9)) { buf[r] = 0; snprintf(report, sizeof report, "%s", buf + 9); continue; }
        uint64_t at = now_ms();
        if (!got) first = at;
        if (!got || at - last >= 500) ngroups += ngroups < 10;
        groups[ngroups - 1]++;
        last = at; got++; bytes += (uint64_t)r;
    }
    close(fd);
    char list[128] = "0";
    for (unsigned i = 0, o = 0; i < ngroups; i++) o += (unsigned)snprintf(list + o, sizeof list - o, "%s%lu", i ? "/" : "", groups[i]);
    printf("PASS udp got=%lu bytes=%llu first_ms=%llu span_ms=%llu groups=%s other=%lu server=[%s]\n", got, (unsigned long long)bytes,
           (unsigned long long)(got ? first - start : 0), (unsigned long long)(got ? last - first : 0), list, from_other, report);
    return 0;
}
static int tcp(char **a, int down) {
    unsigned long port = number(a[1], 1, 65535), seconds = number(a[3], 1, 60);
    if (!port || !seconds || strlen(a[2]) > 64) { stage = "arguments"; return fail(); }
    struct sockaddr_in remote;
    int fd = en2_socket(SOCK_STREAM, &remote, a[0], (unsigned)port);
    if (fd < 0) return fail();
    stage = "connect";
    struct timeval tv = {5, 0};
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv); (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    if (connect(fd, (struct sockaddr *)&remote, sizeof remote)) { close(fd); return fail(); }
    char hello[128];
    int len = snprintf(hello, sizeof hello, "K5 %s %s %lu\n", a[2], down ? "down" : "up", seconds);
    stage = "hello";
    if (send(fd, hello, (size_t)len, 0) != len) { close(fd); return fail(); }
    static char buf[65536];
    uint64_t start = now_ms(), end = start + seconds * 1000 + (down ? 5000 : 0), bytes = 0;
    stage = down ? "download" : "upload";
    while (now_ms() < end) {
        ssize_t r = down ? recv(fd, buf, sizeof buf, 0) : send(fd, buf, sizeof buf, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        bytes += (uint64_t)r;
        if (!down && now_ms() >= start + seconds * 1000) break;
    }
    uint64_t ms = now_ms() - start;
    close(fd);
    if (!bytes || (!down && ms < seconds * 1000)) return fail();  /* an upload must run its full time */
    printf("PASS %s bytes=%llu ms=%llu mbps=%.1f\n", down ? "down" : "up", (unsigned long long)bytes, (unsigned long long)ms,
           ms ? (double)bytes * 8 / ((double)ms * 1000) : 0.0);
    return 0;
}
int main(int argc, char **argv) {
    if ((argc == 9 || argc == 12) && !strcmp(argv[1], "udp")) return udp(argv + 2, argc == 12);
    if (argc == 6 && !strcmp(argv[1], "down")) return tcp(argv + 2, 1);
    if (argc == 6 && !strcmp(argv[1], "up")) return tcp(argv + 2, 0);
    fprintf(stderr, "usage: sweetk5 udp HOST PORT TOKEN WAIT_S PPS SIZE LISTEN_S [MS COUNT GAP_MS] | down|up HOST PORT TOKEN SECONDS\n");
    return 2;
}
