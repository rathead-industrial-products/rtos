FAQ {#faq_page_title}
===

\tableofcontents

# How can SRTOS be stackless?    {#faq_how_srtos_be_stackless}
A conventional RTOS maintains the entire context of a task in a reserved area
of SRAM. When a task is activated this context becomes the CPU stack. Each task
stack holds all of the task's variables, the processor state (when suspended)
as well as sufficient reserved space for nested function calls and interrupts.
Since the total stack space for a task can rarely be estimated precisely, there
will be some extra SRAM allocated to ensure the task stack does not overflow.

srtos has only a single system stack. This stack is the same as is used in a
single-threaded application without an RTOS. The stack holds function auto
variables as well as return state for nested functions and interrupts. Tasks
in srtos hold their private variables in a separate area and use the system
stack for function calls and interrupts. When an srtos task is suspended, the
task returns itself to its initial state where it can be re-invoked when it
is next activated. It does not require the processor context be saved.

# What compromises must be made for stackless operation?   {#faq_stackless_compromises}
There are a couple of ways in which srtos differs from a conventional RTOS.

The first difference is that local variables are defined
in a separate structure and are accessed through the task control block
pointer. This takes no more storage space than declaring auto-variables on
the task stack in a conventional RTOS, but it does require all variables to
be dereferenced through the task control block pointer, which is somewhat
less convenient than referencing a variable directly.

The second difference is that only a task can block. Subroutines may Pend or
Post, but may not block. If it is architecturally convenient to block on a
subroutine, the subroutine can be rewritten as a separate task and activated
by a signal or queue.

# When can I use Auto variables?    {#faq_when_use_Auto}
Auto variables are destroyed whenever the thread blocks. So if you are ** SURE **
that the auto variable won't be used past a potential blocking operation,
then it is safe to use an auto variable. Auto variables are always OK in
functions, since by definition functions cannot block. So for example a recursive
function using auto variables is allowed.

# What are the benefits of a stackless RTOS?    {#faq_stackless_benefits}
The benefits of a stackless RTOS are primarily related to SRAM management. The
most complicated part of configuring a conventional RTOS is sizing the task
stacks. With srtos, rather than having to properly estimate the size of each task stack, only the
system stack needs to be sized, in the same manner used for a single-threaded
application. The stack space for function calls and interrupts as well as
the extra padding that is normally added to the task stacks
is no longer needed, and the run-time checks for stack overflow can be
eliminated.

In general, an srtos application will require much less SRAM than a conventional
RTOS, and with proper placement of the system stack the possibility of an
undetected stack overflow is eliminated.

