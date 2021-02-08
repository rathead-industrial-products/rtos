
eex RTOS API
============

## Standardized Timeout Values

    eexWaitNoTimeout          = 0,          // no timeout, return immediately
    eexWaitMax                = 0x7fffffff, // maximum timeout value
    eexWaitForever            = 0xffffffff  // wait forever



## Function Return Codes

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

## Thread Entry  
** Function-like macro **  
Must be the first statment in every thread.
  
    void eexThreadEntry(void);

## Create Thread
Create a thread and add it to the list of active threads.  
  
    eex_status_t  eexThreadCreate(eex_thread_fn_t fn_thread, void *argument, uint32_t priority, const char *name); 
        fn_thread  thread function.
        argument   passed to the thread function, generally a pointer to thread-local storage
        priority   unique thread priority
        name       thread name, may be NULL
        return     status code that indicates the execution status of the function.


## Get Thread ID  
Return the thread ID of the current running thread. The thread ID is also the thread priority.  
  
    uint32_t      eexThreadID(void);




## Kernel Control
## Start Scheduler
Start the RTOS Kernel scheduler. This function never returns.  
  
    void eexKernelStart(void);

## Get Current Time
Time since the kernel was started. Milliseconds may be one less than actual if called from an interrupt handler.  
  
    uint32_t      eexKernelTime(uint32_t *us);
        us         set to microseconds since the last millisecond tick
        return     milliseconds since kernel was started




## Idle Hook
Called when there are no ready threads to dispatch.  
  
    uint32_t      eexIdleHook(int32_t sleep_for_ms);
        sleep_for_ms    milliseconds until next thread timeout
                        or negative if a thread has already timed out
                        or 0 if there are no timeouts pending
        return          milliseconds spent in function if it stops the CPU clock (Systick source), return 0 otherwise


## Synchronization Operations
Semaphores, Mutexes, and memory objects (queues, etc.) have a common interface to fetch (Pend) and write (Post).  

    void  eexPend(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, void *kobj);
    void  eexPost(eex_status_t *p_rtn_status, uint32_t timeout, uint32_t val,     void *kobj);
        p_rtn_status    pointer to the return status code (Function Return Codes)
        p_rtn_val       pointer to the value returned by the Pend operation
        timeout         timeout value in ms (or one of the Standardized Timeout Values)
        val             value to be written in the Post operation
        kobj            pointer to the synchronization objects

## Signaling Operations
Read (Pend) and Write (Post) Signals. 

    void  eexPendSignal(eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t signal_mask, void *kobj);
    void  eexPostSignal(eex_status_t *p_rtn_status, uint32_t  signal,    void *kobj);
        p_rtn_status    pointer to the return status code (Function Return Codes)
        p_rtn_val       pointer to the value returned by the Pend operation
        timeout         timeout value in ms (or one of the Standardized Timeout Values)
        signal          signal to be OR'd with destination (Post)
        signal_mask     AND'd with the retrieved signal to mask out unwanted bits (Pend)
        kobj            pointer to the signal object

## Delay
Block for a period of time.  
  
    void  eexDelay(uint32_t delay_ms);
        delay_ms    block for delay_ms milliseconds
    
    void  eexDelayUntil(uint32_t kernel_ms);
        kernel_ms   block until kernel time equals eexKernelTime().

## Synchronization Object Allocation  
** Macro **  
Static allocators for synchronization objects.  
'name' must not be in quotes (i.e. `EEX_MUTEX_NEW(myMutex)`, not `EEX_MUTEX_NEW("myMutex")`)
  
    EEX_SEMAPHORE_NEW(name, maxval, ival)
    EEX_MUTEX_NEW(name)
    EEX_SIGNAL_NEW(name)



