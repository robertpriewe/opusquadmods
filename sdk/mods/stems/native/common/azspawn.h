/* SPDX-License-Identifier: GPL-3.0-only */
/* azspawn.h: start a helper program from inside EP147 without fork() and without a shell.
 *
 * system() and popen() fork EP147: the kernel copies the whole process's page tables
 * (several MB) while holding its memory map lock, which every EP147 thread, the audio
 * thread included, can end up waiting on. posix_spawn on glibc is clone(CLONE_VM |
 * CLONE_VFORK) + exec: the child borrows EP147's memory until exec, so nothing is copied.
 * The command is an argv array, so there is no shell to start and nothing to quote.
 *
 *   azspawn(argv, flags)   start argv[0] (searched in PATH); returns the pid or -1
 *   azspawn_reap(pid)      the exit status once it has exited (-1 = still running)
 *   azspawn_detached(argv, flags)
 *                          start it and let a small thread wait for it, for a helper
 *                          nobody waits on (no zombie left behind); 0 or -1
 *
 * flags: AZSPAWN_QUIET sends stdout/stderr to /dev/null; AZSPAWN_NO_PRELOAD gives the
 * child "LD_PRELOAD=" so it does not load azmod/azstem itself.
 *
 * Header-only, azmod's unit includes it. Not async-signal-safe. */
#ifndef AZSPAWN_H
#define AZSPAWN_H
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "azaltstack.h" /* az_thread_create */

extern char **environ;

#define AZSPAWN_QUIET 1
#define AZSPAWN_NO_PRELOAD 2

static pid_t azspawn(char *const argv[], int flags) {
    if (!argv || !argv[0])
        return -1;
    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0)
        return -1;
    int ok = 1;
    if (flags & AZSPAWN_QUIET)
        ok = posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0) == 0 &&
             posix_spawn_file_actions_adddup2(&fa, 1, 2) == 0;
    char **envp = environ, **own = NULL;
    if (ok && (flags & AZSPAWN_NO_PRELOAD)) {
        size_t n = 0;
        while (environ && environ[n])
            n++;
        own = malloc((n + 2) * sizeof *own);
        if (own) {
            size_t k = 0;
            for (size_t i = 0; i < n; i++)
                if (strncmp(environ[i], "LD_PRELOAD=", 11) != 0)
                    own[k++] = environ[i];
            own[k++] = (char *) "LD_PRELOAD=";
            own[k] = NULL;
            envp = own;
        } else {
            ok = 0;
        }
    }
    pid_t pid = -1;
    int rc = ok ? posix_spawnp(&pid, argv[0], &fa, NULL, argv, envp) : ENOMEM;
    posix_spawn_file_actions_destroy(&fa);
    free(own);
    if (rc != 0) {
        errno = rc;
        return -1;
    }
    return pid;
}

/* -1 while the child runs; its wait status once it has exited (and been reaped). */
static int azspawn_reap(pid_t pid) {
    int st = 0;
    pid_t r = waitpid(pid, &st, WNOHANG);
    if (r == pid)
        return st;
    if (r < 0 && errno == ECHILD)
        return 0; /* already reaped (SIGCHLD ignored, or someone else waited) */
    return -1;
}

static void *azspawn_waiter(void *arg) {
    pid_t pid = (pid_t) (intptr_t) arg;
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
    }
    return NULL;
}

static int azspawn_detached(char *const argv[], int flags) {
    pid_t pid = azspawn(argv, flags);
    if (pid < 0)
        return -1;
    pthread_t th;
    pthread_attr_t at;
    if (pthread_attr_init(&at) == 0) {
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&at, 64 * 1024);
        if (az_thread_create(&th, &at, azspawn_waiter, (void *) (intptr_t) pid) != 0)
            (void) azspawn_reap(pid); /* no thread: reap now if it is already done */
        pthread_attr_destroy(&at);
    }
    return 0;
}
#endif
