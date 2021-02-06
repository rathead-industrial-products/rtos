/*******************************************************************************

    Kernel Primitives.

    COPYRIGHT NOTICE: (c) 2016 DDPA LLC
    All Rights Reserved

    MPMC Lockless queue is derived from Michael & Scott
    http://www.cs.rochester.edu/~scott/papers/1996_PODC_queues.pdf

 ******************************************************************************/

#include  <stdbool.h>
#include  <stdint.h>
#include  "slx_bitmap.h"
#include  "slx_cpu.h"
#include  "slx_primitives.h"
#ifdef SLX_TEST_CONCURRENT
    #include  "slx_primitives_test_conc.h"
#endif


#define SLX_PRIM_BAD_DATA   (('B'<<24) | ('A'<<16) | ('D'<<8) + ('D'))
#define SLX_PRIM_Q_EOL      0


// ==== Queue Functions ====

/// Return a unique (rarely repeating) tag to be used in a tagged index structure.
/// note: It is unlikely, yet possible, to have an undetected tag collision.
uint16_t  _slxQNewTag(void) {
    static uint16_t tag = 0;
    ++tag;
    if (tag == 0) { tag = 1; }
    return (tag);
}

void _slxQInit(slx_queue_t *queue) {
    for (uint16_t i=1; i<queue->size+3; ++i) {    // index [0] is unused
        queue->elem[i].data = SLX_PRIM_BAD_DATA;
        queue->elem[i].next.idx = i+1;            // index to next node in list
    }
    /*
     * data.head/tail initialized to point to a dummy node at location 1.
     * avail.head initialized to point to a dummy node at location 2.
     * avail.tail points to the end of the avail linked list at location [size + 2].
     *
     */
    queue->data.head.tag  = _slxQNewTag();
    queue->data.tail.tag  = _slxQNewTag();
    queue->avail.head.tag = _slxQNewTag();
    queue->avail.tail.tag = _slxQNewTag();

    queue->data.head.idx = 1;
    queue->data.tail.idx = 1;
    queue->elem[1].next.tag = _slxQNewTag();    // dummy nodes need a tag
    queue->elem[1].next.idx = SLX_PRIM_Q_EOL;   // linked list terminator

    queue->avail.head.idx = 2;
    queue->elem[2].next.tag = _slxQNewTag();
    queue->avail.tail.idx = queue->size+2;
    queue->elem[queue->size+2].next.tag = _slxQNewTag();
    queue->elem[queue->size+2].next.idx = SLX_PRIM_Q_EOL;
}

/// Return an index to the head node of the list and sever it from the linked list.
/// Return zero if the list is empty.
uint16_t  _slxQDequeue(slx_queue_t * const queue, slx_list_pointer_t * const lptr, uint32_t *p_node_val) {
    slx_tagged_index_t  h, t, dummy;
    #ifdef SLX_TEST_CONCURRENT
    uint32_t irq_sim_switch=0;
    #endif

    for (;;) {
        h = lptr->head;
        t = lptr->tail;
        dummy = queue->elem[h.idx].next;          // head always points to dummy node
        if (h.idx == t.idx) {                     // list empty or tail falling behind
            if (dummy.idx == SLX_PRIM_Q_EOL) {
                return (0);                       // at dummy node, list empty
            }
            dummy.tag = _slxQNewTag();            // try to advance tail, loop will retry if it fails
            (void) slxCPUAtomicCAS(&(lptr->tail.ti), t.ti, dummy.ti);
        }
        else {                                    // get data from node after dummy, sever dummy from list
            *p_node_val = queue->elem[dummy.idx].data;
            dummy.tag = _slxQNewTag();
            if (0 == slxCPUAtomicCAS(&(lptr->head.ti), h.ti, dummy.ti)) {
                break;
            }
        }
    }
    return (h.idx);
}

/// Append an node to the tail of the linked list.
void  _slxQEnqueue(slx_queue_t * const queue, slx_list_pointer_t * const lptr, uint16_t const addme_idx) {
    slx_tagged_index_t  t, last, addme;
    #ifdef SLX_TEST_CONCURRENT
    uint32_t irq_sim_switch=0;
    #endif


    addme.tag = _slxQNewTag();
    addme.idx = addme_idx;

    for (;;) {
        t = lptr->tail;
        last = queue->elem[t.idx].next;
        if (last.idx == SLX_PRIM_Q_EOL) {
            /* Add node to end of list and try to point tail to the inserted node. */
            if (0 == slxCPUAtomicCAS(&(queue->elem[t.idx].next.ti), last.ti, addme.ti)) {
                addme.tag = _slxQNewTag();
                (void) slxCPUAtomicCAS(&(lptr->tail.ti), t.ti, addme.ti);
                break;
            }
        }
        else {  // traverse the list atomically, loop will retry if it fails
            last.tag = _slxQNewTag();
            (void) slxCPUAtomicCAS(&(lptr->tail.ti), t.ti, last.ti);
        }
    }
}

