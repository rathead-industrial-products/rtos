
#include  <stddef.h>
#include  <sam.h>
#include  "eex_os.h"
#include  "logging.h"
#include  "samd_clk.h"

typedef struct tls_thread_t {
    char print_buf[32];
} tls_thread_t;
static tls_thread_t tls_thread_1;
static tls_thread_t tls_thread_2;


static void thread(void * const argument) {
    static uint32_t ms, us, tid;
    static uint32_t next_intvl = 1;

    eexThreadEntry();
    for (;;) {
        eexDelayUntil((500 * next_intvl));
        tid = eexThreadID();
        ms = eexKernelTime(&us);
        log_printf("%6d.%03d: task %d, delay until %d ms", ms, us, tid, (500 * next_intvl++));



    }
}

static void thread_idle(void * const argument) {
    static uint32_t ms, us, et_start, et_end;

    eexThreadEntry();

    for (;;) {
        ms = eexKernelTime(&us);
        et_start = ms * 1000 + us;

        log_printf("%6d.%03d:", ms, us);    // ~500 us
        //log_printf("%d\n", ms);                        // ~ 250 us

        ms = eexKernelTime(&us);
        et_end = ms * 1000 + us;

        log_printf("et: %d us\n", et_end - et_start);

        eexDelay(100);
    }
}

static void thread_sec(void * const argument) {
    static uint32_t ms, us;
    static uint32_t next_sec = 1;

    eexThreadEntry();
    for (;;) {
        eexDelayUntil(1000 * next_sec++);
        ms = eexKernelTime(&us);
        log_printf("thread_sec %6d.%03d:", ms, us);
    }
}

int main(void){
    eex_status_t err = eexStatusOK;

    samd_clkInit();


    log_printf("Starting Main\n");

    (void) eexThreadCreate(thread_idle, NULL,          0, "idle thread");
    (void) eexThreadCreate(thread,      &tls_thread_1, 1, "task 1");
    (void) eexThreadCreate(thread,      &tls_thread_2, 2, "task 2");
    (void) eexThreadCreate(thread_sec,  NULL,          3, "task second");

    log_printf("Starting Scheduler\n");

    eexKernelStart();

    return ((int) err);
}

