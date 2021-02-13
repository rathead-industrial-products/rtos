/*******************************************************************************

    eex_timer.h - Real time executive Timer.

    COPYRIGHT NOTICE: (c) ee-quipment.com
    All Rights Reserved

 ******************************************************************************/


#ifndef _eex_timer_H_
#define _eex_timer_H_

#include <stdint.h>
#include "eex_os.h"


// Static allocator for timer.
// 'name' must not be in quotes. i.e. EEX_TIMER_NEW(myTimer) not EEX_TIMER_NEW("myTimer")
#define EEX_TIMER_NEW(name, fn_timer, argument, interval)



typedef void (*eex_timer_fn_t) (void * const argument);

typedef struct eex_timer_cb_t {
    eex_timer_fn_t          fn_timer;       // start address of timer function
    void                        *arg;       // timer function argument
    const char                 *name;       // timer name
    uint32_t                 control;       // timer control and status bit field
    uint32_t                interval;       // ms between periodic calls to fn_timer
    uint32_t               remaining;       // ms remaining to expiry when timer stopped
    uint32_t                  expiry;       // kernel time when timer expires
    eex_timer_cb_t       *next_timer;       // next timer in linked list
} eex_timer_cb_t;


typedef enum  {
    eexTimerActive  = 0x00000001,           // timer has been added to list of active timers
    eexTimerRunning = 0x00000002,           // timer is running
} eex_timer_status_t;


/*
 * On start, timer will fire the first timer after delay, then periodically at interval.
 * If timer->interval = 0, timer is a one-shot. A one-shot timer is still active
 * and may be rerun by calling eexTimerStart() again.
 *
 * To reset a timer, (e.g. a watchdog) call eexTimerStart() on an already running
 * timer. It will be restarted using the new delay value.
 */


void                eexTimerAdd(eex_timer_cb_t * timer);
void                eexTimerRemove(eex_timer_cb_t * timer);
void                eexTimerStart(eex_timer_cb_t*  timer, uint32_t delay);
void                eexTimerStop(eex_timer_cb_t * timer);
void                eexTimerResume(eex_timer_cb_y * timer);
eex_timer_status_t  eexTimerStatus(eex_timer_cb_t * timer);





#undef  EEX_TIMER_NEW
#define EEX_TIMER_NEW(name, fn_timer, argument, interval)                                     \
static eex_timer_cb_t name##_storage = { fn_timer, argument, #name, 0, interval, 0, 0, 0 };   \
static eex_timer_cb_t * const name = &name##_storage


#endif  /* _eex_timer_H_ */
