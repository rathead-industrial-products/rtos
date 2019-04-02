/*******************************************************************************

    eex_os.c - Real time executive.

    COPYRIGHT NOTICE: (c) 2018 DDPA LLC
    All Rights Reserved

 ******************************************************************************/

#include  <stdint.h>
#include  <stddef.h>
#include  <stdbool.h>
#include  <string.h>
#include  <limits.h>
#include  "eex_os.h"

#pragma GCC diagnostic ignored "-Wmultichar"    // Don't complain about e.g. 'MUTX'

#ifdef UNIT_TEST
#define STATIC
#else
#define STATIC static
#endif


#define EEX_EMPTY_THREAD_LIST         ((eex_thread_list_t) 0)   // const used to reset thread lists
#define EEX_PENDSV_EXCEPTION_NUMBER   (14)                      // arm defined

// helper macros
#define EEX_TIMEOUT_EXPIRED(timeout)  ((timeout) && (timeout != (uint32_t) eexWaitForever) && (_eexTimeDiff(timeout, eexKernelTime(NULL)) <= 0))

/*******************************************************************************

    Private Functions

 ******************************************************************************/
STATIC eex_tagged_data_t    _eexNewTaggedData(uint16_t data);
STATIC int32_t              _eexTimeDiff(uint32_t time, uint32_t ref);
STATIC bool                 _eexInScheduler(void);
STATIC void                 _eexBMSet(eex_bm_t * const a, uint32_t const bit);
STATIC void                 _eexBMClr(eex_bm_t * const a, uint32_t const bit);
STATIC uint32_t             _eexBMState(eex_bm_t const * const a, uint32_t const bit);
STATIC uint32_t             _eexBMFF1(const eex_bm_t a);
STATIC eex_thread_list_t *  _eexThreadListGet(eex_thread_list_selector_t which_list);
STATIC void                 _eexThreadListAdd(eex_thread_list_t *list, eex_thread_id_t tid);
STATIC void                 _eexThreadListDel(eex_thread_list_t *list, eex_thread_id_t tid);
STATIC bool                 _eexThreadListContains(const eex_thread_list_t *list, eex_thread_id_t tid);
STATIC eex_thread_id_t      _eexThreadListHPT(eex_thread_list_t list, eex_thread_list_t mask);
STATIC void                 _eexThreadIDSet(eex_thread_id_t tid);
int32_t                     _eexThreadTimeoutNext(void);
STATIC void                 _eexEventInit(void *yield_pt, eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t val, eex_kobj_cb_t *p_kobj, eex_event_action_t action);
STATIC void                 _eexEventRemove(eex_thread_id_t tid, eex_thread_event_t *event, eex_status_t status);
STATIC eex_thread_id_t      _eexEventTry(eex_thread_id_t evt_thread_priority, eex_thread_event_t *event);
STATIC bool                 _eexSemaMutexTry(const eex_thread_event_t *event);
STATIC bool                 _eexSignalTry(const eex_thread_event_t *event);

/*******************************************************************************

    Memory Allocation

 ******************************************************************************/

// Array of thread control blocks. The array index is the thread priority. Counting thread 0, there are EEX_CFG_USER_THREADS_MAX+1 threads.
STATIC eex_thread_cb_t g_thread_tcb[EEX_CFG_USER_THREADS_MAX+1] = { 0 };

// Thread lists
STATIC          eex_thread_list_t   g_thread_ready_list       = EEX_EMPTY_THREAD_LIST;
STATIC          eex_thread_list_t   g_thread_waiting_list     = EEX_EMPTY_THREAD_LIST;
STATIC          eex_thread_list_t   g_thread_interrupted_list = EEX_EMPTY_THREAD_LIST;

// currently running thread
STATIC volatile eex_thread_id_t     g_thread_running = 0;

// delay control block for all threads to share
eex_kobj_cb_t   delay_kobj  = { 'DLAY', 0, 0 };

// system timer declared in eex_arm.c
extern volatile uint32_t g_timer_ms;


