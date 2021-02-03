
Introduction
============

First, why another RTOS?  
Every RTOS has different objectives which result in a set of compromises.  
Here are the pain points this RTOS addresses:  
* Complex configuration
* Requiring a dedicated stack for each thread
* Determining (guessing?) the correct stack size that doesn't waste space yet doesn't overflow
* Trying to understand the hundreds of undecipherable files required to build

This RTOS requires almost no configuration, threads do not have individual stacks, and it builds using three files.

Of course there are compromises...  
* Must be built using GCC (requires local labels and computed goto)
* Threads can only block in the main routine, not in functions (subroutines)
* Priority inversion cannot be addressed by hoisting the priority of a preempted thread, and is currently unaddressed


### Overview ###

eex is a multithreaded, preemptive, stackless real-time operating system. Whereas
conventional RTOS's have a system stack plus an individual stack for each
task, eex has only a system stack. The stackless behavior of eex simplifies
static SRAM allocation and analysis, and enables reliable operation using
substantially less SRAM than conventional stack-per-task RTOS's.

eex never disables interrupts on the M3/M4. Shared data structures are modified 
using a lockless protocol based on a processor level Compare-And-Swap (CAS) primitive. 
The M0 doesn't have a CAS instruction so interrupts are disabled for a couple of 
instructions during a simulated CAS operation.

Kernel structures such as queues and semiphores are implemented using a
lockless protocol. More complex operations such as scheduling that would
normally be done in a locked critical section are performed in the PendSV
exception handler. PendSV is set to be the lowest priority exception and so although it
can be interrupted, by design it always runs to completion and so does not
have to be locked.


### Compiler Restrictions ###

The implementation of eex relies on specific compiler behavior.

* C99             [-std=gnu99]
* Local labels
* Computed goto


### Processor Restrictions ###

eex is targeted at ARM Cortex-M processors. It has been specifically designed
to work on CM0, CM0+, CM3, and CM4 processors. It has been tested on CM0+ and
CM4 processors. It may work on other ARM variants.

Porting to other (non-ARM) architectures would require finding a substitue for
pendSV and rewriting the Compare-And-Swap operation for the specific processor
architecture.


### Processor Configuration ###

Cortex-M processors execute in one of two modes, Thread or Handler; one of
two privilege levels, Privileged or Unprivileged; and use one of two stacks,
Main or Process. eex always executes in Privileged mode using the Main stack. 
Threads execute in thread mode, and interrupts (including the scheduler)
operate in handler mode.


