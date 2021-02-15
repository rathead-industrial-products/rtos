/*******************************************************************************

    eex_os.h - Real time executive API.

    COPYRIGHT NOTICE: (c) ee-quipment.com
    All Rights Reserved

 ******************************************************************************/

#ifndef _eex_os_H_
#define _eex_os_H_

#include  <stdint.h>
#include  <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


#define EEX_MAJOR_VERSION           0
#define EEX_MINOR_VERSION           0
#define EEX_PATCH_VERSION           0


/*******************************************************************************
 *
 *  eex Configuration Parameters
 *
 *  The user macros must be defined by the application, either by editing
 *  this file or by including a separate .h file. All default macro values
 *  can be overridden, making the separate .h file a clean option.
 *
 */

//#include "eex_user.h"      /* for example, put your macros in this file */

/* Segger Systemview Support - 1 to enable, 0 to disable */
#ifndef EEX_SEGGER_SYSTEMVIEW
#define EEX_SEGGER_SYSTEMVIEW               0
#endif

/* RTOS Configuration */
#ifndef EEX_CFG_THREADS_MAX
#define EEX_CFG_THREADS_MAX                32       // max 32, min 0 (0 will continuously call eexIdleHook())
#endif

#ifndef EEX_CFG_TIMER_THREAD_PRIORITY
#define EEX_CFG_TIMER_THREAD_PRIORITY       0       // Software timer(s) priority 1-32 (0 = no software timers)
#endif

/* System Configuration */
#ifndef __CORTEX_M
#define __CORTEX_M                          0       // Cortex M0
#endif

#ifndef EEX_CFG_CPU_FREQ
#define EEX_CFG_CPU_FREQ                    48000000    // 48 MHz
#endif

/*****************************************************************************/


/*******************************************************************************
 *
 *  Unit test / lint setup
 */

#if (defined UNIT_TEST) || (defined _lint)
    #undef  EEX_SEGGER_SYSTEMVIEW
    #define EEX_SEGGER_SYSTEMVIEW           0
    #undef  EEX_CFG_THREADS_MAX
    #define EEX_CFG_THREADS_MAX            32
    #ifdef UNIT_TEST
        #define STATIC
        #else
        #define STATIC static
    #endif
#endif

/*****************************************************************************/





/*******************************************************************************
 *
 *  eex API
 *
 *  This section is the user interface to the RTOS. Everything below this
 *  section is implementation is not intended to be accessed by the user.
 *
 */

// Standardized timeout values
typedef enum  {
    eexWaitNoTimeout          = 0,          // no timeout, return immediately
    eexWaitMax                = 0x7fffffff, // maximum timeout value
    eexWaitForever            = 0xffffffff  // wait forever
} eex_std_timeout_value_t;

// Status code values returned by eex functions
typedef enum  {
    eexStatusOK                 = 0,          // function completed; no error or event occurred.
    eexStatusTimerListBusy      = 0x0101,     // timer list mutex is held
    eexStatusTimerNotFound      = 0x0102,     // timer not found in list
    eexStatusKOErr              = 0x0201,     // kernel object not available
    eexStatusKOSemMutOverflow   = 0x0202,     // the count on a semaphore or mutex overflowed a 16 bit value
    eexStatusThreadReady        = 0x0401,     // event released a pending thread, concat thread priority = 0x04pp
    eexStatusThreadBlocked      = 0x0801,     // thread pending on event
    eexStatusThreadTimeout      = 0x0802,     // thread timeout occurred.
    eexStatusEventNotReady      = 0x1001,     // resource not ready, thread not queued
    eexStatusBlockErr           = 0x1002,     // interrupt handler and thread 0 cannot block
    eexStatusIRQNotCallable     = 0x2002,     // cannot be called from an interrupt handler
    eexStatusSchedAddErr        = 0x4001,     // scheduler cannot add thread to list
    eexStatusThreadCreateErr    = 0x8001,     // thread cannot be created
    eexStatusThreadPriorityErr  = 0x8002,     // thread cannot be created with the requested priority
    eexStatusSignalNone         = 0x10001,    // no requested signal bit is set
    eexStatusThread0NotCallable = 0x11002,    // cannot be called from thread 0
    eexStatusInvalid            = 0x7FFFFFFF  // force enum into 32 bits
} eex_status_t;

// Entry point of a thread.
typedef void (*eex_thread_fn_t) (void *argument);


