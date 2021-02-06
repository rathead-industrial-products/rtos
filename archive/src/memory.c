/*****************************************************************************

      (c) Copyright 2014 ee-quipment.com
      ALL RIGHTS RESERVED.

    memory.c - Collections that operate on statically allocated blocks of memory
              which will operate efficiently and deterministically in
              SRAM limited embedded systems.

    FIFO: A fifo type that operates on chars.

    OBUF: An ordered fixed size ring buffer that can malloc
          arbitrary-sized chunks with the stipulation that memory is freed
          mostly in the same order it was malloc'd.

    DL:   A type-independent ring buffer (delay line) to provide storage and
          delay suitable for FIR filter implementations.

    POOL: A thread-safe malloc alternative that allocates fixed sized blocks.

 *****************************************************************************/

#include  "bitvector.h"
#include  "dbc.h"
#include  "memory.h"


/*****************************************************************************

   POOL: A thread-safe malloc alternative that allocates fixed sized blocks.

       The total number of blocks is limited to 64.
       The maximum allocation block size is 1024 bytes.
       The number and size of allocation blocks are optimized to each application
           using the #define POOL_PARTITION_xx_BLOCKS macros.

       Definitions:
           POOL - the entire statically allocated memory area, including overhead.
           PARTITION - A subset of the pool containing equally sized blocks in
                       power-of-two increments.
           BLOCK - A single power-of-two sized allocatable section of a partition

        A call to poolMalloc(size) will attempt to allocate the smallest power-of-two
        block available that will hold size bytes. In the pathological case an attempt
        to allocate 1 byte may use a 1024 byte block.

        Bit field organization
         PBMax    blks-1                           1     0
        ----------------------------------------------------
        | unused |  8   | .. | 16 | .. |         | .. |1024|
        ----------------------------------------------------

   Revision History:
    09/09/16  Initial release

 *****************************************************************************/

/*
 * When allocating memory, all partitions smaller than the requested size
 * are masked (as if they were allocated) to allow FF1 to find the first
 * block equal to or larger than the requested size.
 */
#define POOL_PARTITION_MASK_1024    ((uint64_t) (0xffffffffffffffff       << POOL_PARTITION_1024_BLOCKS))
#define POOL_PARTITION_MASK_512     ((uint64_t) (POOL_PARTITION_MASK_1024 << POOL_PARTITION_512_BLOCKS))
#define POOL_PARTITION_MASK_256     ((uint64_t) (POOL_PARTITION_MASK_512  << POOL_PARTITION_256_BLOCKS))
#define POOL_PARTITION_MASK_128     ((uint64_t) (POOL_PARTITION_MASK_256  << POOL_PARTITION_128_BLOCKS))
#define POOL_PARTITION_MASK_64      ((uint64_t) (POOL_PARTITION_MASK_128  << POOL_PARTITION_64_BLOCKS))
#define POOL_PARTITION_MASK_32      ((uint64_t) (POOL_PARTITION_MASK_64   << POOL_PARTITION_32_BLOCKS))
#define POOL_PARTITION_MASK_16      ((uint64_t) (POOL_PARTITION_MASK_32   << POOL_PARTITION_16_BLOCKS))
#define POOL_PARTITION_MASK_8       ((uint64_t) (POOL_PARTITION_MASK_16   << POOL_PARTITION_8_BLOCKS))

const uint64_t  g_partition_mask[POOL_PARTITIONS] = { POOL_PARTITION_MASK_8,   POOL_PARTITION_MASK_16,  POOL_PARTITION_MASK_32,  POOL_PARTITION_MASK_64,
                                                      POOL_PARTITION_MASK_128, POOL_PARTITION_MASK_256, POOL_PARTITION_MASK_512, POOL_PARTITION_MASK_1024 };

const uint8_t   g_pool_blocks[POOL_PARTITIONS] = { POOL_PARTITION_8_BLOCKS,   POOL_PARTITION_16_BLOCKS,  POOL_PARTITION_32_BLOCKS,  POOL_PARTITION_64_BLOCKS,
                                                   POOL_PARTITION_128_BLOCKS, POOL_PARTITION_256_BLOCKS, POOL_PARTITION_512_BLOCKS, POOL_PARTITION_1024_BLOCKS };

#define POOL_HISTORY_ALLOC          0x8000
#define POOL_HISTORY_FREE           0x0000

#define POOL_SIZE   ((1024 * POOL_PARTITION_1024_BLOCKS) + (512 * POOL_PARTITION_512_BLOCKS) + (256 * POOL_PARTITION_256_BLOCKS) + (128 * POOL_PARTITION_128_BLOCKS) +  \
                     (64 * POOL_PARTITION_64_BLOCKS)     + (32 * POOL_PARTITION_32_BLOCKS)   + (16 * POOL_PARTITION_16_BLOCKS)   + (8 * POOL_PARTITION_8_BLOCKS))


NEW_FIFO(g_pool_history, 2 * POOL_HISTORY_DEPTH);   // 2 bytes per history entry
static pool_stats_t    g_pool_stat[POOL_PARTITIONS];
static pool_profile_t  g_pool_profile;
static char            g_pool[POOL_SIZE];           // master allocation of pool storage

NEW_BIT_VECTOR(g_blks_allocated_bv, POOL_BLOCKS);   // 1 if block allocated, 0 if free
NEW_BIT_VECTOR(g_pool_bv, POOL_BLOCKS);             // scratch bit vector


/// \return the size of the partition referenced by index. Return zero if index is invalid.
static size_t poolPartitionAtIndex(const size_t index) {
    if (index > POOL_PARTITIONS) { return (0); }
    else                         { return (1 << (index + 3)); }
}

/// \return the smallest partition that will contain size. Return zero if no partition can contain size.
static size_t poolBestFitPartition(const size_t size) {
    for (int p=0; p<POOL_PARTITIONS; ++p) {             // from 8 partition to 1024
        if (size <= poolPartitionAtIndex((size_t) p)) {
            return (poolPartitionAtIndex((size_t) p));
        }
    }
    return (0);
}

/// \return the array index of the given size partition. Return -1 if partition is invalid.
static size_t poolPartitionIndex(const size_t size) {
    for (int p=POOL_PARTITIONS-1; p>=0; --p) {          // from 1024 partition to 8
        if (size == poolPartitionAtIndex((size_t) p)) {
            return ((size_t) p);
        }
    }
    return (-1);
}

