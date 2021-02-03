
Quick Reference Guide {#mainpage}
============


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

To override the default implementation implement a function of the form:
```
uint32_t eexIdleHook(uint32_t sleep_for_ms) {
    /* sleep for no longer than sleep_for_ms */
    return (/* actual ms slept */);
}
```

### Dos and Don'ts ###

DO:  
* Assign each thread a unique priority from 1 to 32 inclusive.  
* Make all local thread variables static. Functions may use auto variables.  
* To profile interrupt handlers using Segger Sysview call EEX_PROFILE_ENTER and EEX_PROFILE_EXIT
on the handler entry and exit. Threads are automatically profiled.

DO NOT:  
* Call a blocking operation (PEND or POST) from a subroutine, eexIdleHook(), or an interrupt handler.
PEND and POST may be called if they will not block (e.g. timeout = 0).

### Tips and Tricks ###

Call eexDelay(0) to cause the scheduler to run as a pseudo Yield().
It will just dispatch the same thread again but it may be useful for debugging or profiling.


### Examples ###
