#include "gre.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    int n = sendto(g->udp, frame, length + 8, 0, (struct sockaddr *)&g->peer, sizeof(g->peer));
    if (n != (int)length + 8) {
        atomic_store(&g->failure, MATCH_GRE_UDP_SEND);
        atomic_store(&g->ready, -1);
        return -1;
    }
    return 0;
}
int gre_start(gre_context *g, int udp, pthread_mutex_t *lock, uint32_t ip, uint16_t port,
              uint32_t key, int initiator) {
    memset(g, 0, sizeof(*g));
    g->app = g->bridge = -1;
    g->udp = udp;
    g->io_lock = lock;
    g->key = key;
    g->initiator = initiator;
    g->peer.sin_family = AF_INET;
    g->peer.sin_addr.s_addr = htonl(ip);
    g->peer.sin_port = htons(port);
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair))
        return -1;
    g->app = pair[0];
    g->bridge = pair[1];
    fcntl(g->app, F_SETFL, O_NONBLOCK);
    fcntl(g->bridge, F_SETFL, O_NONBLOCK);
    atomic_init(&g->stop, 0);
    atomic_init(&g->ready, 0);
    atomic_init(&g->failure, 0);
    if (pthread_create(&g->thread, NULL, match_lwip_worker, g)) {
        close(g->app);
        close(g->bridge);
        g->app = g->bridge = -1;
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
    if (g->app >= 0)
        close(g->app);
    if (g->bridge >= 0)
        close(g->bridge);
    g->app = g->bridge = -1;
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
    return recvfrom(g->udp, packet, capacity, MSG_DONTWAIT, (struct sockaddr *)from, &length);
}
