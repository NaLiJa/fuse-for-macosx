#include "stun.h"
#include <string.h>
static uint16_t u16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t u32(const uint8_t *p) { return ((uint32_t)u16(p) << 16) | u16(p + 2); }
void stun_binding_request(uint8_t packet[20], const uint8_t transaction[12]) {
    memset(packet, 0, 20);
    packet[1] = 1;
    packet[4] = 0x21;
    packet[5] = 0x12;
    packet[6] = 0xa4;
    packet[7] = 0x42;
    memcpy(packet + 8, transaction, 12);
}
int stun_binding_response(const uint8_t *p, size_t n, const uint8_t transaction[12], uint32_t *ip,
                          uint16_t *port) {
    if (n < 20 || u16(p) != 0x101 || u32(p + 4) != 0x2112a442 || memcmp(p + 8, transaction, 12) ||
        (u16(p + 2) & 3) || u16(p + 2) != n - 20)
        return -1;
    for (size_t i = 20; i + 4 <= n;) {
        uint16_t type = u16(p + i), length = u16(p + i + 2);
        if (length > n - i - 4)
            return -1;
        if (type == 0x20 && length == 8 && p[i + 5] == 1) {
            *port = u16(p + i + 6) ^ 0x2112;
            *ip = u32(p + i + 8) ^ 0x2112a442;
            return *port ? 0 : -1;
        }
        i += 4 + ((length + 3) & ~3u);
    }
    return -1;
}

int stun_discover(const stun_io *io, uint32_t *ipv4, uint16_t *port) {
    uint8_t transaction[12], request[20], response[1500];
    io->random(io->context, transaction, sizeof(transaction));
    stun_binding_request(request, transaction);
    for (unsigned attempt = 0; attempt < 3; attempt++) {
        if (io->send(io->context, request, sizeof(request)))
            return -1;
        uint64_t deadline = io->now(io->context) + (500u << attempt);
        while (io->now(io->context) < deadline) {
            int n = io->receive(io->context, response, sizeof(response), 50);
            if (n > 0 && !stun_binding_response(response, (size_t)n, transaction, ipv4, port))
                return 0;
        }
    }
    return -1;
}
