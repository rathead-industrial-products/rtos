
This is the repository for the eex RTOS.

eex -- embedded executive

eex is targeted at Arm CortexM embedded systems. Small-ish and resource constrained.

To use eex you should be familiar with basic RTOS concepts. They are all fundamentally
the same with different design compromises.

What is different about eex?  
* Stackless - There is only one system stack shared by all threads and interrupts.
* Non-stop - On the M3/M4 interrupts are never disabled.
* Easy configuration - Set some macros to tailor eex to your needs.
* Easy to manage and understand - eex builds using only three (3) files.

What design compromises are made? 
* Must be built using GCC (requires local labels and computed goto).
* Threads can only block in the main routine, not in functions (subroutines).
* Priority inversion has a more complex solution than just hoisting the priority of the low-priority thread.


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
lockless protocol. Task scheduling that would
normally be done in a locked critical section is performed in the PendSV
exception handler. PendSV is set to be the lowest priority exception and so although it
can be interrupted, by design it always runs to completion and so does not
have to be locked.


### Platform Targets ###

eex is targeted at ARM Cortex-M processors. It has been specifically designed
to work on CM0, CM0+, CM3, and CM4 processors. It has been tested on CM0+ and
CM4 processors. It may work on other ARM variants.

Porting to other (non-ARM) architectures would require finding a substitute for
pendSV and rewriting the Compare-And-Swap operation for the specific processor
architecture.

eex will also build and run on a POSIX console.


