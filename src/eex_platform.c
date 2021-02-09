/*******************************************************************************

    eex_platform.c - Non architecture portable functions.

    Define a compiler macro to choose which platform to target:

    defined __CONSOLE - Linux or Cygwin
    __CORTEX_M == 0   - Cortex M0
    __CORTEX_M == 3   - Cortex M3
    __CORTEX_M == 4   - Cortex M4

    COPYRIGHT NOTICE: (c) ee-quipment.com
    All Rights Reserved

 ******************************************************************************/

#include  <stdint.h>
#include  "eex_os.h"


#if !((defined __CONSOLE) || ((defined __CORTEX_M) && ((__CORTEX_M == 0) || (__CORTEX_M == 3) || (__CORTEX_M == 4))))
    #error PLATFORM UNDEFINED OR NOT SUPPORTED
#endif


// millisecond counter, updated by SysTick
volatile uint32_t g_timer_ms = 0;


#if (defined __CONSOLE)

#include  <signal.h>
#include  <sys/time.h>

// schedule request pending
static volatile bool g_f_pend_scheduler = false;

uint32_t eexCPUAtomicCAS(uint32_t volatile *addr, uint32_t expected, uint32_t store) {
    *addr = store;
    return (0);
}

uint32_t eexCPUCLZ(uint32_t x) {
    return (x ? __builtin_clz(x) : 32);   // __builtin_clz undefined if x == 0
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

#endif  /* __CONSOLE */


#if (__CORTEX_M == 0)

uint32_t eexCPUAtomicCAS(uint32_t volatile *addr, uint32_t expected, uint32_t store) {
    uint32_t rslt = 1;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    if (*addr == expected) {
      *addr = store;
      rslt  = 0;
    }
    __set_PRIMASK(primask);
    return (rslt);
}

uint32_t eexCPUCLZ(uint32_t x) {
    static uint8_t const clz_lkup[] = {
        32U, 31U, 30U, 30U, 29U, 29U, 29U, 29U,
        28U, 28U, 28U, 28U, 28U, 28U, 28U, 28U
    };
    uint32_t n;

    if (x >= (1U << 16)) {
        if (x >= (1U << 24)) {
            if (x >= (1 << 28)) { n = 28U; }
            else { n = 24U; }
        }
        else {
            if (x >= (1U << 20)) { n = 20U; }
            else { n = 16U; }
        }
    }
    else {
        if (x >= (1U << 8)) {
            if (x >= (1U << 12)) { n = 12U; }
            else { n = 8U; }
        }
        else {
            if (x >= (1U << 4)) { n = 4U; }
            else { n = 0U; }
        }
    }
    return ((uint32_t) clz_lkup[x >> n] - n);
}

#endif    /* (__CORTEX_M == 0) */

#if ((__CORTEX_M == 3) || (__CORTEX_M == 4))
uint32_t eexCPUAtomicCAS(uint32_t volatile *addr, uint32_t expected, uint32_t store) {

    if (__LDREXW(addr) != expected) {
        return 1;
    }
    return (__STREXW(store, addr));
}

uint32_t eexCPUCLZ(uint32_t x) {
  return ((uint32_t) __CLZ(x));
}
#endif  /* ((__CORTEX_M == 3) || (__CORTEX_M == 4)) */

#if ((__CORTEX_M == 0) || (__CORTEX_M == 3) || (__CORTEX_M == 4))

// triggers pendSV exception and never returns
void eexKernelStart(void) {

    NVIC_SetPriority(PendSV_IRQn, EEX_CFG_INT_PRI_PENDSV);    // pendSV is always enabled at lowest possible priority
    (void) SysTick_Config(((EEX_CFG_CPU_FREQ)/1000));         // configure systick for 1 ms ticks
    NVIC_SetPriority (SysTick_IRQn, EEX_CFG_INT_PRI_LOWEST);  // set Systick at low priority
    __enable_irq();

    eexSchedulerPend();
}

// return ms and systick count
// convert systick count to an up-counter
// systick count is rollover protected and ms are adjusted for a possible unserviced interrupt
static void _eexKernelTimeRaw(uint32_t *p_ms, uint32_t *p_ticks) {
    uint32_t pend, ms, cnt;

    do {
        pend = SCB->ICSR & SCB_ICSR_PENDSTSET_Msk;            // Systick interrupt pending bit
        ms   = g_timer_ms;
        cnt  = SysTick->VAL;
    } while ((ms != g_timer_ms) ||                            // repeat if timer rolled over
             (pend != (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk))); // or interrupt occurred or was serviced

    if (pend) { ++ms; }   // Systick interrupt waiting to be serviced
    *p_ms    = ms;
    *p_ticks = SysTick->LOAD - cnt;
}

uint32_t eexKernelTime(uint32_t *us) {
    uint32_t ms, cnt, mhz;

    _eexKernelTimeRaw(&ms, &cnt);       // convert systick count to us
    mhz = (EEX_CFG_CPU_FREQ)/1000000;   // i.e. mhz=48 at 48 MHz CPU freq
    if (us) { *us = cnt/mhz; }          // 0 to 999 us
    return (ms);
}

// return total systick count mod-32, used for timestamping by Segger systemview
unsigned long SEGGER_SYSVIEW_X_GetTimestamp(void) {
    uint32_t ms, cnt, cycles_per_ms, total;

    _eexKernelTimeRaw(&ms, &cnt);
    cycles_per_ms = SysTick->LOAD + 1;
    total = cnt + (ms * cycles_per_ms);
    return (total);
}

// returns the ARM exception number or 0 if in thread mode
uint32_t eexInInterrupt() {
    return (SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk);
}

void SysTick_Handler(void) {
    ++g_timer_ms;
    EEX_PROFILE_ENTER;    // don't increment ms between profile timestamps
    if (eexThreadTimeout() > eexThreadID()) {
        eexSchedulerPend();
    }
    EEX_PROFILE_EXIT;
}


/******************************************************************************

    void PendSV_Handler(void)

    PendSV is the entrance and exit to and from the task scheduler.
    The scheduler is run in handler mode in order to eliminate the
    requirement that the scheduler be re-entrant.

    The PendSV exception is entered through eexSchedulerPend. When called from
    handler mode this will set the pendSV bit. Interrupts and the scheduler
    set the pendSV to force the scheduler to run (or re-run) when the
    interrupt exits.

    When eexSchedulerPend is called from thread mode, either in eexKernelStart
    or upon exit from a blocking thread, the pendSV bit is set and the
    PendSV exception is taken immediately. Upon entry to the pendSV handler
    the return address is examined to determine whether pendSV was entered
    from handler or thread mode.

    If entered from thread mode the exception frame is superfluous and is removed.

    A higher priority interrupt can interrupt PendSV and through an event action
    can make PendSV pending. However this will not preempt PendSV, but will
    simply cause PendSV to run again once it exits. Therefore PendSV, and thus
    the scheduler, will always run to completion before being called again.

    Threads are either in the ready state or the interrupted state. Interrupted
    threads have their state on the stack. Only the thread at the top of the
    stack can be scheduled, but conveniently this will be the highest priority
    thread of all the interrupted threads. Ready threads are dispatched by
    constructing an exception frame pointing to the thread entry point and
    with r0 set to point to the TCB.

    When the scheduler is done there will be an exception frame on the top of
    the stack pointing to the highest priority thread among the ready threads
    and the interrupted threads.

    Control is passed to the scheduled thread when PendSV returns. This changes
    the processor state from Handler Mode to Thread Mode.

    The handler may be entered one of three ways:
        In thread mode as a function call, as on the initial entry to the scheduler from main()
        In thread mode as the return address when a thread completes
        In handler mode as the exception handler for the PendSV interrupt

    In thread mode, either the thread is complete or the function call
    will never return. Therefore no registers need to be preserved.

    In handler mode, the scratch registers are on the stack and therefore
    do not need to be preserved.

    If in thread mode, change to handler mode through a PendSV interrupt.

******************************************************************************/
void eexSchedulerPend(void) __attribute__(( naked, noinline ));
void eexSchedulerPend(void) {
    __asm volatile (
        "ldr  r0, =0xe000ed04"        "\n\t"    // SCB->ICSR
        "movs r1, #1"                 "\n\t"
        "lsl  r1, r1, #28"            "\n\t"    // SCB_ICSR_PENDSVSET_Pos (should be lsls, gcc assembler chokes)
        "mrs  r2, ipsr"               "\n\t"
        "cmp  r2, #0"                 "\n\t"    // thread mode = 0
        "bne  _return"                "\n\t"    // in an interrupt - pendSV and return to interrupt handler
        "_pendsv_set:"                "\n\t"
        "str  r1, [r0]"               "\n\t"    // in thread mode - take pendSV exception
        "_wait:"                      "\n\t"
        "b   _wait"                   "\n\t"    // exception return PC should point here
        "_return:"                    "\n\t"
        "str  r1, [r0]"               "\n\t"    // pendSV pend bit set
        "bx lr"
    );
}

void PendSV_Handler(void) __attribute__(( naked ));
void PendSV_Handler(void) {
    __asm volatile (
        // if handler entered from thread mode, we are now in handler mode and stacked PC will point inside eexSchedulerPend function
        "movs r3, #1"                   "\n\t"    // r3 is scheduler argument = true, assume entered pendSV from an interrupt

                                                  // return address points to area around end of eexSchedulerPend if entered in thread mode
                                                  // precise address depends upon processor
        "ldr  r1, =_pendsv_set"         "\n\t"    // instruction location where pendSV pend bit set
        "ldr  r2, =_return"             "\n\t"    // instruction location above wait loop for pendSV exception
        "ldr  r0, [sp, #24]"            "\n\t"    // PC at point where exception happened
        "cmp  r0, r1"                   "\n\t"    // lower boundary of possible return address
        "blo  _stack_clean"             "\n\t"    // processor was in exception mode on entry, keep exception frame
        "cmp  r0, r2"                   "\n\t"    // upper boundary of possible return address
        "bhi  _stack_clean"             "\n\t"    // processor was in exception mode on entry, keep exception frame
        "movs r3, #0"                   "\n\t"    // otherwise processor was in thread mode, set scheduler argument false
        //"str  r3, [sp, #24]"            "\n\t"    // reset stacked pc in case pendSV is tail-chained from a new interrupt
        "add  sp, sp, #32"              "\n\t"    // not returning to thread mode calling location, remove extraneous exception frame
        "_stack_clean:"                 "\n\t"
    );

    /*
     * Now in handler mode with stack cleaned up and ready to run the scheduler.
     * eexScheduler is passed a boolean parameter, true if the pendSV handler was entered from an interrupt.
     * On return from eexScheduler()
     *    r0 = 0 if return from interrupt
     *    r0 = pointer to thread control block if jump to thread
     *
     * If jump to thread
     *    allocate exception frame on stack
     *    SP->R0 = argument passed to thread function
     *    SP->PC = thread function entry point
     *    SP->LR = thread return address (eexPendSVHandler)
     *    LR = return to thread mode
     */

    __asm volatile (
        "mov  r0, r3"                 "\n\t"    // scheduler argument (0=from thread, 1=from interrupt)
        "bl   eexScheduler"           "\n\t"
        "cmp  r0, #0"                 "\n\t"    // r0 is pointer to tcb, or NULL if returning to thread on stack
        "beq  _dispatch"              "\n\t"    // hpt thread is on top of interrupt stack
        "sub  sp, sp, #32"            "\n\t"    // create exception frame
        "ldr  r1, [r0, #0]"           "\n\t"    // tcb->fn_thread
        "str  r1, [sp, #24]"          "\n\t"    // #24 pc is thread function entry point
        "ldr  r2, [r0, #4]"           "\n\t"    // tcb->arg
        "str  r2, [sp, #0]"           "\n\t"    // #0 r0 is the function argument
        "ldr  r3, =eexSchedulerPend"  "\n\t"    // go to eexSchedulerPend when thread completes
        "str  r3, [sp, #20]"          "\n\t"    // #20 lr holds function return address
        "ldr  r0, =0x01000000"        "\n\t"    // default xpsr - thumb bit set
        "str  r0, [sp, #28]"          "\n\t"    // #28 xpsr
        "_dispatch:"                  "\n\t"
        "ldr  r0, =0xFFFFFFF9"        "\n\t"    // signal this is a return from exception, use MSP
        "mov  lr, r0"                 "\n\t"
        "bx lr"                       "\n\t"
    );
}


#endif    /* ((__CORTEX_M == 0) || (__CORTEX_M == 3) || (__CORTEX_M == 4)) */


