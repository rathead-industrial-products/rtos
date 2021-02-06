/**
 *
 *  eex_primitives.h
 *  Kernel primitives - MPMC Queue (static size), SPSC Fifo (static size), MPSC linked-list Fifo
 *
 *  All kernel primitives are lockless.
 *
 *  The MPMC queue is sized at instantiation. It contains 32 bit objects.
 *  It is lockless, thread safe, and ABA free.
 *
 *  The SPSC queue is sized at instantiation. It can contain objects of
 *  any type, defined at instatiation. It is lockless, thread safe, and ABA free.
 *
 *  The MPSC fifo is a linked list containing 32 bit objects.
 *  It is lockless, thread safe, and ABA free.
 *
 *
 *
 *  COPYRIGHT NOTICE: (c) 2018 DDPA LLC
 *  All Rights Reserved
 *
 */


#ifndef _eex_primitives_H_
#define _eex_primitives_H_

#include  <stdbool.h>
#include  <stddef.h>
#include  <stdint.h>
#include  "eex_bitmap.h"
#include  "eex_configure.h"
#include  "eex_dbc.h"


// ==== Typedefs ====

// Tag + index in an atomic structure (32 bit) to enable lock-free synchronization
typedef volatile union eex_tagged_index_t {
    struct {
        uint16_t            tag;    // unique tag to avoid ABA problem
        uint16_t            idx;    // array index or item count
    };
    uint32_t                 ti;    // tagged index combined in a word
} eex_tagged_index_t;

// Pointers to the ends of the MPMC linked list
typedef volatile struct eex_list_pointer_t {
    eex_tagged_index_t      head;   // first (oldest) data
    eex_tagged_index_t      tail;   // last (newest) data
} eex_list_pointer_t;

// Individual storage node in a lockless queue
typedef volatile struct eex_queue_node_t {
        uint32_t            data;    // queue data or (cast) pointer
        eex_tagged_index_t  next;    // index of next data location
} eex_queue_node_t;

// Individual storage node in a single-ended linked list
typedef volatile struct eex_sell_node_t {
        uint32_t            data;    // queue data or (cast) pointer
        eex_sell_node_t   * next;    // next node
} eex_sell_node_t;

// Lockless MPMC queue containing 32 bit nodes. Size is static and set at instantiation.
typedef volatile struct eex_MPMCqueue_t {
    eex_list_pointer_t       data;   // pointers to list of data
    eex_list_pointer_t      avail;   // pointers to list of empty locations
    uint16_t                 size;   // number of nodes in queue
    eex_queue_node_t        *elem;   // ptr to queue storage array (sized at instantiation)
} eex_MPMCqueue_t;

// Lockless SPSC fifo. Element type is defined at instantiation. Size is static and set at instantiation.
typedef volatile struct eex_SPSCfifo_t {
    uint16_t                 head;   // pointer to next available location
    uint16_t                 tail;   // pointer to next valid data element
    union {
        struct {
            uint16_t      entries;   // number of valid elements in fifo
            uint16_t         size;   // number of elements in fifo
        };
        uint32_t     size_entries;   // compound value to allow CAS of entries (size stays fixed)
    };
    void                   *array;   // ptr to fifo storage array (typed and sized at instantiation)
} eex_SPSCfifo_t;




// ==== Defines ====

// New storage allocator for a MPMC queue. The queue pointer (name) is a constant with static scope.
// Size is limited to <= 2^16 - 3. Number of allocated array nodes is size + 3
// Room for one dummy node in each (data, avail) list plus unused (null) index zero.
#define NEW_eex_PRIM_QUEUE(name, size)                                        \
static eex_queue_node_t     name##_q_node_array[(size)+3];                    \
static eex_MPMCqueue_t      name##_struct = { { { 0, 0 }, { 0, 0 } }, { { 0, 0 }, { 0, 0 } }, size, name##_q_node_array };   \
static eex_MPMCqueue_t * const name = &name##_struct

// New storage allocator for a SPSC fifo. The fifo pointer (name) is a constant with static scope.
// Size is limited to <= 2^16. Type independence relies on macros, so compiler errors may be misleading.
#define NEW_eex_PRIM_FIFO(name, type, size)                                   \
static type               name##_array[(size)];                               \
static eex_SPSCfifo_t     name##_struct = { 0, 0, 0, size, name##_array };    \
static eex_SPSCfifo_t * const name = &name##_struct

// Type independent SPSC fifo put function. type must the same as when the fifo was instantiated.
// Returns FALSE if the fifo is full.
#define eex_FIFO_PUT(fifo, type, val)  ({                                         \
    _Static_assert(sizeof(type) == sizeof(val), "Fifo val is the wrong type.");   \
    eexFifoPut(fifo, (uint32_t) val, sizeof(val)); })

// Type independent SPSC fifo get function. type must the same as when the fifo was instantiated.
// Returns FALSE if the fifo is empty.
#define eex_FIFO_GET(fifo, type, p_val)  ({                                           \
    _Static_assert(sizeof(type) == sizeof(*p_val), "Fifo *p_val is the wrong type."); \
    eexFifoGet(fifo, (void *) p_val, sizeof(*p_val)); })


// ==== Queue Functions ====

// Put val into a MPMC queue. The put operation is lockless and thread-safe.
// Returns FALSE if the queue was full.
bool  eexMPMCQPut(eex_MPMCqueue_t *queue, uint32_t val);

// Get the next value from a MPMC queue. The get operation is lockless and thread-safe.
// Returns FALSE if the queue was empty.
bool  eexMPMCQGet(eex_MPMCqueue_t *queue, uint32_t *p_val);

// Put val into a SPSC fifo. The put operation is lockless and thread-safe.
// Returns FALSE if the queue was full.
// This function is generally called from the eex_FIFO_PUT macro.
bool  eexSPSCFifoPut(eex_SPSCfifo_t *fifo, uint32_t val, size_t sizeof_val);

// Get the next value from a SPSC fifo. The get operation is lockless and thread-safe.
// Returns FALSE if the queue was empty.
// This function is generally called from the eex_FIFO_GET macro.
bool  eexSPSCFifoGet(eex_SPSCfifo_t *fifo, void *val, size_t sizeof_val);

// Insert node into a MPSC fifo. The put operation is lockless and thread-safe.
// Returns FALSE if the queue was full.
bool  eexMPSCFifoPut(eex_sell_node_t *head, eex_sell_node_t *node);

// Get the oldest node from a MPSC fifo. The get operation is lockless and thread-safe.
// Returns FALSE if the queue is empty.
// This function is generally called from the eex_FIFO_GET macro.
bool  eexMPSCFifoGet(eex_sell_node_t *head, eex_sell_node_t *node);






#endif  /* _eex_primitives_H_ */


