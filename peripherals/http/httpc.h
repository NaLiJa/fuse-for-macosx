/* Project: Embeddable HTTP 1.0/1.1 Client
 * Author:  Richard James Howe
 * License: The Unlicense
 * Email:   howe.r.j.89@gmail.com */
#ifndef HTTPC_H
#define HTTPC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdarg.h>

#ifndef HTTPC_API
#define HTTPC_API /* Used to apply attributes to exported functions */
#endif

#ifndef ALLOCATOR_FN
#define ALLOCATOR_FN
/** Reallocate ptr from oldsz to newsz using caller-owned arena. NULL ptr
 * allocates; newsz=0 frees and returns NULL. Return NULL on allocation failure. */
typedef void *(*allocator_fn)(void *arena, void *ptr, size_t oldsz, size_t newsz);
#endif

enum {
	HTTPC_OPT_HTTP_1_0     = 1u << 0, /* attempt HTTP 1.0 request, although still process HTTP 1.1 responses */
	HTTPC_OPT_LOGGING_ON   = 1u << 1, /* turn logging on, if compiled in */
	HTTPC_OPT_NON_BLOCKING = 1u << 2, /* turn on non-blocking mode, library will return HTTPC_YIELD instead of blocking */
	HTTPC_OPT_REUSE        = 1u << 3, /* turn on connection reuse */
};

/** Body callback with caller-supplied param and borrowed buf[length].
 * position is the body offset; content_length may be unknown/zero.
 * Upload: fill buf and return bytes supplied, 0 at end, negative on failure.
 * Download: consume/copy bytes and return HTTPC_OK or negative to abort.
 * Do not retain buf beyond the call. */
typedef int (*httpc_callback)(void *param, unsigned char *buf, size_t length, size_t position, size_t content_length);

struct httpc_options;
typedef struct httpc_options httpc_options_t;

struct httpc_options { /* Note that get/put/etcetera functions can *write* to this structure */
	allocator_fn allocator;

	/** Open domain:port (host order), optionally TLS; output an owned socket handle.
	 * Return HTTPC_OK, negative error, or HTTPC_YIELD while pending. Close releases it. */
	int (*open)(httpc_options_t *os, void **socket, void *opts, const char *domain, unsigned short port, int use_ssl);
	/** Close/release the backend socket. Return HTTPC_OK, negative error, or
	 * HTTPC_YIELD while pending; socket is no longer usable once completed. */
	int (*close)(httpc_options_t *os, void *socket);
	/** Read up to input *length bytes into buf; replace *length with bytes read.
	 * HTTPC_OK with zero bytes means EOF; negative is error; HTTPC_YIELD is pending. */
	int (*read)(httpc_options_t *os, void *socket, unsigned char *buf, size_t *length);
	/** Write up to input *length bytes from buf; replace *length with bytes written.
	 * Return HTTPC_OK, negative error, or HTTPC_YIELD; partial writes are allowed. */
	int (*write)(httpc_options_t *os, void *socket, const unsigned char *buf, size_t *length);
	/** Delay/schedule milliseconds of retry backoff; return HTTPC_OK or backend
	 * failure/yield result. The callback must follow the selected blocking mode. */
	int (*sleep)(httpc_options_t *os, unsigned long milliseconds );
	/** Write elapsed milliseconds for timeout/backoff; return HTTPC_OK or
	 * negative on failure. Use a counter consistent with sleep and retry timing. */
	int (*time)(httpc_options_t *os, unsigned long *millisecond);
	/** Log fmt/ap to file. Arguments are borrowed; return the backend logger
	 * result (negative on error). Must not change socket or request ownership. */
	int (*logger)(httpc_options_t *os, void *file, const char *fmt, va_list ap);

	/** Optional retry cancellation hook: nonzero aborts before retry, zero allows
	 * retry. os is borrowed; used to avoid replaying non-idempotent POST requests. */
	int (*should_abort)(httpc_options_t *os);