/// \return the partition size of blk. Return zero if blk is invalid.
static size_t poolBlkPartition(int blk) {
    size_t  size = 0;

    if ((blk < 0) || (blk > POOL_BLOCKS)) {
        return (0);
    }

    for (int p=POOL_PARTITIONS-1; p>=0; --p) {        // from 1024 partition to 8
        if (!g_pool_blocks[p]) {                      // no blocks in this partition
            continue;
        }
        if (blk > (g_pool_blocks[p] - 1)) {           // skip over this partition
            blk -= g_pool_blocks[p];
        }
        else {
            size = poolPartitionAtIndex((size_t) p);  // block in this partition, compute partition size
            break;
        }
    }
    return (size);

}

/// \return the address of block blk. return NULL if the block is invalid.
static void * poolBlkAddr(int blk) {
    void *  addr = &g_pool;

    if ((blk < 0) || (blk >= POOL_BLOCKS)) {
        return (NULL);
    }

    for (int p=POOL_PARTITIONS-1; p>=0; --p) {              // from 1024 partition to 8
        if (!g_pool_blocks[p]) {                            // no blocks in this partition
            continue;
        }
        if (blk > (g_pool_blocks[p] - 1)) {                 // increment address over this partition
            addr += g_pool_blocks[p] * (1 << (p+3));
            blk -= g_pool_blocks[p];
        }
        else {
            addr += blk * poolPartitionAtIndex((size_t) p); // block in this partition, point addr to it
            break;
        }
    }
    return (addr);
}

/// \return the block number corresponding to addr. Return -1 if addr is invalid.
static int    poolBlkAtAddr(const void * addr) {
    int     block = 0;
    void *  cmp_addr = &g_pool;

    for (int p=POOL_PARTITIONS-1; p>=0; --p) {      // from 1024 partition to 8
        for (int b=0; b<g_pool_blocks[p]; ++b) {
            if (addr == cmp_addr) {
                return (block);
            }
            cmp_addr += 1 << (p+3);   // partition size
            ++block;
        }
    }
    return (-1);    // couldn't find address
}

void * poolMalloc(size_t size) {
    size_t  best_fit_partition, allocated_partition;
    int     block;
    void *  addr;

    best_fit_partition = poolBestFitPartition(size);

    do {
        // put partition mask into bitvector, OR with allocated blocks, invert sense of allocation bitvector to allow FF1
        g_pool_bv.array[0] = (uint32_t) g_partition_mask[poolPartitionIndex(best_fit_partition)];
        g_pool_bv.array[1] = (uint32_t) (g_partition_mask[poolPartitionIndex(best_fit_partition)] >> 32);

        bvOR(&g_pool_bv, &g_pool_bv, &g_blks_allocated_bv);   // mask partitions that are smaller than size requested
        bvNOT(&g_pool_bv, &g_pool_bv);  // invert so that 1 = free

        block = bvFF1(&g_pool_bv);
        if (block == -1) {              // no blocks available to allocate
           block = POOL_BLOCKS;         // flag with invalid block number
           break;
        }

    } while (bvSet(&g_blks_allocated_bv, block)); // potential block found, try to atomically allocate it

    addr = poolBlkAddr(block);                    // invalid block will return NULL

    #ifdef PROFILE
        if (addr) {
            allocated_partition = poolBlkPartition(block);                          // size of allocated block (may be larger than optimal)
            fifoPush16(g_pool_history, (uint16_t) (POOL_HISTORY_ALLOC | allocated_partition));  // record that a block of size allocated_partition was allocated
            if (best_fit_partition != allocated_partition) {
                g_pool_stat[poolPartitionIndex(best_fit_partition)].cnt_fail += 1;  // failed to allocate optimal sized partition
            }
            g_pool_stat[poolPartitionIndex(allocated_partition)].cnt_alloc += 1;    // record number of times partition was allocated
            g_pool_stat[poolPartitionIndex(allocated_partition)].cur_alloc += 1;    // track blocks currently allocated

            // set high water mark
            g_pool_stat[poolPartitionIndex(allocated_partition)].max_alloc =  MAX(g_pool_stat[poolPartitionIndex(allocated_partition)].max_alloc,
                                                                                  g_pool_stat[poolPartitionIndex(allocated_partition)].cur_alloc);
        }
        else {
            g_pool_stat[poolPartitionIndex(best_fit_partition)].cnt_fail += 1;      // failed to allocate optimal (or any!) sized partition
        }
    #endif

    return (addr);
}

void poolFree(void * addr) {
    size_t   allocated_partition;
    int      block;

    block = poolBlkAtAddr(addr);
    if ((block != -1) && bvClr(&g_blks_allocated_bv, block)) {    // free block if valid
        allocated_partition = poolBlkPartition(block);            // update stats only if freed block was allocated (it is legal to free an already free block)
        fifoPush16(g_pool_history, (uint16_t) (POOL_HISTORY_FREE | allocated_partition));  // record that a block of size allocated_partition was freed
        g_pool_stat[poolPartitionIndex(allocated_partition)].cur_alloc -= 1;                        // track blocks currently allocated
    }
}

pool_profile_t * poolProfile(void) {
    g_pool_profile.pool_stat = g_pool_stat;
    g_pool_profile.pool_history = g_pool_history;
    g_pool_profile.pool_state = (uint64_t *) g_blks_allocated_bv.array;
    return (&g_pool_profile);
}

/*****************************************************************************

   DL: A type-independent fixed sized ring buffer delay line. Thread safe.

        The number of delay line entries is limited to 64K.
        The delay line contents are type independent - parameters must be cast to the proper type.
        The delay line is allocated at compile-time and cannot be resized or dynamically created.
        Each delay line is instantiated as an opaque object of type (void *).

        Usage Example:
        NEW_DELAY_LINE(obj_name, type, num_taps)
        dlUpdate(obj_name, element)
        myelement = *(type *) dlGetTap(obj_name, tap)

        The delay line is a ring buffer with num_taps elements. New elements
        are added at tap zero and the oldest element is at tap (num_taps - 1).
        Elements may be accessed individually with dlGetTap() or as an array
        using dlAsArray().

        Delay line elements are type independent and function parameters
        are passed as pointers of type (void *).

   Revision History:
    02/20/15  Initial release

 *****************************************************************************/

