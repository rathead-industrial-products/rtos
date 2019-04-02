
#include <stdio.h>
#include <stdlib.h>
#include "eex_os.h"
#include "eex_kernel.h"

struct tls_thread_t {
    char print_buf[32];
} tls_thread_1, tls_thread_2;


static void thread(void * const tls) {
    static int tid, r;

    eexThreadEntry();
    for (;;) {
        r = rand() % 1000 + 1;

        tid = eexThreadID();
        (void) printf("%d: task %d, delay until %d ms\n", eexKernelTime(NULL), tid, r + eexKernelTime(NULL));
        fflush(stdout);

        eexDelay(r);
    }
}

static void thread_sec(void * const tls) {
    static uint32_t next_sec = 1;

    eexThreadEntry();
    for (;;) {
        eexDelayUntil(1000 * next_sec++);
        (void) printf("%d:\n", eexKernelTime(NULL));
    }
}

int main(void){
    eex_status_t err;

    printf("Starting Main\n");
    fflush(stdout);

    srand(314159);

    err = eexThreadCreate(thread,     &tls_thread_1, 1, "task 1");
    err = eexThreadCreate(thread,     &tls_thread_2, 2, "task 2");
    err = eexThreadCreate(thread_sec, NULL,          3, "task second");

    printf("Starting Scheduler\n");
    fflush(stdout);

    eexKernelStart();

    return (err);
}

