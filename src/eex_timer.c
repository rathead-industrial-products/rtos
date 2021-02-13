/*******************************************************************************

    eex_timer.c - Real time executive Timer.

    COPYRIGHT NOTICE: (c) ee-quipment.com
    All Rights Reserved

 ******************************************************************************/


#include  <stdint.h>
#include "eex_os.h"
#include "eex_timer.h"





/*******************************************************************************
 *
 *  Timers are normal functions (not threads). They wake up occasionally and
 *  the function runs. There is a timer control thread, created by the OS with
 *  priority EEX_CFG_TIMER_THREAD_PRIORITY, that manages the timers and calls their
 *  functions.
 *
 *  If EEX_CFG_TIMER_THREAD_PRIORITY == 0, the timer control thread is not created.
 *
 *  Each timer has a control block. There are three values that control when the
 *  timer expires and how it is reloaded.
 *
 *  timer->interval is the delay in ms between invocations of the timer function.
 *  A nonzero interval value meas the timer is periodic and runs repeatedly until
 *  stopped or removed.
 *
 *  If the interval value is zero the timer is a one-shot. It runs once and then
 *  stops. After running once is will still be managed by the timer control thread
 *  and so it can be restarted.
 *
 *  timer->remaining is the remaining time in ms before a stopped timer would
 *  expire if the timer was running. timer->remaining is set only when a running
 *  timer is stopped. It is not updated continuously. timer->remaining is also
 *  set to the delay value when eexTimerStart() is called. This allows a periodic
 *  timer to have a delay value from start that is different from the periodic
 *  value.
 *
 *  timer->expiry is the kernel clock time when the timer will expire. This is
 *  not a ms delay value, but an absolute expiration time.
 *
 *  An active timer is a timer that has been added with eexTimerAdd() and has not
 *  been removed with eexTimerRemove().
 *
 *  Active timers are kept in a linked list managed by the timer control thread.
 *  When a thread calls eexTimerAdd() the timer control block is inserted into
 *  a thread-safe "add queue" and the timer control thread is signaled. The timer
 *  control thread removes any timer control blocks in the "add queue" in a thread-
 *  safe manner and adds them to its internal active list. The timer eexTimerActive
 *  status bit is then set.
 *
 *  When a thread calls eexTimerAdd, because of the relative priority of the
 *  calling thread and the timer control thread no assumptions can be made about
 *  when the thread will actually run.
 *
 *  Similarly, if a thread calls eexTimerRemove and wants to reclaim the timer
 *  control block memory, it must wait until the eexTimerActive status bit is
 *  cleared. Only then can the control block memory be overwritten.
 *
 ******************************************************************************/


typedef enum  {
    _timerStatusBits    = 0x000000ff,     // status bits defined in eex_timer.h
                                          // field positions of status and control bits
    _timerStatusActive  = 1,
    _timerStatusRunning = 2,
    _timerCtlStart      = 8,              // app has commanded timer to start
    _timerCtlStop       = 9,              // app has commanded timer to start
    _timerCtlRemove     = 10,             // app has commanded timer be removed
} eex_timer_control_t;

static eex_timer_cb_t *g_active_timer_list_head = NULL;
static eex_timer_cb_t *g_add_timer_list_head    = NULL;


EEX_SIGNAL_NEW(sig_timer);        // Used to signal timer thread when a timer function is called



/*******************************************************************************

    Private Functions

 ******************************************************************************/
STATIC void  _atomicBitSet(uint32_t * const field, uint32_t const bit);
STATIC void  _atomicBitClr(uint32_t * const field, uint32_t const bit);



/*******************************************************************************

    Atomic bit field operations

 ******************************************************************************/
STATIC void  _atomicBitSet(uint32_t * const field, uint32_t const bit) {
    uint32_t old;
    uint32_t new;

    assert(field && (bit <= 32));
    if (bit == 0) { return; }
    do {
        old = *field;
        new = old | (0x80000000u >> (32 - bit));
    } while(eexCPUAtomicCAS(field, old, new));
}

