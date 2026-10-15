/* tls.h: TLS/SSL socket support using mbedTLS
   
   Provides TLS/SSL support for W5100 socket emulation.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#ifndef FUSE_TLS_H
#define FUSE_TLS_H

#include "compat.h"

#include <mbedtls/ssl.h>
#include <mbedtls/error.h>

typedef struct tls_socket_t {
  mbedtls_ssl_context ssl;           /* SSL context */
  mbedtls_ssl_config conf;            /* SSL configuration */
  compat_socket_t fd;                 /* Underlying POSIX socket */
  int handshake_complete;             /* Flag for handshake status */
  int has_pending_data;               /* Flag indicating more data available after read */
} tls_socket_t;

/* Allocate and initialize a TLS socket from a POSIX socket */
/**
 * Allocate/configure mbedTLS around an already connected fd. hostname may be
 * NULL or a borrowed NUL-terminated SNI name/address. Return a wrapper or NULL
 * on failure; does not handshake. fd remains caller-owned even on success.
 * Free the wrapper with tls_socket_free and close fd separately. Certificate
 * verification is currently disabled pending system/Mozilla trust integration.
 */
tls_socket_t* tls_socket_alloc( compat_socket_t fd, const char *hostname );

/**
 * Free mbedTLS contexts and wrapper memory; NULL is allowed.
 * Does not close the underlying fd or perform a TLS shutdown. Call tls_close
 * first if a close-notify is wanted; never access the wrapper after this call.
 */
void tls_socket_free( tls_socket_t *tls );

/**
 * Complete a handshake after TCP connect, waiting up to timeout_seconds for
 * TLS progress (wall-clock seconds). Return 0 when complete/already complete,
 * or a negative value (-1 or mbedTLS error) on failure. Restores blocking
 * mode after the handshake attempt. The caller retains wrapper/fd ownership and must clean up on failure.
 */
int tls_connect( tls_socket_t *tls, unsigned timeout_seconds );

/**
 * Read at most len bytes into caller-owned buf after handshake completion.
 * Return positive bytes read, 0 for would-block or TLS close-notify, or -1 on
 * error/unready wrapper. Updates has_pending_data from buffered TLS plaintext.
 * Socket blocking mode/timeouts determine whether this call waits.
 */
ssize_t tls_read( tls_socket_t *tls, void *buf, size_t len );

/**
 * Write up to len bytes from caller-owned buf after a completed handshake.
 * Return bytes written (possibly partial), 0 for would-block, or -1 on error.
 * Caller must retry an unsent suffix; blocking behavior follows the fd settings.
 */
ssize_t tls_write( tls_socket_t *tls, const void *buf, size_t len );

/**
 * Send best-effort TLS close-notify if the handshake completed; NULL is allowed.
 * Does not close fd or free the wrapper. Caller must subsequently free the
 * wrapper and close its descriptor; socket settings determine blocking behavior.
 */
void tls_close( tls_socket_t *tls );

#endif                          /* #ifndef FUSE_TLS_H */

