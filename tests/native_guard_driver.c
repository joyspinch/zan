/* Independent native guard ABI/behaviour probe. This is a TEST caller, not
 * a runtime implementation. Linked separately to emitted ARM64 or reference C.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void zan_rt_guard_fail3(const char *, unsigned, unsigned, const char *);
int zan_rt_soft_is_hard(void);
void zan_rt_set_strict(void);
int guard_abi_probe(void);
static char file[] = "probe.zan";
static char other_file[] = "probe.zan";
static void at_exit(void) { puts("atexit"); }
static void report(unsigned line, unsigned col, const char *msg) {
    zan_rt_guard_fail3(file, line, col, msg);
}
static void *worker(void *arg) {
    uintptr_t distinct = (uintptr_t)arg;
    for (int i = 0; i < 200; ++i)
        report(distinct ? (unsigned)distinct : 17, 9, "thread");
    return NULL;
}
int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "basic";
    atexit(at_exit);
    if (!strcmp(mode, "strict")) zan_rt_set_strict();
    if (!strcmp(mode, "cached-strict")) {
        printf("hard=%d\n", zan_rt_soft_is_hard());
        zan_rt_set_strict();
    }
    if (!strcmp(mode, "cached-env")) {
        printf("hard=%d\n", zan_rt_soft_is_hard());
        setenv("ZAN_RT_HARD", "1", 1);
    }
    if (!strcmp(mode, "cached-hard")) {
        printf("hard=%d\n", zan_rt_soft_is_hard());
        setenv("ZAN_RT_HARD", "0", 1);
    }
    if (!strcmp(mode, "abi")) {
        if (guard_abi_probe()) return 5;
    } else if (!strcmp(mode, "threads") || !strcmp(mode, "threads-distinct")) {
        pthread_t threads[16];
        for (uintptr_t i = 0; i < 16; ++i)
            if (pthread_create(&threads[i], NULL, worker,
                               !strcmp(mode, "threads") ? NULL : (void *)(i + 1))) return 3;
        for (int i = 0; i < 16; ++i) pthread_join(threads[i], NULL);
    } else if (!strcmp(mode, "identity")) {
        report(1, 2, "first"); report(1, 2, "suppressed different message");
        zan_rt_guard_fail3(other_file, 1, 2, "different pointer");
        report(2, 2, "different line"); report(1, 3, "different column");
        zan_rt_guard_fail3(NULL, UINT32_MAX, 2147483648u, NULL);
        zan_rt_guard_fail3(NULL, UINT32_MAX, 2147483648u, "suppressed null site");
    } else if (!strcmp(mode, "overflow")) {
        for (unsigned i = 0; i < 258; ++i) report(i, 1, "overflow");
        for (unsigned i = 0; i < 258; ++i) report(i, 1, "overflow");
    } else if (!strcmp(mode, "long")) {
        char msg[8192], path[8192];
        memset(msg, 'm', sizeof msg - 1); msg[sizeof msg - 1] = 0;
        memset(path, 'f', sizeof path - 1); path[sizeof path - 1] = 0;
        report(8, 9, msg);
        zan_rt_guard_fail3(path, 8, 9, msg);
    } else if (!strcmp(mode, "lengths")) {
        char msg[1500];
        for (unsigned len = 1350; len < 1410; ++len) {
            memset(msg, 'x', len); msg[len] = 0;
            report(len, 9, msg);
        }
    } else if (!strcmp(mode, "logdir-cache")) {
        report(1, 1, "first directory");
        setenv("ZAN_LOG_DIR", "changed-logdir", 1);
        report(2, 1, "still first directory");
    } else if (!strcmp(mode, "chdir")) {
        report(1, 1, "first cwd");
        if (chdir("second")) return 4;
        report(2, 1, "second cwd");
    } else {
        report(17, 9, "failure\n"); report(17, 9, "repeat");
    }
    puts("continued");
    return 0;
}