/*******************************************************************************

    Utility Functions

 ******************************************************************************/

// Return a tagged data structure with a unique (rarely repeating) tag.
// note: It is unlikely, yet possible, to have an undetected tag collision.
STATIC eex_tagged_data_t  _eexNewTaggedData(uint16_t data) {
    static uint16_t   tag = 0;
    eex_tagged_data_t td;

    ++tag;
    if (tag == 0) { tag = 1; }    // don't allow a zero tag
    td.tag  = tag;
    td.data = data;
    return (td);
}

// Return the difference between two unsigned time values.
// Signed subtraction of two unsigned values will work correctly
// as long as the difference is < 2^31 (half the total unsigned value).
STATIC int32_t _eexTimeDiff(uint32_t time, uint32_t ref) {
    return ((int32_t) (time - ref));
}


/*******************************************************************************

    Thread Timeout

    _eexThreadTimeoutNext() - Return ms until the soonest thread timeout, or negative
    if a thread has timed out, or zero if there are no timeouts pending.

    eexThreadTimeout() - Return the thread id of the highest priority waiting thread
    that has timed out.

    Every waiting thread has a timeout value associated with it.
    Only threads can wait, so interrupts will not interfere with
    any timeout processing, except for the system timer which
    will pend the scheduler if it detects a timeout has occurred.

    Timeouts are only tested for waiting threads, so there is no hazard of an
    event init being interrupted by the timer.

    All timeout processing is done in the scheduler, guaranteeing
    unobstructed access to all thread data structures.

    There is no thread 0, so tid 0 is returned as a flag to indicate there
    are no upcoming timeouts.

******************************************************************************/

int32_t _eexThreadTimeoutNext(void) {
    eex_thread_list_t  *waiting_list = _eexThreadListGet(EEX_THREAD_WAITING);
    eex_thread_id_t     tid;
    eex_thread_cb_t    *tcb;
    uint32_t            timeout;
    int32_t             next_timeout=INT_MAX;

    for (tid=1; tid<=EEX_CFG_USER_THREADS_MAX; ++tid) {
        if (_eexThreadListContains(waiting_list, tid)) {
            tcb = eexThreadTCB(tid);
            timeout = tcb->event.timeout;
            if (timeout < next_timeout) { next_timeout = timeout; }
        }
    }
    if (next_timeout == INT_MAX) { next_timeout = 0; }            // no timeouts pending
    else { next_timeout = next_timeout - eexKernelTime(NULL); }   // ms until next timeout
    return (next_timeout);
}

eex_thread_id_t eexThreadTimeout(void) {
    eex_thread_list_t  *waiting_list = _eexThreadListGet(EEX_THREAD_WAITING);
    eex_thread_id_t     tid;
    eex_thread_cb_t    *tcb;
    uint32_t            timeout;
    eex_thread_list_t   mask = EEX_EMPTY_THREAD_LIST;

    while ((tid = _eexThreadListHPT(*waiting_list, mask))) {
        tcb = eexThreadTCB(tid);
        timeout = tcb->event.timeout;
        if (EEX_TIMEOUT_EXPIRED(timeout)) { break; }
        _eexThreadListAdd(&mask, tid);
    }
    return (tid);
}


/*******************************************************************************

    Thread Lists

    Thread lists keep track of which threads are waiting or ready or interrupted
    as well as which threads are waiting for resource availability in a kernel
    object.

    Thread lists are maintained as bitmaps with the bit position corresponding
    to the thread ID. This allows efficient sorting by priority using CLZ.

    eexThreadListHPT returns the highest priority task in the list, optionally masked.

    The current implementation uses a 32 bit word, allowing 32 threads plus
    thread zero, represented by no bits set in the bitmap.

    Bit positions are numbered starting from the leftmost bit = 32.
    This is equal to (32 - clz).
    Zero is a valid bit number for set and clr and means set or clear no bits.

 ******************************************************************************/