// Create a thread and add it to Active Threads.
// fn_thread  thread function.
// argument   passed to the thread function, generally a pointer to thread-local storage
// priority   unique thread priority
// name       thread name, may be NULL
// return     status code that indicates the execution status of the function.
eex_status_t  eexThreadCreate(eex_thread_fn_t fn_thread, void *argument, uint32_t priority, const char *name);

// Start the RTOS Kernel scheduler.
// This function never returns.
void          eexKernelStart(void);

// Time since the kernel was started. Milliseconds may be one less than actual if called from an interrupt handler.
// us         set to microseconds since the last millisecond tick
// return     milliseconds since kernel was started
uint32_t      eexKernelTime(uint32_t *us);

// Return the thread ID of the current running thread. The thread ID is also the thread priority.
uint32_t      eexThreadID(void);

// Called when there are no ready threads to dispatch.
// sleep_for_ms milliseconds until next thread timeout
//              or negative if a thread has already timed out
//              or 0 if there are no timeouts pending
// return       milliseconds spent in function if it stops the CPU clock (Systick source), return 0 otherwise
uint32_t      eexIdleHook(int32_t sleep_for_ms);


// Function-like macros. These are redefined as macros below.
void  eexThreadEntry(void);               // must be the first statment in every thread.

void  eexPend(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, void *kobj);
void  eexPost(eex_status_t *p_rtn_status, uint32_t val,        uint32_t timeout, void *kobj);

void  eexPendSignal(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t signal_mask, void *kobj);
void  eexPostSignal(eex_status_t *p_rtn_status, uint32_t  signal,    void *kobj);

void  eexDelay(uint32_t delay_ms);        // max delay is eexWaitMax
void  eexDelayUntil(uint32_t kernel_ms);  // max kernel_ms is eexWaitMax from current time. rollover is allowed.


// Static allocators for synchronization primitives. More completely defined in implementation section below.
// name must not be in quotes. i.e. EEX_MUTEX_NEW(myMutex) not EEX_MUTEX_NEW("myMutex")
#define EEX_SEMAPHORE_NEW(name, maxval, ival)
#define EEX_MUTEX_NEW(name)
#define EEX_SIGNAL_NEW(name)

/*****************************************************************************/


/*******************************************************************************
 *
 *  Private interface - Internal RTOS calls and macro definitions
 *
 *  This is ** NOT ** part of the user interface
 */


// Thread list
typedef          uint32_t  eex_thread_id_t;
typedef volatile uint32_t  eex_bm_t;
typedef volatile eex_bm_t  eex_thread_list_t;


/*
 * A kernel object is a primitive synchronization type. Semaphore, mutex, queue, etc.
 * An event is a kernel object as seen by a thread, with pointers to input and output
 * parameters, a timeout, and control information that make the event self-contained.
 */


// Event types
typedef uint32_t     eex_kobj_desc_t;       // one of 'NONE', 'DLAY', 'MAIL', 'MESG', 'MUTX', 'POOL', 'SEMA', 'SIGL', 'TIMR'

// Tag + data in a 32 bit atomic structure to enable lock-free synchronization
typedef volatile union {
    struct {
        uint16_t                 tag;       // unique tag to avoid ABA problem
        uint16_t                data;       // array index or item count
    };
    uint32_t                      td;       // tagged data
} eex_tagged_data_t;

typedef volatile struct {
    eex_kobj_desc_t             type;       // one of 'SEMA', 'MUTX', etc...
    eex_thread_list_t           pend;       // threads waiting on a post operation
    eex_thread_list_t           post;       // threads waiting on a pend operation
} eex_kobj_cb_t;

extern eex_kobj_cb_t       delay_kobj;      // delay control block for all threads to share

typedef volatile struct {
    eex_kobj_cb_t                 cb;       // control block
    uint32_t                  signal;       // signal bits
} eex_signal_cb_t;

typedef volatile struct {
    eex_kobj_cb_t                 cb;       // control block
    eex_tagged_data_t          count;       // semaphore or mutex count
    uint16_t                 max_val;       // semaphore maximum count, 1 if mutex
    uint16_t                owner_id;       // thread ID that holds the mutex, 0 if free
} eex_sema_mutex_cb_t;

typedef volatile uint32_t eex_signal_t;

typedef enum { EEX_EVENT_NO_ACTION=0, EEX_EVENT_PEND, EEX_EVENT_POST } eex_event_action_t;

typedef struct eex_thread_event_t {
    uint32_t                 timeout;       // thread event timeout
    eex_status_t               *rslt;       // result code from thread event
    uint32_t                  *p_val;       // pointer to event return value
    uint32_t                     val;       // event input parameter or signal mask
    eex_event_action_t        action;       // specifies pend or post operation to event
    eex_kobj_cb_t              *kobj;       // event object
} eex_thread_event_t;

