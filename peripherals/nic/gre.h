#ifndef MATCH_GRE_H
#define MATCH_GRE_H
#include "compat.h"
#ifdef WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
typedef struct gre_context {
    compat_socket_t udp, bridge, app;
    uint32_t key;
    struct sockaddr_in peer;
    pthread_t thread;
    pthread_mutex_t *io_lock;
    atomic_int stop, ready, failure;
    int created, initiator;
    unsigned queued, head;
    struct {
        unsigned length;
        struct sockaddr_in from;
        unsigned char packet[1280];
    } input[16];
} gre_context;
enum { MATCH_GRE_UDP_TIMEOUT = 1, MATCH_GRE_TCP_TIMEOUT, MATCH_GRE_TCP_RESET,
       MATCH_GRE_TCP_ABORTED, MATCH_GRE_MEMORY, MATCH_GRE_TCP_SETUP, MATCH_GRE_UDP_SEND };
extern unsigned long match_gre_dropped_packets;
extern unsigned long match_gre_tx_packets, match_gre_rx_packets, match_gre_tcp_bytes,
    match_gre_icmp_replies;
/**
 * Initialize a fresh caller-owned context and start its lwIP bridge thread.
 * udp must be nonblocking. udp and lock are borrowed and must outlive the
 * worker. ip/port are the outer
 * peer endpoint in host order; key identifies the tunnel; initiator selects the
 * inner TCP role. Return 0 when started or -1 on local setup failure.
 * Observe atomic ready (0 pending, 1 usable, -1 failed) before using g->app.
 * Stop any previous context before reusing it; only one lwIP tunnel is supported.
 */
int gre_start(gre_context *g, compat_socket_t udp, pthread_mutex_t *lock, uint32_t ip, uint16_t port,
              uint32_t key, int initiator);
/**
 * Signal and join g's worker, then close its app/bridge descriptors.
 * May block; caller must have initialized g. Does not close borrowed g->udp or
 * destroy g->io_lock. Call before releasing those resources or reusing g.
 */
void gre_stop(gre_context *g);
/**
 * Copy a datagram and source into g's bounded queue while STUN owns reception.
 * Caller must hold g->io_lock. Silently drop if stopped, longer than 1280 bytes,
 * or all 16 slots are occupied. packet/from remain owned by the caller.
 */
void gre_queue(gre_context *g, const void *packet, unsigned length, const struct sockaddr_in *from);
/**
 * Read one queued or nonblocking outer datagram into packet[capacity] and from.
 * Caller holds g->io_lock. Return bytes copied (a queued packet may be truncated),
 * or recvfrom's negative result/platform socket error when no packet is available or on error.
 * Buffers are caller-owned; this function does not validate GRE contents.
 */
int gre_read(gre_context *g, void *packet, unsigned capacity, struct sockaddr_in *from);
/**
 * Encapsulate length bytes of inner IP in keyed GRE and send on g's UDP socket.
 * packet is borrowed. Return 0 if sent (or intentionally dropped by configured
 * test injection), -1 if too large or send failed. A send failure also sets the
 * context's failure/ready atomics. Called by lwIP output, not the application.
 */
int gre_output(gre_context *g, const void *packet, unsigned length);
/**
 * Thread entry for the tunnel bridge; argument points to a live gre_context.
 * Runs ICMP path checks, inner TCP setup, and app/UDP forwarding until stop or
 * failure. Publishes ready/failure atomically. Returns NULL; context, UDP socket,
 * and mutex remain caller-owned and must survive until gre_stop joins it.
 */
void *match_lwip_worker(void *argument);
#endif