STATIC void  _eexBMSet(eex_bm_t * const a, uint32_t const bit) {
    uint32_t old_bm;
    uint32_t new_bm;

    assert(a && (bit <= 32));
    if (bit == 0) { return; }
    do {
        old_bm = *a;
        new_bm = old_bm | (0x80000000u >> (32 - bit));
    } while(eexCPUAtomicCAS(a, old_bm, new_bm));
}

STATIC void  _eexBMClr(eex_bm_t * const a, uint32_t const bit) {
    uint32_t old_bm;
    uint32_t new_bm;

    assert(a && (bit <= 32));
    if (bit == 0) { return; }
    do {
        old_bm = *a;
        new_bm = old_bm & ~(0x80000000u >> (32 - bit));
    } while(eexCPUAtomicCAS(a, old_bm, new_bm));
}

STATIC uint32_t  _eexBMState(eex_bm_t const * const a, uint32_t const bit) {
    uint32_t state;

    assert(a && bit && (bit <= 32));
    state = (*a & (0x80000000u >> (32 - bit))) ? 1 : 0;
    return (state);
}

STATIC uint32_t  _eexBMFF1(const eex_bm_t a) {
    uint32_t clz = eexCPUCLZ(a);
    return (32 - clz);
}

STATIC eex_thread_list_t * _eexThreadListGet(eex_thread_list_selector_t which_list) {
    if (which_list == EEX_THREAD_READY)       { return (&g_thread_ready_list);       }
    if (which_list == EEX_THREAD_WAITING)     { return (&g_thread_waiting_list);     }
    if (which_list == EEX_THREAD_INTERRUPTED) { return (&g_thread_interrupted_list); }
    assert(0);
}

STATIC void _eexThreadListAdd(eex_thread_list_t *list, eex_thread_id_t tid) {
    _eexBMSet(list, tid);
}

STATIC void _eexThreadListDel(eex_thread_list_t *list, eex_thread_id_t tid) {
    _eexBMClr(list, tid);
}

STATIC bool _eexThreadListContains(const eex_thread_list_t *list, eex_thread_id_t tid) {
    if (tid && _eexBMState(list, tid)) { return (true); }
    return (false);
}

STATIC eex_thread_id_t _eexThreadListHPT(eex_thread_list_t list, eex_thread_list_t mask) {
    return ((eex_thread_id_t) _eexBMFF1((~mask) & list));
}


/*******************************************************************************

    Scheduler

    The scheduler is passed a boolean indicating if it was called from an
    interrupt handler.

    If called from an interrupt handler, the running thread is added to the
    interrupted list.

    If the scheduler has not been called from an interrupt handler, the
    running thread has blocked from a PEND or POST operation.

    When a thread frees a higher priority thread as a result of a PEND or
    POST operation, the thread will block and the scheduler will run.

    The ready, interrupted, and waiting lists are examined to find the highest
    priority thread (HPT). If the HPT is ready, it is set as the running thread
    and will be dispatched by the caller. If the HPT is on the stack, it is set
    as the running thread and the caller will perform a return-from-interrupt.
    If the HPT is waiting, then the event that it is waiting on is tried, and
    if it successfully acquires the resource then it is treated as a ready
    thread and set as the running thread and dispatched by the caller. If the
    resource cannot be acquired, that thread is masked and another attempt is
    made to determine the HPT.

    When the scheduler tries an event and the result is successful the
    associated thread will be dispatched. If the result of trying the
    event also frees a higher priority task, the scheduler will be pended
    which will cause a pendSV interrupt immediately after the thread is
    dispatched. The dispatched thread will be put on the interrupted list
    and the scheduler will run again.

    If a thread is blocked waiting on a mutex, then the mutex is being held
    by a lower priority thread. The thread holding the mutex must have been
    interrupted, as it would be poor form to acquire a mutex and then block.
    To minimize the effect of priority inversion all waiting threads below
    the priority of the thread pending on the mutex are ignored while threads
    that have been interrupted are dispatched. The thread holding the mutex
    will eventually reach the top of the stack and be dispatched, allowing it
    to run and release the mutex.

    If there are no threads that can be run then eexIdleHook() is called.
    The scheduler will continue to loop, calling eeexIdleHook() until a thread unblocks.

    The eexIdleHook() function is normally used to put the processor to sleep.
    It returns ms count that is added to the system clock when it returns.
    It can pend or post, but cannot block. It is NOT a thread and must return.

    The scheduler will always run to completion and so does not have to be
    reentrant. It may be interrupted, but a thread will never run concurrently
    with the scheduler. This allows the scheduler unfettered access to all
    of the threads' control blocks.

******************************************************************************/

