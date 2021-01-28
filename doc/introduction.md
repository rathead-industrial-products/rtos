
Introduction {#mainpage}
============


### Overview ###

SRX is a multithreaded, preemptive, stackless real-time operating system. Whereas
conventional RTOS's have a system stack plus an individual stack for each
task, SRX has only a system stack. The stackless behavior of SRX simplifies
static SRAM allocation and analysis, and enables reliable operation using
substantially less SRAM than conventional stack-per-task RTOS's.

SRX never disables interrupts. Shared data structures are modified using a
lockless protocol based on a processor level Compare-And-Swap primitive.

Primitive structures such as queues and semiphores are implemented using a
lockless protocol. More complex operations such as scheduling that would
normally be done in a locked critical section are performed in the PendSV
exception handler. This is the lowest priority exception and so although it
can be interrupted, it is not re-entrant. 


### Compiler Restrictions ###

The implementation of srx relies on specific compiler behavior.

* C99             [-std=gnu99]
* Local labels    [insert link here]
* Computed goto   [insert link here]


### Processor Restrictions ###

SRX is targeted at ARM Cortex-M processors. It has been specifically designed
to work on CM0, CM0+, CM3, and CM4 processors. It has been tested on CM0+ and
CM4 processors. It may work on other ARM variants.

Porting to other (non-ARM) architectures would require finding a substitue for
pendSV and rewriting the Compare-And-Swap operation for the specific processor
architecture.


### Processor Configuration ###

Cortex-M processors execute in one of two modes, Thread or Handler; one of
two privilege levels, Privileged or Unprivileged; and use one of two stacks,
Main or Process. srtos always executes in Privileged mode using the Main stack.