bool  slxQGet(slx_queue_t *queue, uint32_t *p_val) {
    uint16_t  mybuf_idx;
    uint32_t  mybuf_data;

    /* A brand new queue must be initialized. */
    if (queue->avail.head.ti == 0) {
        _slxQInit(queue);
    }

    /* Get data from the queue and sever the first (dummy) node from the data list. */
    mybuf_idx = _slxQDequeue(queue, &(queue->data), &mybuf_data);
    if (mybuf_idx == 0) { return (false); }   // no data nodes, queue is empty

    /*
     * Read and invalidate the data. Set the index to indicate this node will
     * be the end of the available list.
     */
    *p_val = mybuf_data;
    queue->elem[mybuf_idx].data = SLX_PRIM_BAD_DATA;
    queue->elem[mybuf_idx].next.tag = _slxQNewTag();
    queue->elem[mybuf_idx].next.idx = SLX_PRIM_Q_EOL;

    /* Append empty data buffer to end of linked list of available nodes. */
    _slxQEnqueue(queue, &(queue->avail), mybuf_idx);

    return (true);
}

bool  slxQPut(slx_queue_t *queue, uint32_t val) {
    uint16_t  mybuf_idx;
    uint32_t  ignore;

    /* A brand new queue must be initialized. */
    if (queue->avail.head.ti == 0) {
        _slxQInit(queue);
    }

    /* Get an index to a node and sever it from the linked list of available nodes. */
    mybuf_idx = _slxQDequeue(queue, &(queue->avail), &ignore);
    if (mybuf_idx == 0) { return (false); }   // no available nodes, queue is full

    /*
     * Prepare queue data. Set the index to indicate this node will
     * be the end of the data list.
     */
    queue->elem[mybuf_idx].data = val;
    queue->elem[mybuf_idx].next.tag = _slxQNewTag();
    queue->elem[mybuf_idx].next.idx = SLX_PRIM_Q_EOL;

    /* Append data buffer to end of linked list of data nodes */
    _slxQEnqueue(queue, &(queue->data), mybuf_idx);

    return (true);
}

bool  slxFifoPut(slx_fifo_t *fifo, uint32_t val, size_t sizeof_val) {

    if (fifo->entries >= fifo->size) { return (false); }

    switch (sizeof_val) {
        case sizeof(char):
            ((char *) fifo->array)[fifo->head] = (char) val;
            break;

        case sizeof(uint16_t):
            ((uint16_t *) fifo->array)[fifo->head] = (uint16_t) val;
            break;

        case sizeof(uint32_t):
            ((uint32_t *) fifo->array)[fifo->head] = (uint32_t) val;
            break;

        default:
            assert(false);
            break;
    }

    fifo->head += sizeof_val;
    if (fifo->head >= (fifo->size * sizeof_val)) { fifo->head = 0; }    // wrap index

    /* Fifo is SPSC, so only fifo->entries is shared between processes. () prevent the test_conc macro substitution. */
    while ((slxCPUAtomicCAS)(&(fifo->size_entries), fifo->size_entries, fifo->size_entries + 1));

    return (true);
}

bool  slxFifoGet(slx_fifo_t *fifo, void *val, size_t sizeof_val) {

    if (fifo->entries == 0) { return (false); }

    switch (sizeof_val) {
        case sizeof(char):
            *((char *) val) = ((char *) fifo->array)[fifo->tail];
            break;

        case sizeof(uint16_t):
            *((uint16_t *) val) = ((uint16_t *) fifo->array)[fifo->tail];
            break;

        case sizeof(uint32_t):
            *((uint32_t *) val) = ((uint32_t *) fifo->array)[fifo->tail];
            break;

        default:
            assert(false);
            break;
    }

    fifo->tail += sizeof_val;
    if (fifo->tail >= (fifo->size * sizeof_val)) { fifo->tail = 0; }    // wrap index

    /* Fifo is SPSC, so only fifo->entries is shared between processes. () prevent the test_conc macro substitution. */
    while ((slxCPUAtomicCAS)(&(fifo->size_entries), fifo->size_entries, fifo->size_entries - 1));

    return (true);
}


bool  eexMPSCFifoPut(eex_sell_node_t **head, eex_sell_node_t *node) {
    do {
        node->next = *head;
    } while ((slxCPUAtomicCAS)(head, *head, n));
}


bool  eexMPSCFifoGet(eex_sell_node_t *head, eex_sell_node_t *node) {

}