eex_thread_cb_t * eexScheduler(bool from_interrupt) {
    eex_thread_list_t  *ready_list       = _eexThreadListGet(EEX_THREAD_READY);
    eex_thread_list_t  *interrupted_list = _eexThreadListGet(EEX_THREAD_INTERRUPTED);
    eex_thread_list_t  *waiting_list     = _eexThreadListGet(EEX_THREAD_WAITING);
    eex_thread_id_t     running_tid      = eexThreadID();
    eex_thread_event_t *event            = &(eexThreadTCB(running_tid)->event);
    uint32_t            old_ms, ms_asleep;

    EEX_PROFILE_SCHED_ENTER(running_tid, from_interrupt);

    // threads are interrupted
    //   or block after a successful pend or post but also free up a higher priority thread
    //   or block due to a resource not being available
    if (from_interrupt)                             { _eexThreadListAdd(interrupted_list, running_tid); }
    else if (event->action == EEX_EVENT_NO_ACTION)  { _eexThreadListAdd(ready_list, running_tid); }
    else                                            { _eexThreadListAdd(waiting_list, running_tid); }

    eex_thread_list_t thread_waiting_mask = EEX_EMPTY_THREAD_LIST;
    eex_thread_id_t   ready_thread, unblock_thread;
    eex_thread_cb_t  *ready_tcb;
    eex_thread_id_t   hoisted_thread = 0;

    // loop until there is a thread ready to dispatch
    for (;;) {
        if (hoisted_thread) {   // hoisted priority of a thread that holds a mutex
            ready_thread   = hoisted_thread;
            hoisted_thread = 0;
        }
        else {
            ready_thread = _eexThreadListHPT((*ready_list | *interrupted_list | *waiting_list), thread_waiting_mask);
        }
        ready_tcb = eexThreadTCB(ready_thread);
        event     = &(ready_tcb->event);

        // thread is ready to run, launch it
        if (_eexThreadListContains(ready_list, ready_thread)) {
            _eexThreadListDel(ready_list, ready_thread);
            break;
        }

        // thread was interrupted, return to it
        else if (_eexThreadListContains(interrupted_list, ready_thread)) {
            _eexThreadListDel(interrupted_list, ready_thread);
            ready_tcb = NULL;   // restore thread context from stack
            break;
        }

        // thread waiting on event, dispatch it if event can be satified or it timed out
        else if (_eexThreadListContains(waiting_list, ready_thread)) {
            unblock_thread = _eexEventTry(ready_thread, event);
            if (unblock_thread) {
                _eexThreadListDel(waiting_list, ready_thread);  // thread unblocked, dispatch it
                if (unblock_thread > ready_thread) {            // potentially unblocked a higher priority thread
                    eexSchedulerPend();                         // rerun scheduler to try hpt
                }
                break;
            }
            else {
                // mask out waiting thread, search for next-highest priority thread
                _eexThreadListAdd(&thread_waiting_mask, ready_thread);
                // if the waiting event was a mutex and the mutex owner has a lower priority
                //     and the mutex owner is waiting, then try it next (hoist priority)
                //     and the mutex owner is interrupted, then ignore waiting threads and
                //     start dispatching interrupted threads until the mutex owner runs
                if ((event->kobj) && (event->kobj->type == 'MUTX')) {
                    eex_thread_id_t mutex_owner = ((eex_sema_mutex_cb_t *) (event->kobj))->owner_id;
                    if (mutex_owner < ready_thread) {   // priority inversion
                        // try mutex_owner next instead of waiting for its turn
                        if (_eexThreadListContains(waiting_list, mutex_owner)) {
                            _eexThreadListAdd(&thread_waiting_mask, mutex_owner);
                            hoisted_thread = mutex_owner;
                        }
                        // mutex_owner was interrupted, start dispatching interrupted threads
                        else { thread_waiting_mask |= *waiting_list; }
                    }
                }
            }
        }

        // no thread is ready, waiting, or interrupted
        else {
            // call idle hook to sleep until next timeout or interrupt
            ms_asleep = eexIdleHook(_eexThreadTimeoutNext());
            // adjust ms timer for time spent in idle hook
            do { old_ms = g_timer_ms; }
            while (eexCPUAtomicCAS(&g_timer_ms, old_ms, old_ms + ms_asleep));

            // reset waiting thread mask and run scheduler again
            //EEX_PROFILE_SCHED_IDLE;   // tell profiler system is idling if no idle thread dispatch
            thread_waiting_mask = EEX_EMPTY_THREAD_LIST;
        }
    }
    _eexThreadIDSet(ready_thread);
    EEX_PROFILE_SCHED_EXIT(ready_thread);
    return (ready_tcb);
}