typedef struct  {
    const uint16_t  taps;
    uint16_t        index;        // always points to tap zero
    const size_t    type_size;
    char            element[];
} dl_obj_t;


/*
 *  Add dl_element to the ring buffer. Discard the oldest element. The type
 *  of dl_element is unknown, so treat it as an array of type_size
 *  char characters. index always points to tap zero.
 *
 */
void dlUpdate(void * dl_obj, void * dl_element) {
    dl_obj_t * dl  = (dl_obj_t *) dl_obj;
    char * element = (char *) dl_element;
    int offset;

    LOCK;

    REQUIRE (dl->taps > 0);
    REQUIRE (dl->index < dl->taps);

    dl->index = ((dl->index - 1) + dl->taps) % dl->taps;   // point to new tap zero
    offset = dl->index * dl->type_size;

    for (int i=0; i<dl->type_size; ++i) {
        dl->element[i + offset] = element[i];
    }

    ENSURE (dl->index < dl->taps);

    END_LOCK;
}

/*
 * Allow python style indexing where [-1] is the last entry of the array.
 * Also allow tap to be > number of taps in delay line.
 */
void * dlGetTap(void * dl_obj, int16_t tap) {
    dl_obj_t * dl = (dl_obj_t *) dl_obj;
    int offset;

    LOCK;

    REQUIRE (dl->taps > 0);
    REQUIRE (dl->index < dl->taps);

    tap %= dl->taps;      // abs(tap) < dl->taps
    if (tap < 0) { tap += dl->taps; }

    offset = ((dl->index + tap) % dl->taps) * dl->type_size;

    END_LOCK;

    return ((void *) &dl->element[offset]);
}

/*
 * Return the index currently pointing to tap zero. This allows the caller
 * to 'freeze' the state of the ring buffer without concern about the
 * indices moving from a call to dlUpdate.
 */
uint16_t dlGetIndex(void * dl_obj) {
    dl_obj_t * dl = (dl_obj_t *) dl_obj;

    return (dl->index);
}

/*
 *  Rotate entire element array so that tap zero is at array index zero and
 *  reset the index value.
 */
#define MEM_DL_SHIFT_BUF_SIZ    8    // temp buffer size when shifting delay line
void * dlAsArray(void * dl_obj) {
    dl_obj_t * dl = (dl_obj_t *) dl_obj;
    char shifted_out[MEM_DL_SHIFT_BUF_SIZ];
    int len, offset, shift_len;

    LOCK;

    REQUIRE (dl->taps > 0);
    REQUIRE (dl->index < dl->taps);

    len    = dl->taps * dl->type_size;                        // size of element array
    offset = dl->index * dl->type_size;                       // element[offset] is tap zero element

    while (offset > 0) {                                      // move element[offset] to element[0]
        shift_len = MIN(offset, MEM_DL_SHIFT_BUF_SIZ);        // for efficiency move bytes shift_len positions at a time
        for (int i=0; i<len; ++i) {
            if (i < shift_len) {
                shifted_out[i] = dl->element[i];              // temporarily save leftmost shift_len elements
            }
            if (i < (len - shift_len)) {
                dl->element[i] = dl->element[i + shift_len];  // left shift array
            }
            else {
                dl->element[i] = shifted_out[i - (len - shift_len)];
            }
        }
        offset -= shift_len;
    }
    dl->index = 0;

    END_LOCK;

    return ((void *) dl->element);
}


uint16_t dlTaps(void * dl_obj) {
    dl_obj_t * dl  = (dl_obj_t *) dl_obj;

    return (dl->taps);
}



/*****************************************************************************

   OBUF: A fixed sized ring buffer. Thread safe.

   A pool of uint32_t aligned memory is allocated at compile-time.
   Arbitrarily sized blocks are served up with obufMalloc(). A block
   will always be a linear memory space - it will never wrap from the
   end of the ring buffer to the beginning.

   The pool buffer is not intended to be accessed directly but since
   it needs to be visible at compile time it cannot be encapsulated. The
   pool is instantiated as a variable named '_obuf_obj_poolname' where
   'poolname' is the _obuf_obj_t object name passed to NEW_OBUF.

   The size of the instantiated pool is the requested size rounded up to
   a multiple of 8 bytes plus 12 bytes of overhead.

   Blocks should be freed in the same order that they are malloc'd. Blocks
   may be freed out of order, blocks are not returned to the pool until the
   oldest allocated block is freed.

   If obufFree() is called with a pointer that does not lie
   within the ring buffer the call will be ignored and return with no error.

   The use-case for this collection is to provide temporary storage for I/O
   buffers that are produced and consumed in-order, such as a USB endpoint
   buffer. Allowing pointers outside of the ring buffer to be silently freed
   simplifies handling of mixed variable and constant buffers, for example
   handing a string to an OUT endpoint and then calling obufFree()
   on the completion callback succeeds regardless of whether the string
   was in the ring buffer or non-volatile storage.

   Useage Example:
   NEW_OBUF(ep1_out, 64);
   outstr = (char *) obufMalloc(ep1_out, 12);
   outstr[0] = 'h'; outstr[1] = 'e'; ... outstr[10] = 'd'; outstr[11] = '\0';
   // usbOut(outstr);
   outstr_const = "hello world";
   // usbOut(outstr_const);
   assert (obufFree(ep1_out, outstr) == OBUF_NOERR);
   assert (obufFree(ep1_out, outstr_const) == OBUF_NOERR);  // allowed because outstr_const is outside of ep1_out pool

   Revision History:
    05/01/14  Initial release
    07/01/14  Added obufDataPtrs for diagnostics
    07/02/14  Refactored algorithm and data structure - interface unchanged
    07/11/14  Removed restriction that blocks be freed in the order allocated, added obufMemAvail()
    07/11/14  Added obufMemAvail for diagnostics
    07/12/14  Added max_free, max_frag for diagnostics

 *****************************************************************************/


/*
 * Allocated blocks are maintained with a linked list, with oldest pointing
 * to the least-recent block allocated from the pool. The next pointer for
 * the last block allocated will point to the next available free location.
 *
 * The data block links are allocated in the buffer, immediately in front of
 * the data area that is allocated to the caller. Only the next pointer is
 * additive to the data block.
 *
 * Return NULL if a block of memory cannot be allocated from the pool.
 */
