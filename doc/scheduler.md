
Scheduler {#scheduler}
=========


### Overview ###

The scheduler dispatches threads as they become ready to run. Scheduling is
configured on a per-thread basis to be either priority based or round-robin.

#### Thread States ####

Threads are in one of two states: Completed or Interrupted. Threads always run to
completion unless hardware interrupted. When a thread blocks, for example when
waiting on a resource (PEND), the thread completion point (i.e. function return) is
hidden in the PEND call, but the thread does indeed complete.

An Interrupted thread is one that has been interrupted and the thread context remains
on the stack. Multiple Interrupted threads will be in priority order on the stack,
with the highest priority thread the closest to the top of the stack.

### Processor Modes ###

The scheduler can be called from both exceptions and threads. The scheduler
executes in the PendSV handler with the processor in exception mode. Control
is passed to an Interrupted thread by a simple return from PendSV, as the
context for the highest priority interrupted thread will be on the top of the
stack. When control is passed to a blocked (completed) thread the processor must 
transition to Thread mode. An exception stack frame is created with the stacked
PC pointing to the target thread, and the stacked LR pointing to a function
which will create a PendSV exception. An exception return is then performed.

#### Entry and Exit ####

The scheduler is entered by pending the PendSV exception. An exception will
tail-chain to the PendSV handler when there are no higher pending exceptions.
When threads block they will call a helper function which will cause a PendSV
exception.

Upon entry to the PendSV handler from a thread, the exception stack frame is
removed from the stack.

Upon exit, if an interrupted thread has been scheduled, its context will be
on top of the stack. If a completed thread has been scheduled, a new exception
frame is created on the stack. All the stacked values are don't-care, except
for the PSR, the PC which points to the target thread along with R0 which is
the thread parameter (a pointer to the TCB), and the LR which points to the
scheduler entry helper function. PendSV returns the processor to Thread mode
through the standard EXC_RETURN process.

#### Priority ####

Each thread has a unique priority. There may be a maximum of 32 threads.
The priority range is 0 to 32 inclusive, with higher numbers indicating
higher priority. Priority 32 is the highest, and 0 is the lowest.
Priority 0 is reserved for the idle thread, which cannot block on a resource
although it can delay. The default priority 0 do-nothing idle thread may
be overridden by a user supplied thread.

#### Event Posting Behavior ####

Posting an event to a kernel object will cause a task pending on that object
to be readied. If the readied task is a higher priority than the running task
the running task will be preempted. This is the normal behavior when events
are posted from both tasks and interrupts.

In tasks, if a posting event has a timeout of zero, then control will return
to the running task even if a higher priority task has been readied. This is
a useful behavior for example synchronizing several tasks. There is no control
over interrupts however. A task posting multiple events with timeout values
of zero may be interrupted and the interrupt handler may post an event that
will cause the scheduler to run, perhaps running one or more tasks that were
being posted by the synchronizing task. Beware!

#### Round Robin ####

A group of threads may be selectively configured to run round-robin even though
they are different priorities. Once a round robin thread has run, it will not
be scheduled again until all other threads in its round robin group have run
regardless of its priority.


### Scheduler ###

The scheduler selects the highest priority thread that is ready to run, or that
has been interrupted.



\dot
digraph scheduler {

    rankdir="LR";
    
    inv1[shape=box, label="", style="invis", width="0", height="0"];
    inv12[shape=box, label="", style="invis", width="0", height="0"];
    inv13[shape=box, label="", style="invis", width="0", height="0"];
    inv2[shape=box, label="", style="invis"];
    inv3[shape=box, label="", style="invis"];
    
    select[shape="box", label="Select\nNext Thread"];
    interrupted[shape="square", orientation="45", margin="0", label="interrupted?"];
    return[shape="box", label="Return"];
    build_sf[shape="box", label="Build Stack Frame"];

    select -> interrupted;
    interrupted -> build_sf [label="no"];
    interrupted:w -> return:w [label="yes"];
    #interrupted:w -> inv1:n [label="yes", constraint=false, arrowhead="none"];
    #inv1:s -> return:w [constraint=false];
    #inv1 -> inv12 -> inv13-> return [style="invis"];
    build_sf -> return
    #build_sf -> inv2;
    return -> inv3;


    {rank="same"; select interrupted return build_sf}
    {rank="same"; inv1 inv12 inv13 build_sf inv2}
    {rank="same"; return inv3}
}
\enddot




### Task Flow ###




\dot
digraph task_flow {

    rankdir="LR";
    ranksep="1.2 equally"
               
    subgraph cluster_sched {
      label=<<b>SCHEDULER</b>>;
      labelloc="top";
      fontname="courier";
      node [shape=record];
      sched [label="<s_ent> running_thread=0 | <pend_sv> \<PendSV\>", fontname="courier"];
    }
     
    subgraph cluster_pendSV {
      label=<<b>PENDSV</b>>;
      labelloc="top";
      fontname="courier";
      node [shape=record];
      pendSV [label="<psv_ent> if from SCHEDULER\n  remove SF | select next thread | \
      If !interrupted\n\ \ \ \ build SF    \n\ \ \ \ LR=SCHEDULER\nPC=Thread | <psv_rtn> return", fontname="courier"];
    }
    
    subgraph cluster_thread {
      label=<<b>TASK</b>>;
      labelloc="top";
      fontname="courier";
      node [shape=record];
      task [label="<entry> entry | <jc> jmp cont | <jc_nxt> | \
      | pend | save cont | <b_rtn> return | <tgt> continuation | \
      | | <i_occ> \<interrupt\noccurs\nhere\> | <i_rtn> | | | <t_rtn> return", fontname="courier";];
    }
    
    subgraph cluster_irq {
      label=<<b>INT HANDLER</b>>;
      labelloc="top";
      fontname="courier";
      node [shape=record];
      interrupt [label="<i_ent> | POST\n\<unblock a task\nset PendSV\> | | <sr> return\n\<tail chain\npendSV\>", fontname="courier"];
    }
    
    
    task:i_occ -> interrupt:i_ent;
    task:jc:e -> task:tgt:e;
    task:t_rtn -> sched:s_ent;
    interrupt:sr:w -> pendSV:psv_ent:w;
    interrupt:sr -> task:i_rtn;
    pendSV:psv_rtn -> task:entry;
    pendSV:psv_rtn -> task:i_rtn;
    sched:pend_sv -> pendSV:psv_ent;
  
  }

\enddot


