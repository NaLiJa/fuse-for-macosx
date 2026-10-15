#ifndef MATCH_LWIPOPTS_H
#define MATCH_LWIPOPTS_H
#define NO_SYS 1
#define ESP_LWIP_DHCP_FINE_TIMERS_ONDEMAND 0
#define ESP_LWIP_IGMP_TIMERS_ONDEMAND 0
#define ESP_LWIP_DNS_TIMERS_ONDEMAND 0
#define ESP_LWIP_MLD6_TIMERS_ONDEMAND 0
#define DHCP_DOES_ARP_CHECK 0
#define LWIP_ACD 0
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_TCP 1
#define LWIP_UDP 0
#define LWIP_RAW 1
#define LWIP_ICMP 1
#define LWIP_ARP 0
#define LWIP_DHCP 0
#define LWIP_DNS 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define LWIP_ETHERNET 0
#define LWIP_TIMERS 1
#define MEM_ALIGNMENT 8
#define MEM_SIZE 65536
#define TCP_MSS 1100
#define TCP_WND 8192
#define TCP_SND_BUF 8192
#define TCP_SND_QUEUELEN 32
#define MEMP_NUM_TCP_SEG 64
#define PBUF_POOL_SIZE 32
#define LWIP_STATS 0
#define LWIP_NETIF_HOSTNAME 0
#define LWIP_NETIF_STATUS_CALLBACK 0
#define IP_REASSEMBLY 0
#define IP_FRAG 0
#define LWIP_RAND() match_lwip_rand()
/** Supply lwIP's LWIP_RAND hook from the host C PRNG.
 * Returns an unsigned pseudo-random value for stack housekeeping; not suitable
 * for authentication keys. Called by the emulator's singleton lwIP worker. */
unsigned int match_lwip_rand(void);
#endif
