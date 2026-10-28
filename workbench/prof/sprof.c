// sprof.so (LD_PRELOAD): samples the program counter 1000 times a CPU second (SIGPROF), writes SPROF_OUT at exit:
// "pc count" lines plus the maps of executable mappings
#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>
#define N (1 << 22)
static unsigned long pcs[N]; static volatile unsigned long n;
static void on_prof(int sig, siginfo_t *si, void *uc) {
    (void)sig; (void)si;
    unsigned long i = __atomic_fetch_add(&n, 1, __ATOMIC_RELAXED);
    if (i < N) pcs[i] = ((ucontext_t *)uc)->uc_mcontext.gregs[REG_RIP];
}
static int cmp(const void *a, const void *b) { unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b; return x < y ? -1 : x > y; }
static void dump(void) {
    struct itimerval z = {0}; setitimer(ITIMER_PROF, &z, 0);
    const char *o = getenv("SPROF_OUT"); if (!o) return;
    if (n < 100) return; char fn[600]; snprintf(fn, sizeof fn, "%s.%d", o, (int)getpid()); FILE *f = fopen(fn, "w"); if (!f) return;
    unsigned long m = n < N ? n : N; qsort(pcs, m, sizeof pcs[0], cmp);
    for (unsigned long i = 0; i < m;) { unsigned long j = i; while (j < m && pcs[j] == pcs[i]) j++; fprintf(f, "%lx %lu\n", pcs[i], j - i); i = j; }
    FILE *mp = fopen("/proc/self/maps", "r"); char line[512];
    while (mp && fgets(line, sizeof line, mp)) if (strstr(line, " r-xp ")) fprintf(f, "MAP %s", line);
    if (mp) fclose(mp); fclose(f);
}
static void on_term(int s) { dump(); signal(s, SIG_DFL); raise(s); }
__attribute__((constructor)) static void init(void) {
    if (!getenv("SPROF_OUT")) return;
    struct sigaction sa = {0}; sa.sa_sigaction = on_prof; sa.sa_flags = SA_SIGINFO | SA_RESTART; sigaction(SIGPROF, &sa, 0);
    struct itimerval t = {{0, 1000}, {0, 1000}}; setitimer(ITIMER_PROF, &t, 0);
    atexit(dump); signal(SIGTERM, on_term); signal(SIGINT, on_term);
}
