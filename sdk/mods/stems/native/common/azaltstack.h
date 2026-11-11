/* SPDX-License-Identifier: GPL-3.0-only */
/* azaltstack.h: an alternate signal stack for every thread azmod or azstem starts.
 *
 * The crash reporter's handlers are installed with SA_ONSTACK, so that a thread that faulted
 * by running off the end of its own stack still has a stack to run the handler on. SA_ONSTACK
 * only takes effect on a thread that has called sigaltstack, and the setting is per thread:
 * the kernel otherwise pushes the signal frame onto the overflowed stack, faults again, and
 * kills the process with no record. So:
 *   - az_thread_create() starts every one of our threads with its own alternate stack, freed
 *     when the thread exits (a pthread key destructor);
 *   - azalt_this_thread(0) gives the calling thread one for good (the constructors call it
 *     on EP147's main thread, which is also JUCE's message thread).
 * EP147's own threads are not ours to change: on those SA_ONSTACK is a no-op, and a stack
 * overflow ends the process at once, as it would without azmod.
 *
 * Each stack is AZALT_SIZE bytes with a PROT_NONE guard page below it, so a handler that
 * overruns it faults instead of writing into whatever lies beneath. A thread that already
 * has an alternate stack keeps it.
 *
 * Header-only: azmod and azstem each include it. */
#ifndef AZALTSTACK_H
#define AZALTSTACK_H
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define AZALT_SIZE \
    (64 * 1024) /* the reporter's frame is ~6 KB; EP147's chained handler runs here too */

static pthread_key_t azalt_key;
static pthread_once_t azalt_once = PTHREAD_ONCE_INIT;
static int azalt_key_ok;

/* thread exit: switch the alternate stack off, then unmap it with its guard page */
static void azalt_release(void *sp) {
    stack_t off;
    off.ss_sp = NULL;
    off.ss_size = 0;
    off.ss_flags = SS_DISABLE;
    sigaltstack(&off, NULL);
    long pg = sysconf(_SC_PAGESIZE);
    munmap((char *) sp - pg, (size_t) (AZALT_SIZE + pg));
}
static void azalt_key_init(void) {
    azalt_key_ok = pthread_key_create(&azalt_key, azalt_release) == 0;
}

/* Give the calling thread an alternate signal stack unless it already has one.
 * free_at_exit: 1 for a thread that will exit (the stack is released with it), 0 for one that
 * lives as long as the process. 0 = the thread has an alternate stack now, -1 = it does not. */
static int azalt_this_thread(int free_at_exit) {
    stack_t cur;
    if (sigaltstack(NULL, &cur) == 0 && !(cur.ss_flags & SS_DISABLE))
        return 0;
    if (free_at_exit) {
        pthread_once(&azalt_once, azalt_key_init);
        if (!azalt_key_ok)
            return -1; /* could never be freed: a leak per thread start */
    }
    long pg = sysconf(_SC_PAGESIZE);
    char *m = (char *) mmap(NULL, (size_t) (AZALT_SIZE + pg), PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (m == MAP_FAILED)
        return -1;
    if (mprotect(m, (size_t) pg, PROT_NONE) != 0) { /* the guard page */
        munmap(m, (size_t) (AZALT_SIZE + pg));
        return -1;
    }
    stack_t ss;
    ss.ss_sp = m + pg;
    ss.ss_size = AZALT_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        munmap(m, (size_t) (AZALT_SIZE + pg));
        return -1;
    }
    if (free_at_exit)
        pthread_setspecific(azalt_key, m + pg);
    return 0;
}

struct azalt_start {
    void *(*fn)(void *);
    void *arg;
};
static void *azalt_entry(void *p) {
    struct azalt_start s = *(struct azalt_start *) p;
    free(p);
    azalt_this_thread(1); /* no alternate stack is not a reason not to run the thread */
    return s.fn(s.arg);
}
/* pthread_create, and the new thread gets an alternate signal stack before fn runs. Every
 * thread in azmod and azstem starts here (tools/unit-tests/run.sh lints for it). */
static int az_thread_create(pthread_t *t, const pthread_attr_t *attr, void *(*fn)(void *),
                            void *arg) {
    struct azalt_start *s = (struct azalt_start *) malloc(sizeof *s);
    if (!s)
        return ENOMEM;
    s->fn = fn;
    s->arg = arg;
    int rc = pthread_create(t, attr, azalt_entry, s); /* altstack-ok: the one raw call */
    if (rc)
        free(s);
    return rc;
}
#endif
