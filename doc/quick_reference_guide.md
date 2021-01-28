
Quick Reference Guide {#mainpage}
============


### Overview ###

If you are familiar with RTOS basics and want to jump right in, start here.

### File Structure ###

You must include eex_os.h. This is the public interface. eex_os.h will pull in:

eex_configure.h	- set user configurable compile-time paramaters
eex_kernel.h	- defines the internal interface needed by the macros in eex_os.h

The following c code (c++ friendly) must be compiled and linked in:
eex_kernel.c	- kernel code
eex_arm.c		- processor specific code

### Restrictions ###

eex relies on GCC compiler extentions, specifically local labels and computed goto.
eex runs on ARM M0/M0+/M3/M4 processors.

### Idle Hook ###

The eexIdleHook() function is called when there are no threads ready to dispatch.
It is normally used to put the processor to sleep, and returns an integer representing
the number of milliseconds the processor was asleep, which is added to the system timer.
eexIdleHook() 0 may pend and post, but cannot block.


### Dos and Don'ts ###

DO:
Assign each thread a unique priority from 1 to 32 inclusive.
Make all local thread variables static.
Call EEX_PROFILE_ENTER and EEX_PROFILE_EXIT on the entry and exit to interrupt handlers to include them in Segger Sysview.
Threads are automatically profiled.


DO NOT:
Call a blocking operation (PEND or POST) from a subroutine.
Call a blocking operation (PEND or POST) from eexIdleHook().


### Tips and Tricks ###

Call eexDelay(0) to cause the scheduler to run as a pseudo Yield().
It will just dispatch the same thread again but it may be useful for debugging or profiling.





### Examples ###
