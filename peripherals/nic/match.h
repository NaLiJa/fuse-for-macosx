#ifndef SPX_MATCH_H
#define SPX_MATCH_H
#include "match_layout.h"
#include "spectranext_strings.h"
/**
 * Recognize a NUL-terminated service@match.spx hostname and remember its service.
 * Return 1 and write the numeric SPX_MATCH_IP marker to ip on success, 0 for a
 * non-match hostname, or -1 for an invalid name, conflicting active match, or
 * allocation failure. Output is valid only on success. Serializes internally;
 * one pending service and one active match are supported.
 */
int match_resolve(const char *host, uint32_t *ip);
/**
 * Execute a SPECTRANEXT_CMD_MATCHMAKING_* operation and fill result with a snapshot.
 * request/result must be non-NULL and must not overlap. request selects the socket,
 * action, peer index/revision, and optional text according to command.
 * Errors are returned in result->stage/error, not as a function return value.
 * May block for STUN, HTTP, or persistence; run on the controller worker, never
 * the bus/emulation thread. Takes the matchmaking lock internally.
 */
void match_command(unsigned command, const match_request *request, match_snapshot *result);
/**
 * Report a connected match to the API once, as best-effort housekeeping.
 * Call on the controller worker AFTER publishing the successful snapshot/status
 * to the application. May block on STUN and HTTP; failure does not close the
 * established peer transport. Serializes internally; no-op without pending work.
 */
void match_complete(void);
/** Copy a selected local/match string into out[capacity], including its NUL.
 * Return 0 on success, -1 on unavailable/unknown/insufficient capacity; never
 * truncate. If capacity is nonzero, failure empties out. Takes the match lock;
 * out must be non-NULL when capacity is nonzero. VERSION is served by Z80 ROM.
 * Peer values are exposed only while the claimed transport remains connected.
 */
int match_get_string(uint8_t string_id, char *out, uint8_t capacity);
/**
 * Close/release the claimed transport and clear pending service and menu state.
 * Takes the matchmaking lock internally and may wait for platform cleanup.
 * Does not perform an HTTP leave request; remote records expire normally.
 */
void match_reset(void);
#endif