// Placeholder in case user does not define an idle function
__attribute__ ((weak)) uint32_t eexIdleHook(int32_t sleep_for_ms)  {
    return (0);
}

STATIC bool _eexInScheduler(void) {
    return (eexInInterrupt() == EEX_PENDSV_EXCEPTION_NUMBER);
}


/*******************************************************************************

    Events

    An event is a container for a kernel object (semaphore, mutex, queue, etc. )
    along with all the control and data fields needed to allow it to be run standalone
    in the thread, scheduler, or in an interrupt handler independent of its association
    to a particular thread.

    Kernel Objects:

        Semaphore:

        Mutex:

        Signal:
        A signal is a 32 bit non-zero value. This value is OR'd with any
        currently set bits in the signal value. The signal will be cleared to zero
        whenever it is read by a PEND operation.

    eexEventInit is the eventual target of a pend or post macro and configures the
    event fields in a thread or dummys up an event for an interrupt, then tries
    the event.

    eexEventTry will attempt a pend or post operation to a kernel object and return
    a thread_id_t representing a thread to unblock, if any. The response to eexEventTry
    depends upon the caller (interrupt, scheduler, or thread). The evt_thread_priority
    parameter is the running thread when called from an interrupt or a thread, and is
    a waiting thread when called from the scheduler.

    Trying an event is thread safe because the scheduler will only try events that belong to
    threads that are waiting.

    Return tid == 0, the event access failed
    interrupt: should never return 0 to an interrupt
    scheduler: ignore
    thread:    block

    Return tid == evt_thread_priority, the event access was successful or non-blocking
    interrupt: continue
    scheduler: make evt_thread_priority ready to run
    thread:    continue

    Return tid > evt_thread_priority, the event access was successful and
      unblocked a higher priority thread as well.
    interrupt: pend the scheduler
    scheduler: pend the scheduler (re-run scheduler after dispatching evt_thread_priority)
    thread:    block

 ******************************************************************************/

