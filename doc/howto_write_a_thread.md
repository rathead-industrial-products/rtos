
How to Write A Thread {#mainpage}
============


### Overview ###

Writing a thread for eex is similar to most other RTOS's. A thread
is not allowed to terminate, so it must be enclosed in a for(;;) or
while(1) loop.

Threads can only block in the main thread. Functions/subroutines/methods
may not block or disaster will ensue.

### Variables ###

A thread cannot have any auto variables that need to survive through a
blocking call. In practice, it is better to have no auto variables at
all in order to avoid potential problems.

Functions may use auto variables since they are be definition not
allowed to block.

Thread variables can either be declared static or can be passed to the
thread via the thread-local-storage pointer parameter that is passed
to the thread when it is invoked.

Static variables are simple and easy to find and are the most similar
to conventional auto variables. Variables declared in a TLS structure
have to be dereferenced, but that is the only way to have one thread
that is invoked multiple times.

It may seem inefficent to declare thread variables static (either in
the thread or the TLS structure) but a conventional RTOS requires a
stack-per-thread which must be large enough to accomodate all of the
auto variables, so there really is no difference in the amound of
memory allocated.

### Thread Entry ###

The first statement in a thread must be the macro eexStartThread(). This
may be placed after any declarations. Declarations with an assignment
are allowed as long as they are static (which all declarations should be).
The eexStartThread() macro contains the code that jumps to the location
after the last call that blocked the thread.


### Profiling ###

...

### Example ###

#### Static Variables ####

```
void thread (void * const tls) {       // may be declared static if in same file as eexThreadCreate()
	static int times = 4;          // static qualifer required, may be initialized

	eexStartThread();
	
	/* One-time initialization, if any, goes here */
		
	for (;;) {                     // threads are always endless loops
		ledFlash(times);       // some function to control external hardware
		eexDelay(1000);        // wait one second
		times = times / 2;
		ledFlash(times);
		eexDelay(1000);
	}
}
```
#### Thread Local Storage Variables ####

```
struct my_thread_variables_t {
	int times;
} my_tls;

static void thread (void * const tls) {  // static not necessary, but probably desired

	eexStartThread();
		
	for (;;) {  
		tls->times = 4;
		ledFlash(times);     // some function to control external hardware
		eexDelay(1000);	     // wait one second
		tls->times = tls->times / 2;
		ledFlash(times);
		eexDelay(1000);
	}
}
```