void * obufMalloc(obuf_obj_t pool, uint32_t size) {
    char *        bot          = pool->buf;
    char *        top          = bot + pool->size;
    p_obuf_dblk_t newest;
    p_obuf_dblk_t allocated;
    bool          f_allocate   = FALSE;  // true if sufficient free memory is available
    int32_t       total_size;            // requested size + rounding & overhead
    int           space_above, space_below, space_between;
    bool          f_sabv, f_sblw, f_sbtwn, empty;

    if (size == 0) { return (NULL); }
    REQUIRE (pool != NULL);

    size = (size + 3) & ~3;                     // round up to a 4-byte boundary
    total_size = size + sizeof(p_obuf_dblk_t);  // add space for the next pointer

    LOCK;
    /* find most-recently allocated block */
    newest = pool->oldest;
    for (int i=1; i<pool->n_blks; ++i) {  // newest is also oldest if only one block
        newest = newest->next;
    }

    space_above   = (top - ((char *) newest->next));  // free memory in the interval next free to top (assuming next hasn't wrapped to bottom)
    space_below   = (((char *) pool->oldest) - bot);  // free memory in the interval bot to oldest (assuming next hasn't wrapped, but will)
    space_between = (((char *) pool->oldest) - ((char *) newest->next)); // free memory in the middle interval if next has wrapped to the bottom of the pool

    f_sabv  = space_above >= total_size;   // sufficient free memory above
    f_sblw  = space_below >= total_size;   // sufficient free memory below
    f_sbtwn = space_between >= total_size; // sufficient free memory below in the middle
    empty   = (pool->n_blks == 0);

    if (empty || (pool->oldest < newest->next)) {   // next hasn't wrapped so free memory is at the ends of the pool, oldest == next if empty or possibly if full
        if (f_sabv | f_sblw) {                      // free memory is sufficient to meet request
            f_allocate = TRUE;
            if (!f_sabv) {                          // wrap to bottom of pool
                newest->next = (p_obuf_dblk_t) bot;
            }
        }
        pool->min_free = MIN(pool->min_free, space_above + space_below - total_size);
    }
    else if (f_sbtwn) {                             // free memory is in the middle of the pool
        f_allocate = TRUE;
        pool->min_free = MIN(pool->min_free, space_between - total_size);
    }

    if (f_allocate) {
        allocated = newest->next;
        ++(pool->n_blks);
        allocated->next = (p_obuf_dblk_t) (((char *) allocated) + total_size);
        ENSURE (allocated->next <= (p_obuf_dblk_t) top);  // error if it goes past the top
        if (allocated->next > (p_obuf_dblk_t) (top - sizeof(struct obuf_dblk_t))) {  // wrap if no room for at least a min-size data block at top
            allocated->next = (p_obuf_dblk_t) bot;
        }
    }
    else {
        ++pool->n_failed;
    }
    END_LOCK;
    return (f_allocate ? (void *) &(allocated->data) : NULL);
}


/*
 * Memory blocks are not returned to the pool until the least-recently allocated
 * block is freed. It is allowed to Free a pointer that does not point to
 * somewhere in the pool.
 */
obuf_error_t  obufFree(obuf_obj_t pool, void * ptr) {
    char *        bot   = pool->buf;
    char *        top   = bot + pool->size;
    obuf_error_t  rslt  = OBUF_NOERR;

    REQUIRE (pool != NULL);
    if ((ptr < ((void *) top)) && (ptr >= ((void *) bot))) {
        LOCK;   // points into pool, traverse linked list looking for ptr
        p_obuf_dblk_t target_blk    = pool->oldest;
        p_obuf_dblk_t ptr_to_target = (p_obuf_dblk_t) &(pool->oldest);
        int blk_cnt = pool->n_blks;
        while (blk_cnt) {
            if (ptr == (void *) target_blk->data) {     // valid ptr, free the memory
                ptr_to_target->next = target_blk->next; // free the memory
                --(pool->n_blks);
                if (pool->n_blks == 0) {                // reset to bottom of pool if now empty
                    pool->oldest       = (p_obuf_dblk_t) bot;
                    pool->oldest->next = (p_obuf_dblk_t) bot;
                }

                break;
            }
            ptr_to_target = ptr_to_target->next;
            target_blk = target_blk->next;
            ENSURE(target_blk);
            --blk_cnt;
        }

        if (blk_cnt == 0) {
            rslt = OBUF_INVALID_POINTER;  // empty pool, invalid or messed-up pointer
        }
        END_LOCK;
    }
    return (rslt);
}


/*
 * Return the first n_ptrs data pointers of the pool linked list. The ptr_array
 * list must be sized to accept n_ptrs + 1. The ptr_array will be terminated
 * with a NULL entry after the last valid data pointer.
 */
void  obufDataPtrs(obuf_obj_t pool, void * ptr_array[], uint32_t n_ptrs) {
    p_obuf_dblk_t p_block;

    REQUIRE (pool != NULL);
    REQUIRE (ptr_array != NULL);

    LOCK;
    if (pool->n_blks != 0) {             // pool not empty
        p_block = pool->oldest;
        for (int i=0; i<pool->n_blks; ++i) {
            ptr_array[i] = (void *) &(p_block->data);
            p_block = p_block->next;
        }
    }
    ptr_array[pool->n_blks] = NULL;
    END_LOCK;
}


/*
 * Return the min free memory observed.
 */
void obufMemStats(obuf_obj_t pool, int * min_free, int * failed_allocs) {
    *min_free = pool->min_free;
    *failed_allocs = pool->n_failed;
}




