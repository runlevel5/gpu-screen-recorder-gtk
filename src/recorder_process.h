#ifndef GSR_RECORDER_PROCESS_H
#define GSR_RECORDER_PROCESS_H

/*
 * Manages the gpu-screen-recorder subprocess. Toolkit-agnostic.
 *
 * Lifecycle:
 *   recorder_process_init()
 *   recorder_process_spawn(argv)         -> starts the child
 *   recorder_process_send_signal(SIGUSR1) -> request save
 *   recorder_process_send_signal(SIGUSR2) -> pause/unpause
 *   recorder_process_poll(&exit_status)  -> non-blocking; returns true if the child exited
 *   recorder_process_terminate()         -> SIGINT, wait
 */

#include <stdbool.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void  recorder_process_init(void);
bool  recorder_process_is_running(void);
pid_t recorder_process_get_pid(void);

/* argv is a NULL-terminated array of C strings. Returns true on success.
 * The child sets PR_SET_PDEATHSIG=SIGTERM so it dies with us. */
bool recorder_process_spawn(const char *const *argv);

/* Sends `sig` to the child. Returns true if the child existed and the signal
 * was delivered. */
bool recorder_process_send_signal(int sig);

/* Non-blocking waitpid. Returns true if the child has exited; on true, *out_status
 * (if non-NULL) is set to the waitpid status. Internal pid is cleared on exit. */
bool recorder_process_poll(int *out_status);

/* Sends SIGINT and waits (blocking) for the child to exit. */
void recorder_process_terminate(void);

#ifdef __cplusplus
}
#endif

#endif /* GSR_RECORDER_PROCESS_H */
