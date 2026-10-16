#include "config.h"
/* Setup-only echo helper; lwIP's ICMP implementation replies to peer requests. */
#include "gre.h"
#include "icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/raw.h"
#include <string.h>
static struct raw_pcb *echo;
static ip4_addr_t remote;
static unsigned short identifier;
static int replied;
static u8_t receive(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *source) {
    (void)arg;
    (void)pcb;
    unsigned char bytes[80];
    unsigned n = pbuf_copy_partial(p, bytes, sizeof(bytes), 0);
    unsigned header = n ? ((bytes[0] & 15) * 4) : 0;
    if (!ip_addr_cmp(source, (const ip_addr_t *)&remote) || header < 20 || n != header + 12 ||
        inet_chksum(bytes + header, 12))
        return 0;
    if (bytes[header] == 0 && bytes[header + 1] == 0 && bytes[header + 4] == (identifier >> 8) &&
        bytes[header + 5] == (identifier & 255) && !memcmp(bytes + header + 8, "SPXM", 4)) {
        replied = 1;
        match_gre_icmp_replies++;
        pbuf_free(p);
        return 1;
    }
    return 0;
}
void match_icmp_init(const ip4_addr_t *peer, unsigned short id) {
    remote = *peer;
    identifier = id;
    replied = 0;
    echo = raw_new(IP_PROTO_ICMP);
    if (echo)
        raw_recv(echo, receive, NULL);
}
void match_icmp_send(void) {
    if (!echo)
        return;
    struct pbuf *p = pbuf_alloc(PBUF_IP, 12, PBUF_RAM);
    if (!p)
        return;
    unsigned char *b = p->payload;
    memset(b, 0, 12);
    b[0] = 8;
    b[4] = identifier >> 8;
    b[5] = identifier & 255;
    memcpy(b + 8, "SPXM", 4);
    unsigned short check = inet_chksum(b, 12);
    memcpy(b + 2, &check, 2);
    raw_sendto(echo, p, (const ip_addr_t *)&remote);
    pbuf_free(p);
}
int match_icmp_ready(void) { return replied; }
void match_icmp_end(void) {
    if (echo)
        raw_remove(echo);
    echo = NULL;
}
