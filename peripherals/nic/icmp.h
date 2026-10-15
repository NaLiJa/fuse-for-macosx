#ifndef MATCH_ICMP_H
#define MATCH_ICMP_H
#include "lwip/ip4_addr.h"
/**
 * Initialize the singleton inner-peer echo tracker and reset readiness.
 * Copies peer and uses id to distinguish replies. End any previous tracker
 * before reinitializing. Call only from the lwIP worker.
 */
void match_icmp_init(const ip4_addr_t *peer, unsigned short id);
/**
 * Send one tagged ICMP echo probe to the configured inner peer.
 * Call only from the lwIP worker. Allocation/PCB failures silently skip this
 * probe; readiness changes only on a matching reply.
 */
void match_icmp_send(void);
/**
 * Return nonzero after receiving a valid matching tagged echo reply.
 * Immediate query on the lwIP worker, not a new probe or a TCP health check.
 */
int match_icmp_ready(void);
/**
 * Remove the raw ICMP PCB if present; safe when no tracker is initialized.
 * Call only from the lwIP worker. Does not close the outer UDP socket.
 */
void match_icmp_end(void);
#endif