bool eexPendPost(void *func_yield_pt, eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t val, eex_kobj_cb_t *p_kobj, eex_event_action_t action) {
    eex_thread_event_t   int_event;   // allocate a dummy non-blocking event on the stack for an interrupt
    eex_thread_event_t  *event;
    eex_thread_id_t      running_tid, unblock_tid;
    bool                 f_block, f_in_interrupt;

    running_tid    = eexThreadID();
    f_in_interrupt = (bool) eexInInterrupt();
    f_block        = false;   // assume event access will be successful

    assert ((p_kobj) && ((action == EEX_EVENT_PEND) || (action == EEX_EVENT_POST)));
    EEX_PROFILE_PEND_POST(running_tid, eexInInterrupt(), timeout, val, p_kobj, action);

    /*
     * Initialize a pointer to the event.
     * Interrupts will have a temporary event created on the stack, since
     * they can't block or be preempted. The event will be destroyed right after trying.
     */
    if (!f_in_interrupt) {
        event = &(eexThreadTCB(running_tid)->event);
    }
    else {
        event = &int_event;     // temporary dummy event on stack
        func_yield_pt = event;  // repurpose func_yield_pt to point to dummy event
    }

    // test for illegal operations
    if (timeout && ((f_in_interrupt) || (running_tid == 0))) {  // interrupts and thread 0 can't block
        if (p_rtn_status != NULL) { *p_rtn_status = eexStatusBlockErr; }
        return (f_block);
    }

    // initialize and try the event
    _eexEventInit(func_yield_pt, p_rtn_status, p_rtn_val, timeout, val, p_kobj, action);
    unblock_tid = _eexEventTry(running_tid, event);

    // pend/post from interrupt, event access was successful and unblocked a thread
    if (f_in_interrupt && (unblock_tid > running_tid)) { eexSchedulerPend(); }

    // pend/post from thread, event access was unsuccessful or was successful and unblocked a thread
    if (!f_in_interrupt && ((unblock_tid == 0) || (unblock_tid > running_tid))) { f_block = true; }

    return (f_block);
}

// initialize the event fields upon execution of a pend or post operation
STATIC void _eexEventInit(void *func_yield_pt, eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t val, eex_kobj_cb_t *p_kobj, eex_event_action_t action) {
    eex_thread_cb_t     *tcb;
    eex_thread_event_t  *event;
    eex_thread_id_t      tid;

    tid = (eexInInterrupt()) ? 0 : eexThreadID();                 // thread 0 acts as a no-op below for interrupts
    tcb = eexThreadTCB(tid);

    event = (eexInInterrupt()) ? func_yield_pt : &(tcb->event);   // if interrupt event use dummy event passed in func_yield_pt
    tcb->resume_pc = func_yield_pt;
    event->kobj    = p_kobj;
    event->action  = action;
    event->rslt    = p_rtn_status;
    if (p_rtn_status != NULL) { *p_rtn_status = eexStatusInvalid; }
    event->p_val   = p_rtn_val;
    event->val     = val;

    // prospectively add to the kernel object waiting list, won't be acted on unless thread is waiting
    if (action == EEX_EVENT_PEND) { _eexThreadListAdd(&(p_kobj->pend), tid); }
    if (action == EEX_EVENT_POST) { _eexThreadListAdd(&(p_kobj->post), tid); }

    // timeout handling
    // Normal timeout - add the current time to timeout_ms to get the clock time for the timout
    //                  don't let it expire at clock time zero (rollover), that's the flag for no timeout
    // No timeout (0) - leave the timeout_ms value unchanged - interrupt timeout is always 0
    // Wait forever   - leave the timeout_ms value unchanged
    if ((!timeout) || (timeout == (unsigned) eexWaitForever)) {
        event->timeout = timeout;
    }
    else {
        if (timeout > (uint32_t) eexWaitMax) { timeout = (uint32_t) eexWaitMax; } // max allowable timeout delay
        event->timeout = eexKernelTime(NULL) + timeout;                           // convert timeout delay to clock time
        if (event->timeout == 0) { event->timeout = 1; }                          // don't allow it to expire on 0
    }
}

// Remove an event from a thread and clean up after a timeout or event completion.
STATIC void _eexEventRemove(eex_thread_id_t tid, eex_thread_event_t *event, eex_status_t status) {

    assert (event);
    assert (event->kobj);

    // clean up event wait lists except for interrupt events
    if (!eexInInterrupt() || _eexInScheduler()) { // scheduler doesn't count as an interrupt, it trying thread events by proxy
        _eexThreadListDel(&(event->kobj->pend), tid);
        _eexThreadListDel(&(event->kobj->post), tid);
    }

    if (event->rslt) { *(event->rslt) = status; }
    (void) memset(event, 0, sizeof(eex_thread_event_t));  // clear all event fields
}

