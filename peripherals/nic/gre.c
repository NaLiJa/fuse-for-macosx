#include "config.h"
#include "gre.h"
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <sys/socket.h>
#endif
unsigned long match_gre_dropped_packets;
unsigned long match_gre_tx_packets, match_gre_rx_packets, match_gre_tcp_bytes,
    match_gre_icmp_replies;
int gre_output(gre_context *g, const void *packet, unsigned length) {
    unsigned char frame[1280];
    if (length > sizeof(frame) - 8)
        return -1;
    frame[0] = 0x20;
    frame[1] = 0;
    frame[2] = 8;
    frame[3] = 0;
    uint32_t key = htonl(g->key);
    memcpy(frame + 4, &key, 4);
    memcpy(frame + 8, packet, length);
    /* Explicit fault injection for reproducible transport tests, disabled by default. */
    const char *drop = getenv("SPX_MATCH_TEST_DROP_EVERY");
    unsigned interval = drop ? (unsigned)strtoul(drop, NULL, 10) : 0;
    match_gre_tx_packets++;
    if (interval && match_gre_tx_packets % interval == 0) {
        match_gre_dropped_packets++;
        return 0;
    }
    int n = sendto(g->udp, (const char *)frame, length + 8, 0,
                   (struct sockaddr *)&g->peer, sizeof(g->peer));
    if (n != (int)length + 8) {
        atomic_store(&g->failure, MATCH_GRE_UDP_SEND);
        atomic_store(&g->ready, -1);
        return -1;
    }
    return 0;
}
/* Winsock has no socketpair. A loopback TCP pair provides the same full-duplex
 * stream and works with the W5100 worker's select() on every host. */
static int bridge_pair(compat_socket_t pair[2]) {
#ifdef WIN32
    compat_socket_t listener = compat_socket_invalid;
    struct sockaddr_in address = {.sin_family = AF_INET};
    socklen_t length = sizeof(address);
    pair[0] = pair[1] = compat_socket_invalid;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == compat_socket_invalid ||
        bind(listener, (struct sockaddr *)&address, sizeof(address)) ||
        getsockname(listener, (struct sockaddr *)&address, &length) || listen(listener, 1))
        goto fail;
    pair[0] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (pair[0] == compat_socket_invalid ||
        connect(pair[0], (struct sockaddr *)&address, sizeof(address)))
        goto fail;
    pair[1] = accept(listener, NULL, NULL);
    if (pair[1] == compat_socket_invalid)
        goto fail;
    compat_socket_close(listener);
    listener = compat_socket_invalid;
    int enabled = 1;
    setsockopt(pair[0], IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, sizeof(enabled));
    setsockopt(pair[1], IPPROTO_TCP, TCP_NODELAY, (const char *)&enabled, sizeof(enabled));
#else
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
        return -1;
#endif
    /* The compat routine uses 1 for nonblocking, despite its historical name. */
    if (compat_socket_blocking_mode(pair[0], 1) ||
        compat_socket_blocking_mode(pair[1], 1))
        goto fail;
    return 0;
fail:
#ifdef WIN32
    if (listener != compat_socket_invalid)
        compat_socket_close(listener);
#endif
    if (pair[0] != compat_socket_invalid)
        compat_socket_close(pair[0]);
    if (pair[1] != compat_socket_invalid)
        compat_socket_close(pair[1]);
    return -1;
}
int gre_start(gre_context *g, compat_socket_t udp, pthread_mutex_t *lock, uint32_t ip, uint16_t port,
              uint32_t key, int initiator) {
    memset(g, 0, sizeof(*g));
    g->app = g->bridge = compat_socket_invalid;
    g->udp = udp;
    g->io_lock = lock;
    g->key = key;
    g->initiator = initiator;
    g->peer.sin_family = AF_INET;
    g->peer.sin_addr.s_addr = htonl(ip);
    g->peer.sin_port = htons(port);
    compat_socket_t pair[2];
    if (bridge_pair(pair))
        return -1;
    g->app = pair[0];
    g->bridge = pair[1];
    atomic_init(&g->stop, 0);
    atomic_init(&g->ready, 0);
    atomic_init(&g->failure, 0);
    if (pthread_create(&g->thread, NULL, match_lwip_worker, g)) {
        compat_socket_close(g->app);
        compat_socket_close(g->bridge);
        g->app = g->bridge = compat_socket_invalid;
        return -1;
    }
    g->created = 1;
    return 0;
}
void gre_stop(gre_context *g) {
    if (g->created) {
        atomic_store(&g->stop, 1);
        pthread_join(g->thread, NULL);
        g->created = 0;
    }
    if (g->app != compat_socket_invalid)
        compat_socket_close(g->app);
    if (g->bridge != compat_socket_invalid)
        compat_socket_close(g->bridge);
    g->app = g->bridge = compat_socket_invalid;
}

/* Caller holds io_lock: STUN discovery and the worker share one UDP receive queue. */
void gre_queue(gre_context *g, const void *packet, unsigned length,
               const struct sockaddr_in *from) {
    if (!g->created || length > 1280 || g->queued == 16)
        return;
    unsigned slot = (g->head + g->queued) % 16;
    memcpy(g->input[slot].packet, packet, length);
    g->input[slot].length = length;
    g->input[slot].from = *from;
    g->queued++;
}
int gre_read(gre_context *g, void *packet, unsigned capacity, struct sockaddr_in *from) {
    if (g->queued) {
        unsigned slot = g->head, length = g->input[slot].length;
        if (length > capacity)
            length = capacity;
        memcpy(packet, g->input[slot].packet, length);
        *from = g->input[slot].from;
        g->head = (g->head + 1) % 16;
        g->queued--;
        return (int)length;
    }
    socklen_t length = sizeof(*from);
    return recvfrom(g->udp, (char *)packet, capacity, 0, (struct sockaddr *)from, &length);
}