/*****************************************************************************

   FIFO: A fifo type that operates on chars. Thread safe.

        Fifo size is limited to 64K Bytes.
        NEW_FIFO returns a pointer to the Fifo control structure (TFifo).
        Push and Pop are optimized for time for efficiency in irq handlers.

        Usage Example:
        NEW_FIFO(cmdQueue, CMD_QUEUE_SIZE);
        FifoPush(cmdQueue, 'p')

   Revision History:
    11/10/07  Released to lib_mitchell
    04/26/09  Added FifoRemaining
    08/26/13  Removed HC08 support and fifo location on natural boundary limitation
              Changed types to stdint.h types
              Removed kludgy support for any data types other than char
              Added locks to make thread safe
              Removed modulo operations from head/tail pointers, replaced with comparisions
              Added fifoPushStr, fifoPopStr
    04/30/14  Incorporated into memory.c which includes general collection classes
    02/05/16  Added Push16/Pop16, Push32/Pop32, Push64/Pop64, PushN/PopN
    09/12/16  Separated struct TFifo from the data storage array to minimize size of initialization constant

 *****************************************************************************/


/*** defined in fifo.h ***
#define NEW_FIFO(fp, SIZE)	returns an undefined type with SIZE elements - must cast to TFifo
#define fifoFull(fifo)      ((bool)         (((TFifo) fifo)->entries == ((TFifo) fifo)->size))
#define fifoEmpty(fifo)     ((bool)         (!((TFifo) fifo)->entries))
#define fifoEntries(fifo)   ((fifo_index_t) (((TFifo) fifo)->entries))
#define fifoRemaining(fifo) ((fifo_index_t) (((TFifo) fifo)->wrap - ((TFifo) fifo)->entries))
#define fifoReset(fifo)     ((void)         (((TFifo) fifo)->entries = ((TFifo) fifo)->head = ((TFifo) fifo)->tail = 0)
*/


bool fifoPush(TFifo fifo, char c) {
    bool success = false;
    LOCK;
    if (!fifoFull(fifo)) {
        fifo->element[fifo->head++] = c;
        if (fifo->head == fifo->size) { fifo->head = 0; }
        ++fifo->entries;
        success = true;
    }
    END_LOCK;
    return (success);
}


bool  fifoPushN(TFifo fifo, uint16_t n, uint8_t * array) {
    bool success = false;
    LOCK;
    if (fifoRemaining(fifo) >= n) {
        success = true;
        for (uint16_t i=0; i<n; ++i) {
            success &= fifoPush(fifo, array[i]);
        }
    }
    END_LOCK;
    return (success);
}


bool  fifoPush16(TFifo fifo, uint16_t hw) {
    return (fifoPushN(fifo, (uint16_t) sizeof(uint16_t), (uint8_t *) &hw));
}


bool  fifoPush32(TFifo fifo, uint32_t w) {
    return (fifoPushN(fifo, (uint16_t) sizeof(uint32_t), (uint8_t *) &w));
}


bool  fifoPush64(TFifo fifo, uint64_t ll) {
    return (fifoPushN(fifo, (uint16_t) sizeof(uint64_t), (uint8_t *) &ll));
}


bool fifoPushStr(TFifo fifo, char * str) {
    fifo_index_t  len = 0;
    char * p_str = str;

    while (*p_str++) { ++len; }   // determine string length

    return (fifoPushN(fifo, len, (uint8_t *) str));
}


bool fifoPop(TFifo fifo, char *c) {
    bool success = false;
    LOCK;
    if (!fifoEmpty(fifo)) {
        *c = fifo->element[fifo->tail++];
        if (fifo->tail == fifo->size) { fifo->tail = 0; }
        --fifo->entries;
        success = true;
    }
    END_LOCK;
    return (success);
}


bool  fifoPopN(TFifo fifo, uint16_t n, uint8_t * array) {
    bool success = false;
    LOCK;
    if (!fifoEmpty(fifo)) {
        success = true;
        for (uint16_t i=0; i<n; ++i) {
            success &= fifoPop(fifo, (char *) &array[i]);
        }
    }
    END_LOCK;
    return (success);
}


bool  fifoPop16(TFifo fifo, uint16_t * hw) {
    return (fifoPopN(fifo, (uint16_t) sizeof(uint16_t), (uint8_t *) &hw));
}


bool  fifoPop32(TFifo fifo, uint32_t * w) {
    return (fifoPopN(fifo, (uint16_t) sizeof(uint32_t), (uint8_t *) &w));
}


bool  fifoPop64(TFifo fifo, uint64_t * ll) {
    return (fifoPopN(fifo, (uint16_t) sizeof(uint64_t), (uint8_t *) &ll));
}


bool fifoPopStr(TFifo fifo, char * str, uint16_t n) {
    bool success = fifoPopN(fifo, n, (uint8_t *) str);
    str[n] = '\0';
    return (success);
}


void fifoFill(TFifo fifo, char c) {
    while (!fifoFull(fifo)) { (void) fifoPush(fifo, c); }
}


void fifoPopOff(TFifo fifo, fifo_index_t n) {
    char	c;
    while (n-- > 0) { (void) fifoPop(fifo, &c); }
}


bool fifoArray(TFifo fifo, char *c, int32_t i) {
    bool  success = false;
    LOCK;
    if (i < 0) { i += fifo->entries; }					    /* index from end of array */
    if ((i >= 0) && (i < fifo->entries)) {	        /* index within range */
        i += fifo->tail;
        if (i >= fifo->size) { i -= fifo->size; }   /* modulo fifo.size */
        *c = fifo->element[i];
        success = true;
    }
    END_LOCK;
	return (success);
}


bool fifoScan(TFifo fifo, char c) {
    fifo_index_t  t, e;
    bool      success = false;
    LOCK;
    t = fifo->tail;
    e = fifo->entries;
    while (e--) {
        if (fifo->element[t] == c) {
            success = true;
            break;
        }
        t = (t + 1) % fifo->size;
    }
    END_LOCK;
    return (success);
}




#ifdef UNIT_TEST

#include <string.h>

/******************************************************************************/

/*
 * The macro PROFILE must be defined for this unit test.
 */

/*
 * POOL_PARTITION_XX_BLOCKS must be defined as below in memory.h
 *
#define POOL_PARTITION_8_BLOCKS       16      // number of 8 byte blocks
#define POOL_PARTITION_16_BLOCKS      8
#define POOL_PARTITION_32_BLOCKS      4
#define POOL_PARTITION_64_BLOCKS      2
#define POOL_PARTITION_128_BLOCKS     2
#define POOL_PARTITION_256_BLOCKS     1
#define POOL_PARTITION_512_BLOCKS     0
#define POOL_PARTITION_1024_BLOCKS    0
 *
 */


