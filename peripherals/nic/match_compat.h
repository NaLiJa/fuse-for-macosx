#ifndef SPX_MATCH_COMPAT_H
#define SPX_MATCH_COMPAT_H
#include <stddef.h>
#include <stdint.h>
/**
 * Return monotonic elapsed milliseconds for refresh and setup deadlines.
 * The epoch is platform-specific; only differences within one run are meaningful.
 */
uint64_t match_compat_time(void);
/**
 * Fill the caller-owned data buffer with length random bytes.
 * Used for invocation identifiers and joining codes; no buffer ownership transfers.
 */
void match_compat_random(void *data, size_t length);
/**
 * Return a borrowed NUL-terminated canonical device identifier.
 * Do not free or modify it. Hardware uses its serial; FuseX uses its instance ID.
 */
const char *match_compat_device(void);
/**
 * Load the saved display name into a caller-owned 24-byte buffer.
 * Always NUL-terminates; missing/unreadable settings produce an empty name,
 * which the core displays as the device serial. May block on storage.
 */
void match_compat_load_name(char name[24]);
/**
 * Persist a NUL-terminated display name of at most 23 bytes.
 * An empty string selects the serial fallback. May block on storage.
 * Return 0 on success or a negative value on failure; caller retains name.
 */
int match_compat_save_name(const char *name);
/**
 * Claim a W5100 socket index for the sole active match.
 * On success, store its generation in generation and its application transport
 * in udp (nonzero for UDP), preparing a persistent outer UDP socket if needed.
 * Return 0 on success or -1 if unavailable/incompatible. May block on setup.
 * Caller holds the matchmaking lock and must eventually release the claim.
 */
int match_compat_claim(unsigned socket, uint32_t *generation, int *udp);
/**
 * Return nonzero if socket still belongs to the active claim and generation.
 * A closed/reused socket is invalid. Caller holds the matchmaking lock.
 */
int match_compat_valid(unsigned socket, uint32_t generation);
/**
 * Perform blocking STUN discovery on the claimed persistent UDP socket.
 * Return 0 and write host-order numeric ip/port on success, negative on failure.
 * Outputs are valid only on success. Caller holds the matchmaking lock.
 */
int match_compat_stun(uint32_t *ip, uint16_t *port);
/**
 * Send a blocking API POST containing NUL-terminated JSON body.
 * instance and sequence supply the invocation and idempotency headers.
 * response is caller-owned storage of capacity bytes including its NUL terminator.
 * Return the HTTP status (including non-200 statuses), or negative on transport
 * failure. The caller owns retry policy; this adapter does not retry the POST.
 * Caller holds the matchmaking lock and keeps all input buffers valid until return.
 */
int match_compat_post(const char *body, const char *instance, unsigned sequence, char *response,
                      size_t capacity);
/**
 * Begin direct setup for a reserved connection UUID and peer device identifier.
 * ip/port are the peer's outer UDP endpoint in host order; udp selects application
 * datagrams instead of GRE/TCP. Reuse the claimed socket and its NAT mapping.
 * Return 0 if setup started or negative on immediate failure; poll ready afterward.
 * Caller holds the matchmaking lock; arguments are borrowed for this call.
 */
int match_compat_start(const char *connection, const char *peer_device, uint32_t ip, uint16_t port,
                       int udp);
/**
 * Poll the claimed transport: positive means usable, 0 still connecting,
 * negative failed. Consult match_compat_error on failure. Caller holds the lock.
 * On readiness, adapts backend completion to the application socket state.
 */
int match_compat_ready(void);
/**
 * Return a borrowed NUL-terminated platform diagnostic, possibly empty.
 * Copy it before another platform operation changes it; never free it.
 * Caller holds the matchmaking lock.
 */
const char *match_compat_error(void);
/**
 * Close the claimed transport and relinquish its socket claim.
 * Safe with no active claim; may wait for worker cleanup. Does not send API leave.
 * Caller holds the matchmaking lock.
 */
void match_compat_release(void);
/**
 * Acquire the platform matchmaking mutex, blocking until available.
 * On hardware also initializes adapter resources on first use. Pair with unlock;
 * the mutex is not recursive.
 */
void match_compat_lock(void);
/**
 * Release the matchmaking mutex held by this task/thread.
 * Call exactly once per successful lock; does not release the active socket.
 */
void match_compat_unlock(void);
#endif