// Thread Control Block
typedef struct eex_thread_cb_t {
    eex_thread_fn_t        fn_thread;       // start address of thread function
    void                        *arg;       // thread function argument
    const char                 *name;       // thread name
    void                  *resume_pc;       // saved pc for continuation
    eex_thread_event_t         event;       // event thread is waiting on
} eex_thread_cb_t;

typedef enum { EEX_THREAD_READY, EEX_THREAD_WAITING, EEX_THREAD_INTERRUPTED } eex_thread_list_selector_t;


// Function prototypes. These are implemented as functions, not macros
uint32_t          eexInInterrupt();         // returns exception number if in handler mode, or 0 if in thread mode
eex_thread_cb_t * eexScheduler(bool from_interrupt);
void              eexSchedulerPend(void);
bool              eexPendPost(void *func_yield_pt, eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t val, eex_kobj_cb_t *p_kobj, eex_event_action_t action);
eex_thread_id_t   eexThreadTimeout(void);   // Returns the thread ID of the highest priority waiting task to time out
int32_t           eexTimeDiff(uint32_t time, uint32_t ref);
eex_thread_cb_t * eexThreadTCB(eex_thread_id_t tid);
uint32_t          eexCPUAtomic32CAS(uint32_t volatile *addr, uint32_t expected, uint32_t store);
void *            eexCPUAtomicPtrCAS(void * volatile *addr, void * expected, void * store);
uint32_t          eexCPUCLZ(uint32_t x);


/*
 * Application visible macros. These are functions that are declared in the API
 * and are redefined as macros here.
 */
#define eexThreadEntry()                                                      EEX_THREAD_ENTRY()

#define eexPend(p_rtn_status, p_rtn_val, timeout, p_kobj)                     EEX_PEND_POST(p_rtn_status, p_rtn_val, timeout, 0,   p_kobj, EEX_EVENT_PEND)
#define eexPost(p_rtn_status, val,       timeout, p_kobj)                     EEX_PEND_POST(p_rtn_status, 0,         timeout, val, p_kobj, EEX_EVENT_POST)

#define eexPendSignal(p_rtn_status, p_rtn_val, timeout, signal_mask, p_kobj)  EEX_PEND_POST(p_rtn_status, p_rtn_val, timeout, signal_mask, p_kobj, EEX_EVENT_PEND)
#define eexPostSignal(p_rtn_status, signal, p_kobj)                           eexPost(p_rtn_status, signal, 0, p_kobj)

#define eexDelay(delay_ms)                                                    eexPend(0, 0, (delay_ms), (&delay_kobj))
#define eexDelayUntil(kernel_ms)                                              eexDelay((kernel_ms) - eexKernelTime(NULL))



/*
 * EEX_THREAD_ENTRY()()
 * Jump to the last yield point.
 * The yield point is reset to NULL upon jumping back into the thread. This allows the thread
 * to be dispatched and run normally starting from the beginning of the thread even if it
 * exited via a return or by running off the end of the function instead of being blocked.
 *
 * EEX_PEND_POST()
 * Pend or post a value to an event. Block if the scheduler must be run, either from
 * the resource not being available (and with timeout != 0) or if the operation unblocked
 * a waiting higher priority thread.
 */
#define EEX_THREAD_ENTRY()                                                                      \
    do {                                                                                        \
        EEX_PROFILE_ENTER;                                                                      \
        if (eexThreadTCB(eexThreadID())->resume_pc != NULL) {                                   \
            goto *eexThreadTCB(eexThreadID())->resume_pc;                                       \
        }                                                                                       \
    } while(0)

#define EEX_PEND_POST(p_rtn_status, p_rtn_val, timeout, val, p_kobj, action)                    \
    do {                                                                                        \
        __label__ yield_pt;                                                                     \
        if (eexPendPost(&&yield_pt, p_rtn_status, p_rtn_val, timeout, val, (eex_kobj_cb_t *) p_kobj, action)) {  \
            EEX_PROFILE_EXIT;                                                                   \
            return; }                                                                           \
        yield_pt: ;                                                                             \
        if (!eexInInterrupt()) { eexThreadTCB(eexThreadID())->resume_pc = NULL; }               \
    } while(0)


/*
 *  Storage allocators for synchronization primitives.
 *
 *  Undefined first to undo declaration in user API above.
 *
 */