int pool_UNIT_TEST(void) {
    bool  pass = TRUE;
    int   i;
    void  *p_blk[POOL_BLOCKS];
    pool_profile_t * prof;

    #ifndef PROFILE
        #warning "PROFILE must be defined for the pool unit test."
        return ((int) !false);
    #endif

    /* allocate one block of each potential size */
    pass &= !!(p_blk[0]  = poolMalloc(8));
    pass &= !!(p_blk[16] = poolMalloc(16));
    pass &= !!(p_blk[24] = poolMalloc(32));
    pass &= !!(p_blk[28] = poolMalloc(64));
    pass &= !!(p_blk[30] = poolMalloc(128));
    pass &= !!(p_blk[32] = poolMalloc(256));
    pass &= !poolMalloc(512);   // returns NULL
    pass &= !poolMalloc(1024);  // returns NULL

    prof = poolProfile();
    pass &= *(prof->pool_state) == 0x0000000100010115LL;


    /* allocate all of the 8 byte blocks */
    for (i=1; i<8; ++i) {
        pass &= !!(p_blk[i] = poolMalloc(i));
    }
    for (i=8; i<POOL_PARTITION_8_BLOCKS; ++i) {
        pass &= !!(p_blk[i] = poolMalloc(8));
    }
    pass &= *(prof->pool_state) == 0x00000001ffff0115LL;

    /* try one more small block, will allocate a 16 byte block */
    pass &= !!(p_blk[17] = poolMalloc(1));
    pass &= *(prof->pool_state) == 0x00000001ffff8115LL;

    /* 22 blocks allocated already, allocate all remaining blocks */
    for (i=22; i<POOL_BLOCKS; ++i) {
        pass &= !!poolMalloc(1);
    }
    pass &= *(prof->pool_state) == 0x00000001ffffffffLL;

    /* memory pool exhausted */
    pass &= !poolMalloc(1);

    /* free a NULL pointer, should be ignored */
    poolFree(NULL);
    pass &= *(prof->pool_state) == 0x00000001ffffffffLL;


    /* free a nonsense pointer, should be ignored */
    poolFree((void *) prof);
    pass &= *(prof->pool_state) == 0x00000001ffffffffLL;

    /* free the 8 byte partition */
    for (i=0; i<POOL_PARTITION_8_BLOCKS; ++i) {
        poolFree(p_blk[i]);
    }
    pass &= *(prof->pool_state) == 0x000000000001ffffLL;

    /* free the second 16 byte block */
    poolFree(p_blk[17]);
    pass &= *(prof->pool_state) == 0x0000000000017fffLL;

    return ((int) !pass);
}


/******************************************************************************/

/*
 * NOT TESTED: dlGetIndex()
 */
#if (MEM_DL_SHIFT_BUF_SIZ != 8)
#error ("Delay line shift buffer size test out of sync with source")
#endif

typedef struct {
    int     _int;
    char    _char;
    void *  _next;
} mem_dl_struct_t;

const int   dl_init_int_7[7]      = { 6, 5, 4, 3, 2, 1, 0 };
const int   dl_init_int_8[8]      = { 7, 6, 5, 4, 3, 2, 1, 0 };
const int   dl_init_int_9[9]      = { 8, 7, 6, 5, 4, 3, 2, 1, 0 };
const int   dl_init_int_10[10]    = { 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 };
const char  dl_init_str_10[10]    = { 'j', 'i', 'h', 'g', 'f', 'e', 'd', 'c', 'b', 'a' };
const char  dl_init_str_11[11]    = { 'k', 'j', 'i', 'h', 'g', 'f', 'e', 'd', 'c', 'b', 'a' };
const mem_dl_struct_t dl_int_struct_10[10]= {
        { 9, 'j', (void *) 0x12340009 },
        { 8, 'i', (void *) 0x12340008 },
        { 7, 'h', (void *) 0x12340007 },
        { 6, 'g', (void *) 0x12340006 },
        { 5, 'f', (void *) 0x12340005 },
        { 4, 'e', (void *) 0x12340004 },
        { 3, 'd', (void *) 0x12340003 },
        { 2, 'c', (void *) 0x12340002 },
        { 1, 'b', (void *) 0x12340001 },
        { 0, 'a', (void *) 0x12340000 }
};

NEW_DELAY_LINE(dl_7_int32, int32_t, 7);
NEW_DELAY_LINE(dl_8_int32, int32_t, 8);
NEW_DELAY_LINE(dl_9_int32, int32_t, 9);

NEW_DELAY_LINE(dl_1_char, char, 1);
NEW_DELAY_LINE(dl_10_char, char, 10);

NEW_DELAY_LINE(dl_10_struct, mem_dl_struct_t, 10);