STATIC eex_thread_id_t _eexEventTry(eex_thread_id_t evt_thread_priority, eex_thread_event_t *event) {
    eex_kobj_cb_t    *p_kobj;
    eex_thread_id_t   hpt;
    uint32_t          unblock;
    bool              f_pend, f_post, try_rslt;

    assert(event);
    assert(event->kobj);
    assert(evt_thread_priority <= EEX_CFG_USER_THREADS_MAX);
    p_kobj = event->kobj;
    f_pend = (event->action == EEX_EVENT_PEND);
    f_post = (event->action == EEX_EVENT_POST);
    assert(f_pend || f_post);

    // test for timeout
    if (EEX_TIMEOUT_EXPIRED(event->timeout)) {
        _eexEventRemove(evt_thread_priority, event, eexStatusThreadTimeout);
        return (evt_thread_priority);
    }

    switch (p_kobj->type) {

        case 'SEMA':
        case 'MUTX':
            try_rslt = _eexSemaMutexTry(event);
            unblock = evt_thread_priority;              // assume success or non-blocking failure
            if (event->action == EEX_EVENT_PEND) {      // decrement the semaphore, acquire the mutex
                if (try_rslt) {                         // semaphore was taken successfully, mutex was acquired
                    if (p_kobj->type == 'MUTX') { ((eex_sema_mutex_cb_t *) event->kobj)->owner_id = (uint16_t) evt_thread_priority; }
                    assert (!(p_kobj->post));           // threads should never block on a post, so none should be waiting
                    _eexEventRemove(evt_thread_priority, event, eexStatusOK);
                }
                else {                                  // resource not available
                    if ((event->timeout) == 0)  { _eexEventRemove(evt_thread_priority, event, eexStatusEventNotReady); }  // non-blocking
                    else                        { unblock = 0; }                                                          // blocking
                }
            }
            else /* EEX_EVENT_POST */ {                 // increment the semaphore or release the mutex
                if (!try_rslt) { assert(0); }           // should never block
                if (p_kobj->type == 'MUTX') { ((eex_sema_mutex_cb_t *) event->kobj)->owner_id = 0; }
                _eexEventRemove(evt_thread_priority, event, eexStatusOK);
                // test if posting unblocked a waiting higher priority thread
                hpt = _eexThreadListHPT(p_kobj->pend, EEX_EMPTY_THREAD_LIST);
                if (hpt > evt_thread_priority) {
                    unblock = hpt;
                }
            }
            break;

        case 'SIGL':
            try_rslt = _eexSignalTry(event);
            unblock = evt_thread_priority;              // assume success or non-blocking failure
            if (event->action == EEX_EVENT_PEND) {
                if (try_rslt) {                         // signal has bit(s) set that we are pending on
                    assert (!(p_kobj->post));           // threads should never block on a post, so none should be waiting
                    _eexEventRemove(evt_thread_priority, event, eexStatusOK);
                }
                else {                                  // no signal, or a signal but not the bits we want
                    if ((event->timeout) == 0)  { _eexEventRemove(evt_thread_priority, event, eexStatusSignalNone); }   // non-blocking
                    else                        { unblock = 0; }                                                        // blocking
                }
            }
            else /* EEX_EVENT_POST */ {
                if (!try_rslt) { assert(0); }           // post the signal to the target, should never fail
                _eexEventRemove(evt_thread_priority, event, eexStatusOK);
                // test if posting unblocked a waiting higher priority thread
                hpt = _eexThreadListHPT(p_kobj->pend, EEX_EMPTY_THREAD_LIST);
                if (hpt > evt_thread_priority) {
                    unblock = hpt;
                }
            }
            break;

        case 'DLAY':
            unblock = 0;  // timeout hasn't expired, block
            break;

        default:
            assert (0);
    }
    return (unblock);
}