	void *arena       /* passed to allocator */,
	     *logfile,    /* passed to logger */
	     *socketopts, /* passed to open */
	     *state;      /* internal state for each operation; do not use */

	unsigned flags;   /* options for library */
	size_t receive_buffer_size; /* zero uses the default small response buffer */

	int argc;         /* custom headers count; number of custom headers */
	char **argv;      /* custom headers; appended to the HTTP request */

	void *context;    /* For your use, feel free to fill with good thoughts and positive affirmations */
	int response;     /* HTTP response code; set after the header is retrieved. */
};


enum {
	HTTPC_ERROR = -1, /* negated HTTP error codes also returned */
	HTTPC_OK    =  0, /* all operations completed successfully */
	HTTPC_YIELD =  1, /* call again later - the operation has not finished */
	HTTPC_REUSE =  2, /* operation complete, connection not closed */
};

struct httpc;
typedef struct httpc httpc_t;

/* all functions: return negative on failure, zero or positive on success */
/**
 * Run GET for NUL-terminated url using configured backend a. Deliver response
 * body chunks to fn(param); buffers are borrowed for each callback. Return
 * HTTPC_OK, HTTPC_YIELD/REUSE as enabled, or negative on failure. Keep a, url,
 * and callback state alive across yielded calls; a->response holds HTTP status.
 */
HTTPC_API int httpc_get(httpc_options_t *a, const char *url, httpc_callback fn, void *param);
/**
 * Run PUT for url, sourcing upload bytes from fn(param). The callback fills
 * its borrowed buffer and returns bytes supplied, 0 at end, negative on failure.
 * Return the standard HTTPC result; a->response holds HTTP status. Keep a, url,
 * and callback state valid across enabled NON_BLOCKING/REUSE operations.
 */
HTTPC_API int httpc_put(httpc_options_t *a, const char *url, httpc_callback fn, void *param); /* fn should return size, 0 on stop, -1 on failure */
/**
 * Run POST for url, sourcing upload bytes from fn(param). The callback fills
 * its borrowed buffer and returns bytes supplied, 0 at end, negative on failure.
 * Return the standard HTTPC result; a->response holds HTTP status. Keep a, url,
 * and callback state valid across enabled NON_BLOCKING/REUSE operations.
 */
HTTPC_API int httpc_post(httpc_options_t *a, const char *url, httpc_callback fn, void *param); /* fn should return size, 0 on stop, -1 on failure */
/**
 * Blocking GET into caller-owned buffer of initial *length capacity. On
 * HTTPC_OK set *length to bytes stored; otherwise it is zero. No implicit NUL
 * terminator. Return negative on failure/overflow; NON_BLOCKING/REUSE unsupported.
 */
HTTPC_API int httpc_get_buffer(httpc_options_t *a, const char *url, char *buffer, size_t *length); /* store GET to buffer */
/**
 * Blocking PUT of length bytes from caller-owned buffer to url.
 * Does not take ownership or NUL-terminate data; response body is discarded.
 * Return the standard HTTPC result; HTTP status is in a->response.
 * NON_BLOCKING and REUSE are unsupported by this buffer helper.
 */
HTTPC_API int httpc_put_buffer(httpc_options_t *a, const char *url, char *buffer, size_t length); /* PUT from buffer */
/**
 * Blocking POST of length bytes from caller-owned buffer to url.
 * Does not take ownership or NUL-terminate data; response body is discarded.
 * Return the standard HTTPC result; HTTP status is in a->response.
 * NON_BLOCKING and REUSE are unsupported by this buffer helper.
 */
HTTPC_API int httpc_post_buffer(httpc_options_t *a, const char *url, char *buffer, size_t length); /* POST from buffer */
/**
 * Run HEAD for url with backend a; no body callback. Return HTTPC_OK,
 * HTTPC_YIELD/REUSE when enabled, or negative on failure; HTTP status is
 * a->response. Caller retains options/url across nonblocking continuation.
 */