int dl_UNIT_TEST(void) {
    bool  pass = TRUE;

    /* array sizes smaller, same, and larger than MEM_DL_SHIFT_BUF_SIZ */
    (void) memcpy(dl_7_int32->element, &dl_init_int_7, sizeof(dl_init_int_7));
    (void) memcpy(dl_8_int32->element, &dl_init_int_8, sizeof(dl_init_int_8));
    (void) memcpy(dl_9_int32->element, &dl_init_int_9, sizeof(dl_init_int_9));

    /* smallest possible array size */
    dl_1_char->element[0] = 'a';

    /* char array */
    (void) memcpy(dl_10_char->element, &dl_init_str_10, sizeof(dl_init_str_10));

    /* struct array */
    (void) memcpy(dl_10_struct->element, &dl_int_struct_10, sizeof(dl_int_struct_10));

    /* dlTaps() */
    pass &= (dlTaps(dl_7_int32)   == 7);
    pass &= (dlTaps(dl_8_int32)   == 8);
    pass &= (dlTaps(dl_9_int32)   == 9);
    pass &= (dlTaps(dl_1_char)    == 1);
    pass &= (dlTaps(dl_10_char)   == 10);
    pass &= (dlTaps(dl_10_struct) == 10);

    /* dlGetTap() */
    pass &= (*(int *) dlGetTap(dl_7_int32,  0) == 6);    // nominal case
    pass &= (*(int *) dlGetTap(dl_7_int32,  6) == 0);
    pass &= (*(int *) dlGetTap(dl_7_int32, -1) == 0);    // negative index case
    pass &= (*(int *) dlGetTap(dl_7_int32, -7) == 6);
    pass &= (*(int *) dlGetTap(dl_7_int32,  9) == 4);    // index modulo case
    pass &= (*(int *) dlGetTap(dl_7_int32, -9) == 1);
    pass &= (*(char *) dlGetTap(dl_1_char, 0) == 'a');
    pass &= (*(char *) dlGetTap(dl_1_char, 1) == 'a');   // 1 tap, always returns only entry
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct, 0))->_int == 9);
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct, 0))->_next == (void *) 0x12340009);
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct, 9))->_int == 0);
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct, 9))->_next == (void *) 0x12340000);

    /* dlUpdate() */
    dlUpdate(dl_7_int32, &(int) { 7 } );                  // int: add a new element '7' replacing oldest element '0'
    pass &= (*(int *) dlGetTap(dl_7_int32, 0) == 7);      // '7' is most recent
    pass &= (*(int *) dlGetTap(dl_7_int32, -1) == 1);     // '1' is now the oldest
    dlUpdate(dl_10_char, &(char) { 'k' } );               // char
    pass &= (*(char *) dlGetTap(dl_10_char, 0) == 'k');
    pass &= (*(char *) dlGetTap(dl_10_char, -1) == 'b');
    mem_dl_struct_t add_struct = { 10, 'k', (void *) 0x1234000a };
    dlUpdate(dl_10_struct, &add_struct );                 // struct
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct,  0))->_int == 10);
    pass &= (((mem_dl_struct_t *) dlGetTap(dl_10_struct, -1))->_int == 1);

    /* dlAsArray() */
    int * dl_7_int_array = dlAsArray(dl_7_int32);
    pass &= (memcmp(dl_7_int_array, &dl_init_int_8, sizeof(dl_init_int_7)) == 0);
    char * dl_10_char_array = dlAsArray(dl_10_char);
    pass &= (memcmp(dl_10_char_array, &dl_init_str_11, sizeof(dl_init_str_10)) == 0);

    return ((int) !pass);
}


/******************************************************************************/

#define   OBUF_SIZE      64
#define   OBUF_SIZE_ODD  57

NEW_OBUF(obuf1, OBUF_SIZE);
NEW_OBUF(obuf2, OBUF_SIZE_ODD);

int testObuf(obuf_obj_t pool) {
  int min_free;
  int failed_allocs;
  void * block[8];
  bool  pass = true;

  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 64) && (failed_allocs == 0);

  block[0] = obufMalloc(pool, 12); pass &= (block[0] != NULL);
  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 64-12-4);

  block[1] = obufMalloc(pool, 12); pass &= (block[1] != NULL);
  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 64-12-4-12-4);

  block[2] = obufMalloc(pool, 12); pass &= (block[2] != NULL);
  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 64-12-4-12-4-12-4);

  block[3] = obufMalloc(pool, 12); pass &= (block[3] != NULL);
  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 0);

  pass &= !obufMalloc(pool, 12);            // pool exhausted
  obufMemStats(pool, &min_free, &failed_allocs);
  pass &= (min_free == 0) && (failed_allocs == 1);

  pass &= !obufFree(pool, block[0]);
  pass &= !obufFree(pool, block[2]);        // block freed out of order
  pass &= !obufFree(pool, &block);          // pointer outside of pool
  pass &= !obufFree(pool, block[1]);
  pass &= !obufFree(pool, block[3]);
  pass &= obufFree(pool, block[2]);         // pool empty, all blocks freed already

  for (int i=0; i<8; ++i) {                 // fill with min-size data
      block[i] = obufMalloc(pool, 1);
      pass &= (block[i] != NULL);
  }
  pass &= !obufMalloc(pool, 1);             // pool exhausted

  for (int i=0; i<8; ++i) {                 // empty it
      pass &= !obufFree(pool, block[i]);
  }
  pass &= obufFree(pool, block[7]);         // pool empty


  block[0] = obufMalloc(pool, 24); pass &= (block[0] != NULL);
  block[1] = obufMalloc(pool,  4); pass &= (block[1] != NULL);  // allocate a block mid-pool
  pass &= !obufFree(pool, block[0]);                            // pool empty except for small block in middle
  block[0] = obufMalloc(pool, 48); pass &= (block[0] == NULL);  // insufficient contiguous free memory
  block[1] = obufMalloc(pool, 24); pass &= (block[1] != NULL);  // but enough for 48 non-contiguous bytes
  block[2] = obufMalloc(pool, 24); pass &= (block[2] != NULL);

  return ((int) !pass);
}


int obuf_UNIT_TEST(void) {
  bool  fail = false;

  fail |= testObuf(obuf1);
  fail |= testObuf(obuf2);

  return (fail);
}


/******************************************************************************/

#define   FIFO_SMALL  4
#define   FIFO_LARGE  (UINT8_MAX+1)
#define   FIFO_ODD    11

static char str[FIFO_LARGE+1];


NEW_FIFO(smallFifo, FIFO_SMALL);
NEW_FIFO(largeFifo, FIFO_LARGE);
NEW_FIFO(oddFifo, FIFO_ODD);


