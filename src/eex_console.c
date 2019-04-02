/*******************************************************************************

    eex_platform_console.c - Non architecture portable functions.

    Link this file in place of eex_platform_arm.c when running on a unix console.

    COPYRIGHT NOTICE: (c) 2018 DDPA LLC
    All Rights Reserved

 ******************************************************************************/

#include  <signal.h>
#include  <sys/time.h>
#include  "eex_kernel.h"


// millisecond counter
static volatile uint32_t g_timer_ms = 0;

// schedule pending request
static volatile bool g_f_pend_scheduler = false;

uint32_t eexCPUAtomicCAS(uint32_t volatile *addr, uint32_t expected, uint32_t store) {
    *addr = store;
    return (0);
}

uint32_t eexCPUCLZ(uint32_t x) {
  return (x ? __builtin_clz(x) : 32);   // __builtin_clz indefined if x == 0
}


void Alarm_Handler(int sig) {
    ++g_timer_ms;
    // if (eexThreadTimeout()) {
    // run scheduler on exit from signal handler
    // can't do it, will have to wait until running thread blocks
    // infinite loop if idle task exists
    // }
}


uint32_t eexInInterrupt() { return (0); }   // no interrupts in console

inline void eexSchedulerPend(void) { g_f_pend_scheduler = true; }

void  eexKernelStart(void) {
    struct itimerval   it;
    eex_thread_cb_t   *tcb;

    // set up 1 ms interrupt
    //signal(SIGALRM, Alarm_Handler);
    it.it_interval.tv_sec  = 0;
    it.it_interval.tv_usec = 1000;
    it.it_value.tv_sec  = 0;
    it.it_value.tv_usec = 1000;
    (void) setitimer(ITIMER_REAL, &it, NULL); // ITIMER_VIRTUAL doesn't seem to work

    // loop forever dispatching threads
    for (;;) {
        tcb = eexScheduler(false);
        if (g_f_pend_scheduler) {    // rerun scheduler before dispatching thread
            g_f_pend_scheduler = false;
            continue;
        }
        tcb->fn_thread(tcb->arg);
    }
}

uint32_t eexKernelTime(uint32_t *us) {
    struct itimerval  it;
    uint32_t          ms;
    do {
        ms = g_timer_ms;
        getitimer(ITIMER_REAL, &it);
        if (us) { *us = ((uint32_t) it.it_value.tv_usec) % 1000; }
    } while (ms != g_timer_ms);    // repeat if timer rolled over
    return (ms);
}





