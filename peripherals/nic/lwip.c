#include "config.h"
/* One worker owns the NO_SYS lwIP stack for the device's single active match. */
#include "gre.h"
#include "icmp.h"
#include "lwip/init.h"
#include "lwip/ip4.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static struct netif tunnel;
static struct tcp_pcb *stream, *listener;
static int stream_connected;
static unsigned char incoming[16384];
static unsigned incoming_n, incoming_offset;
static gre_context *current;
u32_t sys_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (u32_t)((uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
unsigned int match_lwip_rand(void) { return (unsigned)rand(); }
static err_t output(struct netif *n, struct pbuf *p, const ip4_addr_t *ip) {
    (void)n;
    (void)ip;
    unsigned char bytes[1272];
    if (p->tot_len > sizeof(bytes))
        return ERR_BUF;
    pbuf_copy_partial(p, bytes, p->tot_len, 0);
    return gre_output(current, bytes, p->tot_len) ? ERR_IF : ERR_OK;
}
static err_t init(struct netif *n) {
    n->name[0] = 'm';
    n->name[1] = 't';
    n->mtu = 1200;
    n->output = output;
    n->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}
static void error(void *arg, err_t err) {
    (void)arg;
    atomic_store(&current->failure, err == ERR_RST ? MATCH_GRE_TCP_RESET :
                 err == ERR_ABRT ? MATCH_GRE_TCP_ABORTED :
                 err == ERR_MEM ? MATCH_GRE_MEMORY : MATCH_GRE_TCP_SETUP);
    stream = NULL;
    atomic_store(&current->ready, -1);
}
static err_t receive(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)arg;
    (void)pcb;
    (void)err;
    if (!p) {
#ifdef WIN32
        shutdown(current->bridge, SD_SEND);
#else
        shutdown(current->bridge, SHUT_WR);
#endif
        return ERR_OK;
    }
    if (p->tot_len > sizeof(incoming) - incoming_n)
        return ERR_MEM;
    pbuf_copy_partial(p, incoming + incoming_n, p->tot_len, 0);
    incoming_n += p->tot_len;
    match_gre_tcp_bytes += p->tot_len;
    pbuf_free(p);
    return ERR_OK;
}
static void attach(struct tcp_pcb *pcb) {
    stream = pcb;
    tcp_recv(pcb, receive);
    tcp_err(pcb, error);
    tcp_nagle_disable(pcb);
    stream_connected = 1;
    if (match_icmp_ready())
        atomic_store(&current->ready, 1);
}
static err_t connected(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (err == ERR_OK)
        attach(pcb);
    return err;
}
static err_t accept_stream(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)arg;
    if (stream)
        return ERR_ABRT;
    if (err == ERR_OK)
        attach(pcb);
    return err;
}
void *match_lwip_worker(void *argument) {
    current = argument;
    gre_context *g = current;
    static int initialized;
    if (!initialized) {
        lwip_init();
        initialized = 1;
    }
    stream = listener = NULL;
    stream_connected = 0;
    incoming_n = incoming_offset = 0;
    ip4_addr_t local, peer, mask, gateway;
    IP4_ADDR(&local, 10, 254, 0, g->initiator ? 1 : 2);
    IP4_ADDR(&peer, 10, 254, 0, g->initiator ? 2 : 1);
    IP4_ADDR(&mask, 255, 255, 255, 252);
    ip4_addr_set_zero(&gateway);
    netif_add(&tunnel, &local, &mask, &gateway, g, init, ip4_input);
    netif_set_default(&tunnel);
    netif_set_up(&tunnel);
    netif_set_link_up(&tunnel);
    match_icmp_init(&peer, (unsigned short)g->key);
    u32_t started = sys_now(), ping = started - 100;
    int tcp_started = 0;
    unsigned char outgoing[8192];
    unsigned outgoing_n = 0;
    while (!atomic_load(&g->stop)) {
        u32_t now = sys_now();
        if (!atomic_load(&g->ready) && now - started >
            (match_icmp_ready() ? 20000u : 12000u)) {
            atomic_store(&g->failure, match_icmp_ready() ? MATCH_GRE_TCP_TIMEOUT : MATCH_GRE_UDP_TIMEOUT);
            atomic_store(&g->ready, -1);
            break;
        }
        if (!match_icmp_ready() && now - ping >= 100) {
            match_icmp_send();
            ping = now;
        }
        /* The passive PCB must exist before a peer SYN can arrive. It does not
         * publish a usable socket until our correlated ICMP reply is received. */
        if ((!g->initiator || match_icmp_ready()) && !tcp_started) {
            tcp_started = 1;
            struct tcp_pcb *pcb = tcp_new();
            if (!pcb) {
                atomic_store(&g->failure, MATCH_GRE_MEMORY);
                atomic_store(&g->ready, -1);
                break;
            }
            if (g->initiator) {
                stream = pcb;
                tcp_err(pcb, error);
                if (tcp_connect(pcb, (ip_addr_t *)&peer, 2020, connected) != ERR_OK) {
                    atomic_store(&g->failure, MATCH_GRE_TCP_SETUP);
                    atomic_store(&g->ready, -1);
                    break;
                }
            } else {
                if (tcp_bind(pcb, (ip_addr_t *)&local, 2020) != ERR_OK) {
                    atomic_store(&g->failure, MATCH_GRE_TCP_SETUP);
                    tcp_abort(pcb);
                    atomic_store(&g->ready, -1);
                    break;
                }
                listener = tcp_listen(pcb);
                if (!listener) {
                    atomic_store(&g->failure, MATCH_GRE_MEMORY);
                    tcp_abort(pcb);
                    atomic_store(&g->ready, -1);
                    break;
                }
                tcp_accept(listener, accept_stream);
            }
        }
        if (stream_connected && match_icmp_ready())
            atomic_store(&g->ready, 1);

        fd_set readset;
        FD_ZERO(&readset);
        FD_SET(g->udp, &readset);
        struct timeval wait = {0, 10000};
        select((int)(g->udp + 1), &readset, NULL, NULL, &wait);
        {
            pthread_mutex_lock(g->io_lock);
            unsigned char frame[1280];
            struct sockaddr_in from;
            int n = gre_read(g, frame, sizeof(frame), &from);
            pthread_mutex_unlock(g->io_lock);
            uint32_t key = htonl(g->key);
            if (n >= 28 && from.sin_addr.s_addr == g->peer.sin_addr.s_addr &&
                from.sin_port == g->peer.sin_port && frame[0] == 0x20 && !frame[1] &&
                frame[2] == 8 && !frame[3] && !memcmp(frame + 4, &key, 4)) {
                struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)n - 8, PBUF_RAM);
                if (p) {
                    pbuf_take(p, frame + 8, n - 8);
                    if (tunnel.input(p, &tunnel) != ERR_OK)
                        pbuf_free(p);
                    match_gre_rx_packets++;
                }
            }
        }
        sys_check_timeouts();
        if (atomic_load(&g->ready) < 0)
            break;
        if (atomic_load(&g->ready) > 0 && stream) {
            if (incoming_n > incoming_offset) {
                int n = send(g->bridge, (const char *)incoming + incoming_offset,
                             incoming_n - incoming_offset,
                             0
#ifdef MSG_NOSIGNAL
                                 | MSG_NOSIGNAL
#endif
                );
                if (n > 0) {
                    incoming_offset += n;
                    tcp_recved(stream, (u16_t)n);
                    if (incoming_offset == incoming_n)
                        incoming_offset = incoming_n = 0;
                }
            }
            if (!outgoing_n) {
                int n = recv(g->bridge, (char *)outgoing, sizeof(outgoing), 0);
                if (n > 0)
                    outgoing_n = n;
                else if (!n)
                    break;
            }
            if (outgoing_n) {
                unsigned n = outgoing_n;
                if (n > tcp_sndbuf(stream))
                    n = tcp_sndbuf(stream);
                if (n && tcp_write(stream, outgoing, (u16_t)n, TCP_WRITE_FLAG_COPY) == ERR_OK) {
                    outgoing_n -= n;
                    memmove(outgoing, outgoing + n, outgoing_n);
                    tcp_output(stream);
                }
            }
        }
    }
    match_icmp_end();
    if (stream) {
        tcp_arg(stream, NULL);
        tcp_err(stream, NULL);
        tcp_abort(stream);
        stream = NULL;
    }
    if (listener) {
        tcp_close(listener);
        listener = NULL;
    }
    netif_remove(&tunnel);
#ifdef WIN32
    shutdown(g->bridge, SD_BOTH);
#else
    shutdown(g->bridge, SHUT_RDWR);
#endif
    return NULL;
}