STATIC void  _atomicBitClr(uint32_t * const field, uint32_t const bit) {
    uint32_t old;
    uint32_t new;

    assert(field && (bit <= 32));
    if (bit == 0) { return; }
    do {
        old = *field;
        new = old & ~(0x80000000u >> (32 - bit));
    } while(eexCPUAtomicCAS(field, old, new));
}


/*******************************************************************************

    void eexTimerAdd(eex_timer_cb_t * timer)

    This function is part of the API.

    Add the timer to the add_timer_list to be inserted into the list of
    active timers by the timer thread the next time it runs. Signal the
    timer thread so it will run ASAP.

******************************************************************************/
void eexTimerAdd(eex_timer_cb_t * timer) {
    eex_timer_cb_t * old_head, new_head;

    if (!timer->fn_timer) { return; }   // check for null function pointer
    timer->control   = 0;
    timer->remaining = 0;
    timer->expiry    = 0;

    do {
        old_head = g_add_timer_list_head;
        new_head = timer;
        timer->next = old_head;
    } while(eexCPUAtomicCAS(&g_add_timer_list_head, old_head, new_head));

    /* Posts never fail, ignore return status. Signal value has no meaning. */
    eexPostSignal(NULL, 1, sig_timer);

}


/*******************************************************************************

    void eexTimerRemove(eex_timer_cb_t * timer)

    This function is part of the API.

    Mark the timer to be deleted from the list of active timers by the timer
    thread the next time it runs. Signal the timer thread so it will run ASAP.

******************************************************************************/
void eexTimerRemove(eex_timer_cb_t * timer) {

    _atomicBitSet(&timer->control, _timerCtlRemove);  // set remove bit

    /* Posts never fail, ignore return status. Signal value has not meaning. */
    eexPostSignal(NULL, 1, sig_timer);

}


/*******************************************************************************

    void eexTimerStart(eex_timer_cb_t * timer, uint32_t delay)

    This function is part of the API.

    Mark the timer to be started by the timer thread the next time it runs.
    Signal the timer thread so it will run ASAP.

    Timer may be reset by calling this function while the timer is
    already running. It will be restarted with the new delay value.

******************************************************************************/
void eexTimerStart(eex_timer_cb_t * timer, uint32_t delay) {

    timer->remaining = delay;
    _atomicBitSet(&timer->control, _timerCtlStart);  // set start bit

    /* Posts never fail, ignore return status. Signal value has not meaning. */
    eexPostSignal(NULL, 1, sig_timer);

}


/*******************************************************************************

    void eexTimerStop(eex_timer_cb_t * timer)

    This function is part of the API.

    Mark the timer to be stopped by the timer thread the next time it runs.
    Signal the timer thread so it will run ASAP.

******************************************************************************/
void eexTimerStop(eex_timer_cb_t * timer) {

    _atomicBitSet(&timer->control, _timerCtlStop);  // set stop bit

    /* Posts never fail, ignore return status. Signal value has not meaning. */
    eexPostSignal(NULL, 1, sig_timer);

}


/*******************************************************************************

    void eexTimerResume(eex_timer_cb_t * timer)

    This function is part of the API.

    Load the timer with timer->remaining and start it.

******************************************************************************/
void eexTimerResume(eex_timer_cb_t * timer) {

    _atomicBitSet(&timer->control, _timerCtlStart);  // set start bit

    /* Posts never fail, ignore return status. Signal value has not meaning. */
    eexPostSignal(NULL, 1, sig_timer);

}


/*******************************************************************************

    eex_timer_status_t eexTimerRemove(eex_timer_cb_t * timer)

    This function is part of the API.

    Return the timer status field. Mask out the control bits.

******************************************************************************/
eex_timer_status_t eexTimerStatus(eex_timer_cb_t * timer) {
    return (timer->status & _timerStatusBits);
}