// Attempt to increment, decrement, or get the current semaphore or mutex value.
// Return true if successful, false if the resource is not available. Set the event return value to the count value.
STATIC bool _eexSemaMutexTry(const eex_thread_event_t *event) {
    eex_sema_mutex_cb_t *sema;
    eex_tagged_data_t    old_cnt, new_cnt;
    uint32_t            *rtn_val;
    bool                 f_pend, f_post;

    assert (event);
    assert (event->kobj);

    sema    = (eex_sema_mutex_cb_t *) event->kobj;
    rtn_val = event->p_val;
    f_pend  = (event->action == EEX_EVENT_PEND);
    f_post  = (event->action == EEX_EVENT_POST);
    assert(f_pend || f_post);

    do {
        old_cnt.data = sema->count.data;
        if (rtn_val) { *rtn_val = (uint32_t) old_cnt.data; }
        if (f_pend && (old_cnt.data == 0))              { return (false); } // semaphore/mutex not available
        if (f_post && (old_cnt.data == sema->max_val))  { break; }
        new_cnt = _eexNewTaggedData((uint16_t) (old_cnt.data + (f_post ? 1 : -1)));
        if (rtn_val) { *rtn_val = (uint32_t) new_cnt.data; }
    } while(eexCPUAtomicCAS(&(sema->count.td), old_cnt.td, new_cnt.td));

    assert (sema->count.data <= sema->max_val);                               // semaphore/mutex overflow
    assert (!((sema->cb.type == 'MUTX') && f_post && (old_cnt.data == 1)));   // recursive mutexes are NOT supported

    return (true);
}

// Set or read signal bits.
// Return true if any mask bits match their associated signal bits.
// Set the event return value to the bits that match, and clear those bits in signal.
STATIC bool _eexSignalTry(const eex_thread_event_t *event) {
    eex_signal_cb_t    *p_signal_cb;
    eex_signal_t       *p_signal, signal, new_signal, set_bits;
    bool                f_pend, f_post;

    assert (event);
    assert (event->kobj);

    p_signal_cb = (eex_signal_cb_t *) (event->kobj);
    p_signal    = &(p_signal_cb->signal);
    f_pend      = (event->action == EEX_EVENT_PEND);
    f_post      = (event->action == EEX_EVENT_POST);
    assert(f_pend || f_post);

    do {
        signal   = *p_signal;
        set_bits = signal & event->val;                     // val is mask if pend
        if (f_pend) { new_signal = signal & ~set_bits; }    // clear signaled bits
        else        { new_signal = signal | event->val; }   // add new bits to signal is post
    } while(eexCPUAtomicCAS(p_signal, signal, new_signal));

    if (event->p_val) { *(event->p_val) = set_bits; }       // n/a if post
    return((f_post) ? true : (bool) set_bits);              // post always succeeds
}


/*******************************************************************************

    Threads

 ******************************************************************************/

eex_status_t eexThreadCreate(eex_thread_fn_t fn_thread, void * const tls, const uint32_t priority, const char *name) {
    eex_thread_cb_t * tcb;

    // valid thread priorities are 1 through EEX_CFG_USER_THREADS_MAX inclusive.
    if((priority == 0) || (priority > EEX_CFG_USER_THREADS_MAX)) { return (eexStatusThreadCreateErr); }

    tcb = eexThreadTCB(priority);

    // test that a thread with this priority has not already been created
    if (tcb->fn_thread != NULL) {
        return (eexStatusThreadPriorityErr);
    }

    tcb->fn_thread = fn_thread;
    tcb->arg       = tls;
    tcb->name      = name;
    _eexThreadListAdd(_eexThreadListGet(EEX_THREAD_READY), priority);

    EEX_PROFILE_API_CALL_CREATE_THREAD(priority);
    return (eexStatusOK);
}

eex_thread_cb_t * eexThreadTCB(eex_thread_id_t tid) {
    assert (tid <= EEX_CFG_USER_THREADS_MAX);
    return (&g_thread_tcb[tid]);
}

eex_thread_id_t eexThreadID(void) {
    return (g_thread_running);
}

STATIC void _eexThreadIDSet(eex_thread_id_t tid) {
    assert (tid <= EEX_CFG_USER_THREADS_MAX);
    g_thread_running = tid;
}


