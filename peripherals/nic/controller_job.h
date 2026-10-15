#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Two mailbox workers and one background housekeeping worker. Only the
 * emulation thread submits and collects jobs. */
#define CONTROLLER_JOB_BACKGROUND 2u
#define CONTROLLER_JOB_COUNT 3u
/**
 * Submit execute(request) on channel in [0, CONTROLLER_JOB_COUNT).
 * Call from the emulation thread. The request pointer is borrowed, not copied;
 * keep it valid until completion is collected or workers are joined. Return 0
 * if submitted, -1 for an invalid channel, occupied/uncollected job, or thread
 * creation failure. Creates the persistent worker lazily; does not run inline.
 */
int controller_job_start(unsigned channel, void (*execute)(void *), void *request);
/**
 * Collect channel's completion flag and clear it, returning true exactly once
 * per finished job. Use a valid channel and call from the emulation thread.
 * Does not wait for running work or dispose of caller-owned request storage.
 */
bool controller_job_poll(unsigned channel);
/**
 * Return whether a valid channel currently executes a job.
 * Immediate synchronized query from the emulation thread; false can also mean
 * completed but not yet collected. Does not consume the completion flag.
 */
bool controller_job_is_running(unsigned channel);
/**
 * Request cancellation of active HTTP downloads and wait for all running jobs.
 * Blocks the caller; other job types finish through their own timeout/exit paths.
 * Call from the emulation thread before resetting resources workers may use.
 * Persistent workers and uncollected completion flags are retained.
 */
void controller_job_cancel_and_wait(void);
/**
 * Wait for active jobs, then stop and join all persistent workers.
 * Call from the emulation thread before emulator shutdown. Resets job slots;
 * caller-owned request buffers are not freed. May block on worker completion.
 */
void controller_job_shutdown(void);