int testFifo(TFifo fifo) {
  fifo_index_t  i;
  fifo_index_t  sz = fifo->size;
  char  c;
  bool  pass = true;

  pass &= (fifoEmpty(fifo));        /* test empty */

  pass &= (!fifoPop(fifo, &c));     /* underflow */
  for (i=0; i<sz; ++i) {            /* fill it */
      pass &= (fifoPush(fifo, i));
  }
  pass &= (!fifoPush(fifo, i));     /* overflow */

  pass &= (fifoFull(fifo));         /* test full */

  for (i=0; i<sz; ++i) {
      pass &= (fifoPop(fifo, &c));  /* empty it */
  }
  pass &= (!fifoPop(fifo, &c));     /* underflow */

  fifoReset(fifo);                  /* test reset, ensure pointers are at origin */
  pass &= (fifoEntries(fifo) == 0);

  pass &= (fifoPush(fifo, 'x'));    /* scan target at front of fifo */
  for (i=0; i<sz>>1; ++i) {         /* fifo half full */
      pass &= (fifoPush(fifo, 0));
  }
  pass &= (fifoEntries(fifo) == (1 + (sz >> 1))); /* test Entries */

  pass &= (fifoScan(fifo, 'x'));    /* scan success */
  pass &= (!fifoScan(fifo, 'y'));   /* scan failure */
  pass &= (fifoPop(fifo, &c));      /* remove scan target */
  pass &= (!fifoScan(fifo, 'x'));   /* scan failure */
  pass &= (fifoPush(fifo, 'x'));    /* replace scan target at end of fifo */
  pass &= (fifoScan(fifo, 'x'));    /* scan success */
  pass &= (!fifoScan(fifo, 'y'));   /* scan failure */

  for (i=0; i<=sz>>1; ++i) {        /* empty it, half full + 1 */
      pass &= (fifoPop(fifo, &c));
  }
  pass &= (!fifoPop(fifo, &c));     /* underflow */

  fifoReset(fifo);                  /* ensure pointers are at origin */
  fifoFill(fifo, 0);                /* fill fifo */
  for (i=0; i<=sz>>1; ++i) {        /* empty the front half */
      pass &= (fifoPop(fifo, &c));
  }
  pass &= (fifoPush(fifo, 'x'));    /* scan target at end of fifo */
  pass &= (fifoScan(fifo, 'x'));    /* test scan requiring a pointer wraparound */
  pass &= (!fifoScan(fifo, 'y'));   /* scan failure */
  pass &= (fifoPopStr(fifo, str, fifoEntries(fifo))); /* empty the fifo */
  pass &= (!fifoPop(fifo, &c));     /* underflow */
  pass &= (!fifoPopStr(fifo, str, 1)); /* underflow */

  for (i=0; i<sz-1; ++i) {          /* initialize a string of length fifo->size - 1 */
      str[i] = 'z';                 /* to make room for a scan target */
  }
  str[i] = '\0';
  fifoReset(fifo);                  /* test string push without wraparound */
  pass &= (fifoPushStr(fifo, str));
  pass &= (fifo->entries == sz-1);  /* verify proper pointer operation */
  pass &= (fifo->head == fifo->entries);
  pass &= (!fifoPushStr(fifo, str)); /* verify str won't push if too large */
  pass &= (fifoPush(fifo, 'x'));    /* scan target at end of fifo */
  pass &= (fifoScan(fifo, 'x'));    /* test scan */
  pass &= (!fifoScan(fifo, 'y'));   /* scan failure */

  fifoReset(fifo);                  /* test string push with wraparound */
  for (i=0; i<sz>>1; ++i) {         /* fifo half full */
      pass &= (fifoPush(fifo, 0));
  }
  fifoPopOff(fifo, sz);             /* empty fifo, leave pointers in mid-fifo */
  pass &= (fifo->entries == 0);     /* verify proper pointer operation */
  pass &= (fifo->head == sz>>1);
  pass &= (fifo->head == fifo->tail);
  pass &= (fifoPushStr(fifo, str)); /* load with fifo-sized string requiring wraparound */
  pass &= (fifoPush(fifo, 'x'));    /* scan target at end of fifo */
  pass &= (fifoScan(fifo, 'x'));    /* test scan requiring a pointer wraparound */
  pass &= (!fifoScan(fifo, 'y'));   /* scan failure */

  return ((int) !pass);             /* return zero if all tests pass */
}

int testFifoN(TFifo fifo) {
  fifo_index_t  i;
  fifo_index_t  sz = fifo->size;
  char  c;
  uint16_t hw;
  uint32_t w;
  uint64_t ll;
  bool  pass = true;

  /* This test must be run with a fifo with size == 11 */

  fifoReset(fifo);
  for (i=0; i<4; ++i) {                                   /* fifo approx half full */
      pass &= (fifoPush(fifo, 0));
  }
  fifoPopOff(fifo, sz);                                   /* empty fifo, leave head pointer at location 4 */
  while (fifoPush16(fifo, 0x1234));                       /* fill fifo */
  pass &= ((sz - fifo->entries) == 1);                     /* verify odd fifo size has 1 byte remaining */
  pass &= fifoPopN(fifo, sz - 1, (uint8_t *) str);        /* pop off as chars */
  pass &= !fifoPop(fifo, &c);                             /* underflow */

  fifoReset(fifo);
  pass &= fifoPush32(fifo, 0x12345678);                   /* fill fifo */
  pass &= fifoPush32(fifo, 0x12345678);
  pass &= !fifoPush32(fifo, 0x12345678);                  /* overflow */
  pass &= ((sz - fifo->entries) == 3);                    /* verify space remaining < size of Push32 */
  pass &= fifoPop16(fifo, &hw);
  pass &= fifoPop16(fifo, &hw);
  pass &= fifoPop16(fifo, &hw);
  pass &= fifoPop16(fifo, &hw);
  pass &= !fifoPop(fifo, &c);                             /* underflow */

  fifoReset(fifo);
  while (fifoPush64(fifo, 0x0123456789abcdef));           /* fill fifo */
  pass &= ((sz - fifo->entries) == 3);                    /* verify space remaining  */
  pass &= fifoPop32(fifo, &w);
  pass &= fifoPop32(fifo, &w);
  pass &= !fifoPop(fifo, &c);                             /* underflow */

  fifoReset(fifo);
  for (i=0; i<4; ++i) {                                   /* fifo approx half full */
      pass &= (fifoPush(fifo, 0));
  }
  fifoPopOff(fifo, sz);                                   /* empty fifo, leave head pointer at location 4 */
  pass &= (fifoPushN(fifo, sz, (uint8_t *) str));         /* fill fifo */
  pass &= (fifo->entries == fifo->size);                  /* verify fifo filled completely */
  pass &= fifoPop64(fifo, &ll);                           /* 8 bytes */
  pass &= fifoPop16(fifo, &hw);                           /* + 2 bytes = 10 bytes */
  pass &= fifoPop(fifo, &c);                              /* + 1 byte  = 11 bytes */
  pass &= fifoEmpty(fifo);

  return ((int) !pass);             /* return zero if all tests pass */
}


int fifo_UNIT_TEST(void) {
  bool  fail = false;

  fail |= testFifo(smallFifo);
  fail |= testFifo(oddFifo);
  fail |= testFifo(largeFifo);
  fail |= testFifoN(oddFifo);

  return (fail);
}

#endif  /* UNIT_TEST */

