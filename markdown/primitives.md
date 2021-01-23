
Primitives {#primitives}
=========


### Overview ###

Kernel primitives are objects supporting the RTOS. These primitives are:

* Queue
* Fifo
* Semaphore

Queues and Fifos are both first-in-first-out containers. Fifos are more
storage efficient but are limited to SPSC. Queues require more storage
but support MPMC.


#### Queue Object ####

A queue is a fixed-sized structure organized as a singly linked list. Each
queue element is a type (void *), so it may be cast to either a pointer of
some type or a 32 bit value. The queue is lockless and supports
Multiple-Producer/Multiple-Consumer (MPMC). The queue algorithm is based on Michael & Scott:
http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf

A queue is staticly instantiated at compile-time with the macro NEW_SLX_PRIM_QUEUE(name, size).
The size of the queue is limited to 2^16 - 3. Total storage allocated for the
queue is 8 bytes x (size + 3), plus 32 bytes of overhead.

#### Fifo Object ####

A fifo is an array of fixed sized objects, either 1, 2, or 4 bytes in size.
The size and type of the fifo are defined at instantiation.
Fifos are lockless but support only Single-Producer/Single-Consumer (SPSC).

A fifo is staticly instantiated at compile-time with the macro NEW_SLX_PRIM_FIFO(name, type, size).
The size of the fifo is limited to 2^16. Total storage allocated for the
fifo is size x sizeof(type), plus 24 bytes of overhead.