#undef  EEX_SEMAPHORE_NEW
#define EEX_SEMAPHORE_NEW(name, maxval, ival)                                                   \
static eex_sema_mutex_cb_t name##_storage = { { 'SEMA', 0, 0 }, { 0, ival }, maxval, 0};        \
STATIC void * const name = (void *) &name##_storage

#undef  EEX_MUTEX_NEW
#define EEX_MUTEX_NEW(name)                                                                     \
static eex_sema_mutex_cb_t name##_storage = { { 'MUTX', 0, 0 }, { 0, 1 }, 1, 0};                \
STATIC void * const name = (void *) &name##_storage

#undef  EEX_SIGNAL_NEW
#define EEX_SIGNAL_NEW(name)                                                                     \
static eex_signal_cb_t name##_storage = { { 'SIGL', 0, 0 }, 0};                                  \
STATIC void * const name = (void *) &name##_storage


// Interrupt priority levels. The lowest numbers are the highest priority.
#define EEX_CFG_INT_PRI_PENDSV              255     // lowest possible, reserved for pendSV, aliases to 3 in M0 and 7 in M3/M4
#define EEX_CFG_INT_PRI_LOWEST              254     // lowest available, aliases to 2 in M0 and 6 in M3/M4
#define EEX_CFG_INT_PRI_HIGHEST             EEX_CFG_INT_PRI_0
#define EEX_CFG_INT_PRI_0                   0
#define EEX_CFG_INT_PRI_1                   1
#define EEX_CFG_INT_PRI_2                   2
#define EEX_CFG_INT_PRI_3                   3       // reserved for pendSV in M0
#define EEX_CFG_INT_PRI_4                   4       // unavailable in M0
#define EEX_CFG_INT_PRI_5                   5       // unavailable in M0
#define EEX_CFG_INT_PRI_6                   6       // unavailable in M0
#define EEX_CFG_INT_PRI_7                   7       // reserved for pendSV in M3/M4


// Profiler
#if (EEX_SEGGER_SYSTEMVIEW == 1)

// implemented in SEGGER_SYSVIEW_Config_eexOS.c
#include "SEGGER_SYSVIEW.h"
void eexProfile(bool enter);
void eexProfileAPICreateThread(eex_thread_id_t tid);
void eexProfilePendPost(eex_thread_id_t tid, uint32_t timeout, uint32_t *p_val, eex_kobj_cb_t *p_kobj, eex_event_action_t pp);

#define EEX_PROFILE_ENTER                                     eexProfile((bool) true)
#define EEX_PROFILE_EXIT                                      eexProfile((bool) false)
#define EEX_PROFILE_SCHED_ENTER(thread_stop, why)             SEGGER_SYSVIEW_OnTaskStopReady((uint32_t) eexThreadTCB(thread_stop), why)
#define EEX_PROFILE_SCHED_EXIT(thread_exec)                   SEGGER_SYSVIEW_OnTaskStartExec((uint32_t) eexThreadTCB(thread_exec))
#define EEX_PROFILE_SCHED_IDLE                                SEGGER_SYSVIEW_OnIdle()
#define EEX_PROFILE_PEND_POST(tid, n_int, to, val, kobj, act) eexProfilePendPost(tid, n_int, to, val, kobj, act)
#define EEX_PROFILE_INIT                                      do { SEGGER_SYSVIEW_Conf(); SEGGER_SYSVIEW_Start(); } while(0)
#define EEX_PROFILE_API_CALL_CREATE_THREAD(tid)               eexProfileAPICreateThread(tid)

#else
#define EEX_PROFILE_ENTER                                     ((void) 0)
#define EEX_PROFILE_EXIT                                      ((void) 0)
#define EEX_PROFILE_SCHED_ENTER(thread_stop, why)             ((void) 0)
#define EEX_PROFILE_SCHED_EXIT(thread_exec)                   ((void) 0)
#define EEX_PROFILE_SCHED_IDLE                                ((void) 0)
#define EEX_PROFILE_PEND_POST(tid, n_int, to, val, kobj, act) ((void) 0)
#define EEX_PROFILE_INIT                                      ((void) 0)
#define EEX_PROFILE_API_CALL_CREATE_THREAD(tid)               ((void) 0)
#endif


// Assert macro
#ifdef UNIT_TEST
#include "CException.h"
#define assert(e) ((e) ? (void)0 : (void)(Throw(0)))

#elif defined __CONSOLE__
#include  <assert.h>

#else
void HardFault_Handler(void);
#define assert(e) ((e) ? (void)0 : HardFault_Handler())
#endif


#ifdef __cplusplus
}
#endif

#endif  /* _eex_os_H_ */
