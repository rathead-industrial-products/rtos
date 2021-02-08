
eex RTOS API
============

## Standardized Timeout Values

    eexWaitNoTimeout          = 0,          // no timeout, return immediately
    eexWaitMax                = 0x7fffffff, // maximum timeout value
    eexWaitForever            = 0xffffffff  // wait forever



## Function Return Codes (rtn\_status)

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


## Thread Control
## Thread Function Prototype 
    typedef void (*eex_thread_fn_t) (void *argument);

## Create Thread
Create a thread and add it to the list of active Threads.  
  
    eex_status_t  eexThreadCreate(eex_thread_fn_t fn_thread, void *argument, uint32_t priority, const char *name);
  
        fn_thread  thread function.
        argument   passed to the thread function, generally a pointer to thread-local storage
        priority   unique thread priority
        name       thread name, may be NULL
        return     status code that indicates the execution status of the function.


## Kernel Control
Start the RTOS Kernel scheduler. This function never returns.  
  
  
    void eexKernelStart(void);

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
void eexThreadEntry(void);                // must be the first statment in every thread.

void  eexPend(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, void *kobj);
void  eexPost(eex_status_t *p_rtn_status, uint32_t timeout_ms, uint32_t val,     void *kobj);

void  eexPendSignal(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t signal_mask, void *kobj);
void  eexPostSignal(eex_status_t *p_rtn_status, uint32_t  signal,    void *kobj);

void  eexDelay(uint32_t delay_ms);        // max delay is eexWaitMax
void  eexDelayUntil(uint32_t kernel_ms);  // max kernel_ms is eexWaitMax from current time. rollover is allowed.


// Static allocators for synchronization primitives. More completely defined in implementation section below.
// name must not be in quotes. i.e. EEX_MUTEX_NEW(myMutex) not EEX_MUTEX_NEW("myMutex")
#define EEX_SEMAPHORE_NEW(name, maxval, ival)
#define EEX_MUTEX_NEW(name)
#define EEX_SIGNAL_NEW(name)

### Overview ###

If you are familiar with RTOS basics and want to jump right in, start here.

### Restrictions ###

eex runs on ARM M0/M0+/M3/M4 processors.

Start with a simple blinky-type application that already compiles, loads, and runs
when compiled with the GNU C compiler (-std=gnu99). eex relies on GCC compiler
extentions, specifically local labels and computed goto. Once you have things up and
running then you can go wild with C++ and other compiler options.

### File Structure ###

Include eex_os.h. This is the public interface.

The following c code (c++ friendly) must be compiled and linked in:  
* eex_kernel.c - kernel code  
* eex_arm.c - processor specific code

### Idle Hook ###

The eexIdleHook() function is called when there are no threads ready to dispatch. This
is a function, not a thread, and it must return. The idle function is normally used to put the processor to sleep, and returns an integer representing the number of milliseconds the processor was asleep, which is added to the system timer. eexIdleHook() may pend and post, but must not block. The default function just returns to the scheduler and gets called again repeatedly until there is a thread ready.

The idle function must wake from sleep and return immediatedly after an interrupt in case a thread was readied as a result of the interrupt.

To override the default implementation implement a function of the form:
```
uint32_t eexIdleHook(uint32_t sleep_for_ms) {
    /* sleep for no longer than sleep_for_ms */
    /* return 0 immediately if sleep_for_ms is negative */
    return (/* actual ms slept, or 0 if CPU clock has not been stopped */);
}
```

### Dos and Don'ts ###

DO:  
* Assign each thread a unique priority from 1 (lowest) to 32 (highest) inclusive.  
* Make all local thread variables static or create a thread-local-storage struct to hold thread variables. Functions may use auto variables.  
* To profile interrupt handlers using Segger Sysview call EEX_PROFILE_ENTER and EEX_PROFILE_EXIT
on the handler entry and exit. Threads are automatically profiled.

DO NOT:  
* Call a blocking operation (PEND or POST) from a function, eexIdleHook(), or an interrupt handler.
PEND and POST may be called if they will not block (e.g. timeout = 0).
* Concern yourself with properly sizing thread stacks. You're welcome.

### Tips and Tricks ###

Call eexDelay(0) as a pseudo Yield() to cause the scheduler to run. It will just dispatch the same 
thread again but it may be useful for debugging or profiling.


### Examples ###