HTTPC_API int httpc_head(httpc_options_t *a, const char *url);
/**
 * Run DELETE for url with backend a and discard the response body.
 * Return the standard HTTPC result; HTTP status is separate in a->response.
 * Keep options/url valid across any enabled nonblocking continuation.
 */
HTTPC_API int httpc_delete(httpc_options_t *a, const char *url);
/**
 * Run TRACE for url with backend a and discard the response body.
 * Return the standard HTTPC result; inspect a->response for HTTP status.
 * Caller retains options/url across any nonblocking continuation.
 */
HTTPC_API int httpc_trace(httpc_options_t *a, const char *url);
/**
 * Run OPTIONS for url with backend a and discard the response body.
 * Return the standard HTTPC result; inspect a->response for HTTP status.
 * Caller retains options/url across any nonblocking continuation.
 */
HTTPC_API int httpc_options(httpc_options_t *a, const char *url);
/**
 * End retained HTTP operation/session state in a and release its backend
 * resources. Return HTTPC_OK after cleanup. Does not free a
 * or caller-owned callback/context buffers; use for yielded/reusable sessions.
 */
HTTPC_API int httpc_end_session(httpc_options_t *a);
/**
 * Run the library's built-in test suite with configured options a.
 * May perform blocking backend operations; return 0 on success, negative
 * on test failure. Intended for diagnostics rather than application requests.
 */
HTTPC_API int httpc_tests(httpc_options_t *a);

/* You provide these functions and populate 'httpc_options_t' with them when porting to a new platform 
 * (i.e. Not Unix or Windows) - return negative on failure, zero (HTTPC_OK) on success, and for 
 * open/close/read/write return HTTPC_YIELD if you want the client to yield to its' caller. */
/**
 * Platform hook: open domain:port (host-order port), optionally TLS when
 * use_ssl!=0, using socketopts. On completion write an owned handle to *socket;
 * return HTTPC_OK, negative failure, or HTTPC_YIELD for asynchronous progress.
 * The backend close hook must release a successfully opened handle.
 */
HTTPC_API extern int httpc_open(httpc_options_t *a, void **socket, void *socketopts, const char *domain, unsigned short port, int use_ssl);
/**
 * Platform hook: close/release an opaque backend socket handle.
 * Return HTTPC_OK, negative failure, or HTTPC_YIELD for asynchronous progress.
 * Ownership ends once close completes; options a remains caller-owned.
 */
HTTPC_API extern int httpc_close(httpc_options_t *a, void *socket);
/**
 * Platform hook: read up to input *length bytes into buf and replace *length
 * with bytes read (0 for EOF). Return HTTPC_OK, negative failure, or HTTPC_YIELD
 * while pending. buf and the length pointer are borrowed for the call.
 */
HTTPC_API extern int httpc_read(httpc_options_t *a, void *socket, unsigned char *buf, size_t *length);
/**
 * Platform hook: write up to input *length bytes from buf, replacing *length
 * with bytes written, including partial writes. Return HTTPC_OK, negative failure,
 * or HTTPC_YIELD while pending. Does not retain the caller's buffer.
 */
HTTPC_API extern int httpc_write(httpc_options_t *a, void *socket, const unsigned char *buf, size_t *length);
/**
 * Platform hook: delay/retry scheduling for milliseconds using backend a.
 * Return HTTPC_OK when done, negative on failure, or the backend yield result
 * when configured for nonblocking operation.
 */
HTTPC_API extern int httpc_sleep(httpc_options_t *a, unsigned long milliseconds);
/**
 * Platform hook: write the backend's elapsed millisecond counter to milliseconds.
 * Return HTTPC_OK on success or negative on failure; used for timeout/backoff.
 */
HTTPC_API extern int httpc_time(httpc_options_t *a, unsigned long *milliseconds);
/**
 * Platform hook: write a formatted diagnostic to logfile using fmt/ap.
 * Arguments are borrowed; return the backend logging result, negative on failure.
 * Logging must not change HTTP request or socket ownership.
 */
HTTPC_API extern int httpc_logger(httpc_options_t *a, void *logfile, const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif
#endif
