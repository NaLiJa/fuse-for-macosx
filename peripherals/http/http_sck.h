#pragma once

#include "httpc.h"

extern httpc_options_t tls_sck;
/**
 * Copy the default TLS-capable HTTP backend options into caller-owned options.
 * Initializes its callback table/default fields; customize context and callbacks
 * afterward. Does not create a socket or retain the options pointer.
 */
void sck_http_options_init(httpc_options_t *options);
/**
 * Resolve domain, open TCP, and optionally complete TLS for host-order port.
 * options supplies backend context; opts is currently unused. On success, write
 * an owned opaque handle to *socket and return HTTPC_OK; release it with
 * sck_http_close. Return HTTPC_ERROR on setup failure. Blocks for DNS/connect/
 * handshake; uses the ordinary backend timeout policy.
 */
int sck_http_open(httpc_options_t *options, void **socket, void *opts,
                  const char *domain, unsigned short port, int use_ssl);
/**
 * Open an HTTP backend handle as above with three-second TCP-connect and TLS
 * handshake budgets. DNS resolution is not bounded by those budgets. With a
 * non-NULL options->context, also sets three-second send/receive socket timeouts.
 * Return HTTPC_OK with caller-owned *socket, or HTTPC_ERROR. Close on success
 * with sck_http_close; this is not a single three-second end-to-end deadline.
 */
int sck_http_open_bounded(httpc_options_t *options, void **socket, void *opts,
                            const char *domain, unsigned short port, int use_ssl);
/**
 * Close the opaque handle returned by this backend, including its TLS context
 * and underlying descriptor, then free the handle. options is unused.
 * Return HTTPC_OK; do not reuse the handle afterward.
 */
int sck_http_close(httpc_options_t *options, void *socket);

/**
 * Wake blocked I/O by shutting down the backend handle's descriptor.
 * Does not free the handle or TLS wrapper: its owning worker must still call
 * sck_http_close. A NULL handle is ignored; coordinate lifetime with the owner.
 */
void sck_http_abort(void *socket);