/*******************************************************************************

    void _eexTimerTask(eexTimerTask_tls *tls)

    This function is not part of the API.

    If EEX_CFG_TIMER_THREAD_PRIORITY is a valid thread priority then
    this thread is created by eexKernelStart().

******************************************************************************/
void eexTimerThread(void * const argument) { {
    static  eex_status_t    rtn_status;
    static  uint32_t        rtn_val;
    eex_timer_cb_t          dummy_cb, *timer, *active;
    int32_t                 remaining;
    uint32_t                timeout = eexWaitMax;

    eexThreadEntry();

    for (;;) {

        /* unblock on any signal or timeout when next timer thread ready to run */
        eexPendSignal(&rtn_status, &rtn_val, timeout, 0xffffffff, sig_timer);
    
        EEX_PROFILE_ENTER;
    
        /*
         * If there are timers to be added (g_add_timer_list != NULL)
         * sever them from the add_timer list and insert them in the active list.
         */
        if (g_add_timer_list_head) {
            do {
                timer = g_add_timer_list_head;
            } while(eexCPUAtomicCAS(&g_add_timer_list_head, add_timer, NULL));

            while (timer) {
                // severed timer list and active_timer list are now thread safe
                // only this thread can access them

                // ensure validity of parameters
                if (timer->interval  > eexWaitMax) { timer->interval  = eexWaitMax; }
                if (timer->remaining > eexWaitMax) { timer->remaining = eexWaitMax; }

                // insert timer in active list
                timer->next = g_active_timer_list_head;
                g_active_timer_list_head = timer;

                // set active status bit
                _atomicBitSet(&timer->control, _timerStatusActive);

                timer = timer->next;
            }
        }

        /*
         * Traverse the active list
         */

        uint32_t now = eexKernelTime(NULL);

        timer = g_active_timer_list_head;
        while (timer) {

            /*
             * If remove bit set
             *    find timer in active list
             *    remove timer from active list
             *    clear timer control and status bits
             */
            if (timer->control & (1 << (_timerCtlRemove - 1))) {
                dummy_cb.next = g_active_timer_list_head;
                active = &dummy_cb;
                while ((active) && (active->next != timer)) { active = active->next; }
                if (active) {
                    active->next = timer->next;
                    timer = active;   // removed timer may cease to exist after control is cleared
                    timer->control = 0;
                }
            }

            /*
             *    If timer running and expired
             *        call timer function
             *        if periodic
             *          set timer->expiry = timer->interval - adusted for clock timer and previous period overrun
             *        else
             *            Atomically clear running bit
             */
            int32_t time_to_expiry = eexTimeDiff(timer->expiry, eexKernelTime(NULL);  //  0 or negative if timer expired
            if ((timer->control & eexTimerRunning) && (time_to_expiry <= 0)) {
                timer->fn_timer(timer->arg);    // call application timer function
                if (timer->interval) {
                    // next periodic interval adjusted for any overrun in previous period
                    timer->expiry = timer->interval + eexKernelTime(NULL) + (uint32_t) time_to_expiry;
                }
                else {  // timer was one-shot, stop timer
                    timer->expiry = 0;
                    _atomicBitClr(&timer->control, _timerStatusRunning);
                }

            /*
             *    If start bit set
             *        set timer->expiry = timer->remaining
             *        Atomically set running bit and clear start bit
             */
            if (timer->control & (1 << (_timerCtlStart - 1))) {
                timer->expiry = timer->remaining + eexKernelTime(NULL);
                _atomicBitSet(&timer->control, _timerStatusRunning);
                _atomicBitClr(&timer->control, _timerCtlStart);
            }

            /*
             *    If stop bit set
             *        timer->remaining = ms until expiry
             *        set timer->expiry = 0
             *        Atomically clear running and stop bits
             */
            if (timer->control & (1 << (_timerCtlStop - 1))) {
                remaining = eexTimeDiff(timer->expiry, eexKernelTime(NULL));  // convert clock time to timeout delay
                if (remaining < 0) { remaining = 0; }
                timer->remaining = remaining;
                timer->expiry = 0;
                _atomicBitClr(&timer->control, _timerStatusRunning);
                _atomicBitClr(&timer->control, _timerCtlStop);
            }

            /*
             * Find next-to-expire timer and set thread timeout to that value.
             * timeout set to eexWaitMax upon entry to thread
             */
            remaining = eexTimeDiff(now, timer->expiry);
            if (remaining < 0) { remaining = 0; }
            if ((uint32_t) remaining < timeout) { timeout = (uint32_t) remaining; }


            timer = timer->next;
        }

        EEX_PROFILE_EXIT;

    }



}








































