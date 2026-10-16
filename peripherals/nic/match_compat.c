#include "config.h"
#ifdef WIN32
#define _CRT_RAND_S
#endif
#include "match_compat.h"
#include "stun.h"
#include "match.h"
#include "../http/http_sck.h"
#include "gre.h"
#include "w5100.h"
#include "w5100_internals.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#ifndef __APPLE__
#include <sys/random.h>
#endif
#endif
extern nic_w5100_t *spectranet_match_nic(void);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER, io_lock = PTHREAD_MUTEX_INITIALIZER;
static compat_socket_t outer = (compat_socket_t)-1;
static int claimed = -1, attached;
static uint32_t generation;
static gre_context gre = {.app = (compat_socket_t)-1, .bridge = (compat_socket_t)-1};
static char device[17];
/* Deployment defaults; local test services are selected through the launch
 * environment, never through a shared Xcode scheme. */
static const char *endpoint(const char *variable, const char *fallback) {
    const char *value = getenv(variable);
    return value && value[0] ? value : fallback;
}
void match_compat_lock(void) { pthread_mutex_lock(&lock); }
void match_compat_unlock(void) { pthread_mutex_unlock(&lock); }
uint64_t match_compat_time(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
void match_compat_random(void *data, size_t n) {
#ifdef WIN32
    unsigned char *bytes = data;
    while (n) {
        unsigned value;
        if (rand_s(&value))
            abort();
        size_t count = n < sizeof(value) ? n : sizeof(value);
        memcpy(bytes, &value, count);
        bytes += count;
        n -= count;
    }
#elif defined(__APPLE__)
    arc4random_buf(data, n);
#else
    size_t done = 0;
    while (done < n) {
        ssize_t k = getrandom((char *)data + done, n - done, 0);
        if (k > 0)
            done += k;
        else if (errno != EINTR)
            abort();
    }
#endif
}
const char *match_compat_device(void) {
    if (!device[0]) {
        const char *value = getenv("SPX_MATCH_DEVICE");
        if (value && strlen(value) == 16 && strspn(value, "0123456789abcdefABCDEF") == 16)
            snprintf(device, 17, "%s", value);
        else {
            /* Each emulator process is a distinct device. Keep the identity
             * stable through resets/matches, but never share it through HOME. */
            unsigned char random[8];
            match_compat_random(random, sizeof(random));
            for (int i = 0; i < 8; i++)
                snprintf(device + 2 * i, 3, "%02x", random[i]);
        }
    }
    return device;
}
static void name_path(char *path, size_t n) {
    const char *home = getenv("HOME");
#ifdef WIN32
    if (!home || !home[0])
        home = getenv("USERPROFILE");
#endif
    const char *fixed = getenv("SPX_MATCH_DEVICE");
    if (fixed && strlen(fixed) == 16 && strspn(fixed, "0123456789abcdefABCDEF") == 16)
        snprintf(path, n, "%s/.fusex-match-name-%s", home ? home : ".", match_compat_device());
    else
        snprintf(path, n, "%s/.fusex-match-name", home ? home : ".");
}
void match_compat_load_name(char name[24]) {
    char path[1024];
    name_path(path, sizeof(path));
    memset(name, 0, 24);
    FILE *f = fopen(path, "r");
    if (f) {
        size_t n = fread(name, 1, 23, f);
        name[n] = 0;
        fclose(f);
    }
}
int match_compat_save_name(const char *name) {
    char path[1024];
    name_path(path, sizeof(path));
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    int ok = fwrite(name, 1, strlen(name), f) == strlen(name);
    int result = fclose(f);
    return ok && !result ? 0 : -1;
}
int match_compat_claim(unsigned index, uint32_t *gen, int *udp) {
    nic_w5100_t *nic = spectranet_match_nic();
    if (!nic || index >= 4 || claimed >= 0)
        return -1;
    nic_w5100_socket_t *s = &nic->socket[index];
    pthread_mutex_lock(&s->lock);
    if ((s->mode != W5100_SOCKET_MODE_TCP && s->mode != W5100_SOCKET_MODE_UDP) ||
        (s->mode == W5100_SOCKET_MODE_TCP && s->state != W5100_SOCKET_STATE_INIT) ||
        (s->backend_protocol != W5100_SOCKET_PROTOCOL_TCP &&
         s->backend_protocol != W5100_SOCKET_PROTOCOL_GRE_TCP)) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    outer = socket(AF_INET, SOCK_DGRAM, 0);
    if (outer == compat_socket_invalid) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    struct sockaddr_in bindaddr = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY};
    if (bind(outer, (struct sockaddr *)&bindaddr, sizeof(bindaddr))) {
        compat_socket_close(outer);
        outer = compat_socket_invalid;
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    socklen_t local_length = sizeof(bindaddr);
    if (!getsockname(outer, (struct sockaddr *)&bindaddr, &local_length))
        memcpy(s->port, &bindaddr.sin_port, 2);
    if (compat_socket_blocking_mode(outer, 1)) {
        compat_socket_close(outer);
        outer = compat_socket_invalid;
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    *udp = s->mode == W5100_SOCKET_MODE_UDP;
    *gen = generation = s->generation;
    compat_socket_close(s->fd);
    s->fd = compat_socket_invalid;
    s->ok_for_io = 0;
    s->protocol = s->backend_protocol = *udp ? 0 : 0xf2;
    if (!*udp) {
        s->dip[0] = 198;
        s->dip[1] = 18;
        s->dip[2] = 0;
        s->dip[3] = 1;
        s->dport[0] = s->dport[1] = 0;
    }
    claimed = index;
    attached = 0;
    pthread_mutex_unlock(&s->lock);
    return 0;
}
int match_compat_valid(unsigned index, uint32_t gen) {
    nic_w5100_t *nic = spectranet_match_nic();
    if (!nic || index >= 4)
        return 0;
    nic_w5100_socket_t *s = &nic->socket[index];
    pthread_mutex_lock(&s->lock);
    int valid = s->generation == gen && s->mode != W5100_SOCKET_MODE_CLOSED;
    pthread_mutex_unlock(&s->lock);
    return valid;
}
static void pause_udp(int paused) {
    nic_w5100_t *nic = spectranet_match_nic();
    if (!nic || claimed < 0 || !attached)
        return;
    nic_w5100_socket_t *s = &nic->socket[claimed];
    pthread_mutex_lock(&s->lock);
    if (s->match_udp && s->generation == generation)
        s->match_paused = paused;
    pthread_mutex_unlock(&s->lock);
    compat_socket_selfpipe_wake(nic->selfpipe);
}
static void queue_udp(const void *packet, unsigned length, const struct sockaddr_in *from) {
    if (claimed < 0 || !attached || length > 1200)
        return;
    nic_w5100_socket_t *s = &spectranet_match_nic()->socket[claimed];
    pthread_mutex_lock(&s->lock);
    if (s->generation == generation && s->match_udp &&
        !memcmp(&from->sin_addr.s_addr, s->match_ip, 4) &&
        !memcmp(&from->sin_port, s->match_port, 2) && length + 8 <= 0x800u - s->rx_rsr) {
        unsigned char bytes[1208];
        memcpy(bytes, &from->sin_addr.s_addr, 4);
        memcpy(bytes + 4, &from->sin_port, 2);
        bytes[6] = length >> 8;
        bytes[7] = length;
        memcpy(bytes + 8, packet, length);
        unsigned offset = (s->old_rx_rd + s->rx_rsr) & 0x7ff;
        for (unsigned i = 0; i < length + 8; i++)
            s->rx_buffer[(offset + i) & 0x7ff] = bytes[i];
        s->rx_rsr += length + 8;
        s->ir |= 4;
    }
    pthread_mutex_unlock(&s->lock);
}
struct stun_socket {
    struct sockaddr_in server;
};
static int stun_send(void *argument, const uint8_t *packet, size_t n) {
    struct stun_socket *s = argument;
    return sendto(outer, (const char *)packet, n, 0,
                  (struct sockaddr *)&s->server, sizeof(s->server)) == (int)n
               ? 0
               : -1;
}
static int stun_receive(void *argument, uint8_t *packet, size_t capacity, unsigned timeout) {
    struct stun_socket *s = argument;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(outer, &set);
    struct timeval wait = {0, (int)timeout * 1000};
    if (select((int)(outer + 1), &set, NULL, NULL, &wait) <= 0)
        return -1;
    struct sockaddr_in from;
    socklen_t length = sizeof(from);
    int n = recvfrom(outer, (char *)packet, capacity, 0, (struct sockaddr *)&from, &length);
    if (n >= 0 && from.sin_addr.s_addr == s->server.sin_addr.s_addr &&
        from.sin_port == s->server.sin_port)
        return n;
    if (n >= 0) {
        gre_queue(&gre, packet, (unsigned)n, &from);
        queue_udp(packet, (unsigned)n, &from);
    }
    return -1;
}
static uint64_t stun_now(void *argument) {
    (void)argument;
    return match_compat_time();
}
static void stun_random(void *argument, void *data, size_t length) {
    (void)argument;
    match_compat_random(data, length);
}
int match_compat_stun(uint32_t *ip, uint16_t *port) {
    const char *host = endpoint("SPX_MATCH_STUN_HOST", "stun.l.google.com");
    const char *service = endpoint("SPX_MATCH_STUN_PORT", "19302");
    struct addrinfo hints = {.ai_family = AF_INET, .ai_socktype = SOCK_DGRAM}, *addresses = NULL;
    if (outer == compat_socket_invalid || getaddrinfo(host, service, &hints, &addresses))
        return -1;
    struct stun_socket socket = {.server = *(struct sockaddr_in *)addresses->ai_addr};
    stun_io io = {.context = &socket,
                  .send = stun_send,
                  .receive = stun_receive,
                  .now = stun_now,
                  .random = stun_random};
    pause_udp(1);
    pthread_mutex_lock(&io_lock);
    int result = stun_discover(&io, ip, port);
    pthread_mutex_unlock(&io_lock);
    pause_udp(0);
    freeaddrinfo(addresses);
    return result;
}

static int write_all(httpc_options_t *o, void *socket, const char *p, size_t n) {
    while (n) {
        size_t count = n;
        if (o->write(o, socket, (const unsigned char *)p, &count) < 0 || !count)
            return -1;
        p += count;
        n -= count;
    }
    return 0;
}
int match_compat_post(const char *body, const char *instance, unsigned sequence, char *response,
                      size_t capacity) {
    const char *url = endpoint("SPX_MATCH_API", "https://match.spectranext.net/match");
    int tls = !strncmp(url, "https://", 8);
    const char *authority = url + (tls ? 8 : 7);
    if (!tls && (strncmp(url, "http://", 7) ||
                 strcmp(endpoint("SPX_MATCH_LOCAL_HTTP", "0"), "1")))
        return -1;
    const char *slash = strchr(authority, '/');
    if (!slash)
        return -1;
    char host[256];
    size_t n = slash - authority;
    if (n >= sizeof(host))
        return -1;
    memcpy(host, authority, n);
    host[n] = 0;
    unsigned port = tls ? 443 : 80;
    char *colon = strchr(host, ':');
    if (colon) {
        *colon++ = 0;
        port = (unsigned)strtoul(colon, NULL, 10);
        if (!port || port > 65535)
            return -1;
    }
    httpc_options_t options;
    sck_http_options_init(&options);
    options.context = &options;
    options.open = sck_http_open_bounded;
    void *socket = NULL;
    if (options.open(&options, &socket, NULL, host, port, tls) < 0)
        return -1;
    char header[1024];
    int length = snprintf(
        header, sizeof(header),
        "POST %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: "
        "%zu\r\nX-Match-Instance: %s\r\nX-Match-Request: %u\r\nConnection: close\r\n\r\n",
        slash, host, strlen(body), instance, sequence);
    int status = -1;
    if (length < 0 || (size_t)length >= sizeof(header) ||
        write_all(&options, socket, header, length) ||
        write_all(&options, socket, body, strlen(body)))
        goto done;
    size_t used = 0;
    while (used + 1 < sizeof(header)) {
        size_t one = 1;
        if (options.read(&options, socket, (unsigned char *)header + used, &one) < 0 || one != 1)
            goto done;
        used++;
        header[used] = 0;
        if (used >= 4 && !memcmp(header + used - 4, "\r\n\r\n", 4))
            break;
    }
    if (!strstr(header, "\r\n\r\n") || sscanf(header, "HTTP/%*s %d", &status) != 1) {
        status = -1;
        goto done;
    }
    char *cl = NULL;
    for (char *line = strchr(header, '\n'); line && line[1]; line = strchr(line + 1, '\n')) {
        const char *name = "content-length:";
        unsigned i = 0;
        while (name[i] && line[1 + i] && tolower((unsigned char)line[1 + i]) == name[i])
            i++;
        if (!name[i]) {
            cl = line + 1;
            break;
        }
    }
    if (!cl) {
        status = -1;
        goto done;
    }
    size_t remaining = strtoul(cl + 15, NULL, 10);
    if (remaining >= capacity) {
        status = -1;
        goto done;
    }
    used = 0;
    while (remaining) {
        size_t count = remaining;
        if (options.read(&options, socket, (unsigned char *)response + used, &count) < 0 ||
            !count) {
            status = -1;
            goto done;
        }
        used += count;
        remaining -= count;
    }
    response[used] = 0;
done:
    options.close(&options, socket);
    return status;
}
int match_compat_start(const char *connection, const char *peer, uint32_t ip, uint16_t port,
                       int udp) {
    uint32_t key;
    char hex[9];
    memcpy(hex, connection, 8);
    hex[8] = 0;
    key = (uint32_t)strtoul(hex, NULL, 16);
    if (udp) {
        nic_w5100_socket_t *s = &spectranet_match_nic()->socket[claimed];
        pthread_mutex_lock(&s->lock);
        if (s->generation != generation) {
            pthread_mutex_unlock(&s->lock);
            return -1;
        }
#ifdef WIN32
        WSAPROTOCOL_INFO info;
        s->fd = compat_socket_invalid;
        if (!WSADuplicateSocket(outer, GetCurrentProcessId(), &info))
            s->fd = WSASocket(FROM_PROTOCOL_INFO, FROM_PROTOCOL_INFO,
                              FROM_PROTOCOL_INFO, &info, 0, WSA_FLAG_OVERLAPPED);
#else
        s->fd = dup(outer);
#endif
        if (s->fd == compat_socket_invalid) {
            pthread_mutex_unlock(&s->lock);
            return -1;
        }
        s->socket_bound = 1;
        s->dip[0] = ip >> 24;
        s->dip[1] = ip >> 16;
        s->dip[2] = ip >> 8;
        s->dip[3] = ip;
        s->dport[0] = port >> 8;
        s->dport[1] = port;
        memcpy(s->match_ip, s->dip, 4);
        memcpy(s->match_port, s->dport, 2);
        s->match_udp = 1;
        attached = 1;
        pthread_mutex_unlock(&s->lock);
        return 0;
    }
    return gre_start(&gre, outer, &io_lock, ip, port, key, strcmp(match_compat_device(), peer) < 0);
}
int match_compat_connected(unsigned index, uint32_t gen, int udp) {
    nic_w5100_t *nic = spectranet_match_nic();
    if (!nic || index >= 4 || claimed != (int)index || !attached) return 0;
    nic_w5100_socket_t *s = &nic->socket[index];
    pthread_mutex_lock(&s->lock);
    int connected = s->generation == gen && s->state ==
        (udp ? W5100_SOCKET_STATE_UDP : W5100_SOCKET_STATE_ESTABLISHED);
    pthread_mutex_unlock(&s->lock);
    return connected;
}
const char *match_compat_error(void) {
    switch (atomic_load(&gre.failure)) {
    case MATCH_GRE_UDP_TIMEOUT: return "Peer UDP unreachable";
    case MATCH_GRE_TCP_TIMEOUT: return "TCP handshake timed out";
    case MATCH_GRE_TCP_RESET: return "Peer reset TCP connection";
    case MATCH_GRE_TCP_ABORTED: return "TCP connection aborted";
    case MATCH_GRE_MEMORY: return "TCP memory exhausted";
    case MATCH_GRE_UDP_SEND: return "UDP send failed";
    case MATCH_GRE_TCP_SETUP: return "TCP setup failed";
    default: return "Direct connection failed";
    }
}
int match_compat_ready(void) {
    if (attached)
        return 1;
    int ready = atomic_load(&gre.ready);
    if (ready <= 0)
        return ready;
    nic_w5100_socket_t *s = &spectranet_match_nic()->socket[claimed];
    pthread_mutex_lock(&s->lock);
    if (s->generation != generation) {
        pthread_mutex_unlock(&s->lock);
        return -1;
    }
    s->fd = gre.app;
    gre.app = compat_socket_invalid;
    s->state = W5100_SOCKET_STATE_ESTABLISHED;
    /* CONNECT completion is consumed here, just like ordinary ROM CONNECT. */
    s->ir &= ~1;
    s->socket_bound = 1;
    s->ok_for_io = 0;
    attached = 1;
    pthread_mutex_unlock(&s->lock);
    compat_socket_selfpipe_wake(spectranet_match_nic()->selfpipe);
    return 1;
}
void match_compat_release(void) {
    gre_stop(&gre);
    if (outer != compat_socket_invalid)
        compat_socket_close(outer);
    outer = compat_socket_invalid;
    nic_w5100_t *nic = spectranet_match_nic();
    if (nic && claimed >= 0) {
        nic_w5100_socket_t *s = &nic->socket[claimed];
        pthread_mutex_lock(&s->lock);
        if (s->generation == generation) {
            if (s->fd != compat_socket_invalid)
                compat_socket_close(s->fd);
            s->fd = compat_socket_invalid;
            s->ok_for_io = 0;
            s->socket_bound = 0;
            s->match_udp = 0;
            s->protocol = s->backend_protocol = 0;
            s->state =
                s->mode == W5100_SOCKET_MODE_UDP ? W5100_SOCKET_STATE_UDP : W5100_SOCKET_STATE_INIT;
        }
        pthread_mutex_unlock(&s->lock);
    }
    claimed = -1;
    attached = 0;
}
