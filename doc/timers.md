
Software Timers
===============


### Overview ###

Software timers are implemented as functions called from a timer thread. The
thread prority is set in the user configuration section of eex_os.h with
EEX_CFG_TIMER_THREAD_PRIORITY. The RTOS will create and run the timer thread.


#### API ####

	void	eexTimerAdd(eex_timer_t timer);
	void	eexTimerRemove(eex_timer_id_t timer_id);
	void	eexTimerStart(eex_timer_t timer, uint32_t initial_delay);
	void	eexTimerStop(eex_timer_t timer);
	
	eex_timer_status_t	eexTimerStatus(eex_timer_t timer);	// active, running


Timer functions may be called by any thread, including interrupts.

Calling a timer function queues that operation and signals the timer thread.
The operation itself is performed by the timer thread when it runs.


