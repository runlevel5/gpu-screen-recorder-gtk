#include "recorder_process.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#endif

static pid_t g_pid = -1;

void recorder_process_init(void)
{
    g_pid = -1;
}

bool recorder_process_is_running(void)
{
    return g_pid > 0;
}

pid_t recorder_process_get_pid(void)
{
    return g_pid;
}

bool recorder_process_spawn(const char *const *argv)
{
    if(g_pid > 0) {
        fprintf(stderr, "recorder_process_spawn: a child is already running (pid %d)\n", (int)g_pid);
        return false;
    }
    if(!argv || !argv[0])
        return false;

    pid_t parent = getpid();
    pid_t pid = fork();
    if(pid == -1) {
        perror("recorder_process_spawn: fork");
        return false;
    }
    if(pid == 0) {
        /* Child. */
#ifdef __linux__
        if(prctl(PR_SET_PDEATHSIG, SIGTERM) == -1) {
            perror("recorder_process_spawn: prctl(PR_SET_PDEATHSIG)");
            _exit(3);
        }
        /* Guard against a race: parent died before prctl took effect. */
        if(getppid() != parent)
            _exit(3);
#else
        (void)parent;
#endif
        execvp(argv[0], (char *const *)argv);
        perror("recorder_process_spawn: execvp");
        _exit(127);
    }

    g_pid = pid;
    return true;
}

bool recorder_process_send_signal(int sig)
{
    if(g_pid <= 0)
        return false;
    if(kill(g_pid, sig) == -1) {
        perror("recorder_process_send_signal: kill");
        return false;
    }
    return true;
}

bool recorder_process_poll(int *out_status)
{
    if(g_pid <= 0)
        return false;
    int status = 0;
    pid_t r = waitpid(g_pid, &status, WNOHANG);
    if(r == 0)
        return false;          /* still running */
    if(r == -1) {
        if(errno == ECHILD) {
            /* Already reaped by something else. */
            g_pid = -1;
            return true;
        }
        perror("recorder_process_poll: waitpid");
        return false;
    }
    g_pid = -1;
    if(out_status)
        *out_status = status;
    return true;
}

void recorder_process_terminate(void)
{
    if(g_pid <= 0)
        return;
    if(kill(g_pid, SIGINT) == -1 && errno != ESRCH)
        perror("recorder_process_terminate: kill(SIGINT)");

    /* Blocking wait. The caller has already requested shutdown so this is fine. */
    int status = 0;
    pid_t r;
    do {
        r = waitpid(g_pid, &status, 0);
    } while(r == -1 && errno == EINTR);
    g_pid = -1;
}
