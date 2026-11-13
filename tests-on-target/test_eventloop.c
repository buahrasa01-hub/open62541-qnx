/* test_eventloop.c — on-target test: POSIX eventloop (select() fallback).
 *
 * On QNX there is no epoll, so the eventloop takes the select() fallback
 * (UA_HAVE_EPOLL is defined only on Linux). This test proves the fallback
 * actually fires timers on QNX 8.0: two repeating timers (100 ms and
 * 250 ms) over ~2.1 s of wall time. Expectations are deliberately lenient
 * (QEMU runs under TCG with no KVM, so wake-ups can lag).
 *
 * Mirrors the shape of upstream check_eventloop.c without the libcheck
 * framework. Prints PASS/FAIL; exit 0 only if all pass.
 *
 * Build (QNX target): see scripts/build-target.sh
 */
#include <open62541/plugin/eventloop.h>
#include <stdio.h>
#include <time.h>

static size_t count100 = 0;
static size_t count250 = 0;

static void
cb100(void *application, void *data) {
    (void)application;
    (void)data;
    count100++;
}

static void
cb250(void *application, void *data) {
    (void)application;
    (void)data;
    count250++;
}

static double
elapsed(const struct timespec *start, const struct timespec *now) {
    return (double)(now->tv_sec - start->tv_sec) +
           (double)(now->tv_nsec - start->tv_nsec) / 1e9;
}

int
main(void) {
    int failures = 0;
    UA_EventLoop *el = UA_EventLoop_new_POSIX(NULL);
    if(!el) {
        printf("FAIL: UA_EventLoop_new_POSIX returned NULL\n");
        return 1;
    }
    printf("PASS: eventloop created (POSIX/select fallback)\n");

    UA_StatusCode res =
        el->addTimer(el, cb100, NULL, NULL, 100.0, NULL,
                     UA_TIMERPOLICY_CURRENTTIME, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: addTimer 100ms (0x%08x)\n", (unsigned)res);
        return 1;
    }
    res = el->addTimer(el, cb250, NULL, NULL, 250.0, NULL,
                       UA_TIMERPOLICY_CURRENTTIME, NULL);
    if(res != UA_STATUSCODE_GOOD) {
        printf("FAIL: addTimer 250ms (0x%08x)\n", (unsigned)res);
        return 1;
    }
    printf("PASS: two repeating timers registered (100ms, 250ms)\n");

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
        UA_DateTime next = el->run(el, 10); /* 10 ms slices */
        if(next == 0) {
            printf("FAIL: eventloop run returned 0 datetime\n");
            failures++;
            break;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
    } while(elapsed(&start, &now) < 2.1);

    printf("INFO: 100ms timer fired %zu times, 250ms timer %zu times\n",
           count100, count250);
    /* Nominal: 21 and 8. Lenient bounds for TCG slowness. */
    if(count100 < 10 || count100 > 30) {
        printf("FAIL: 100ms timer count %zu outside [10,30]\n", count100);
        failures++;
    } else {
        printf("PASS: 100ms timer count %zu in [10,30]\n", count100);
    }
    if(count250 < 4 || count250 > 14) {
        printf("FAIL: 250ms timer count %zu outside [4,14]\n", count250);
        failures++;
    } else {
        printf("PASS: 250ms timer count %zu in [4,14]\n", count250);
    }
    if(count100 <= count250) {
        printf("FAIL: 100ms timer did not fire more often than 250ms\n");
        failures++;
    } else {
        printf("PASS: 100ms timer fired more often than 250ms\n");
    }

    el->stop(el);
    el->free(el);
    printf("eventloop: %s\n", failures == 0 ? "ALL PASS" : "FAILURES");
    return failures == 0 ? 0 : 1;
}
