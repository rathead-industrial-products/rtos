/*****************************************************************************

      (c) Copyright 2014 ee-quipment.com
      ALL RIGHTS RESERVED.

    memory.h - Collections that operate on statically allocated blocks of memory
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

#ifndef _memory_H_
#define _memory_H_

#include  <stdint.h>
#include  <stddef.h>
#include  "contract.h"


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
    04/03/15  Added dlGetIndex to allow access unaffected by dlUpdate

 *****************************************************************************/

/*
 * Macro to create a delay line ring buffer
 */
#define NEW_DELAY_LINE(obj_name, type, num_taps)                              \
static struct obj_name##_struct {                                             \
    const uint16_t  taps;                                                     \
    uint16_t        index;                                                    \
    const size_t    type_size;                                                \
    type            element[num_taps];                                        \
}  obj_name##_obj =  { num_taps, 0, sizeof(type) };                           \
struct obj_name##_struct * const obj_name = &obj_name##_obj;


void      dlUpdate(void * dl_obj, void * dl_element);   // insert dl_element at tap zero
void *    dlGetTap(void * dl_obj, int16_t tap);         // return pointer to element at tap
uint16_t  dlGetIndex(void * dl_obj);                    // return current index pointing to tap zero
void *    dlAsArray(void * dl_obj);                     // return pointer to array of delay line elements
uint16_t  dlTaps(void * dl_obj);                        // return number of taps in delay line




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

typedef enum { OBUF_NOERR=0, OBUF_INVALID_POINTER } obuf_error_t;

typedef struct obuf_dblk_t * p_obuf_dblk_t;
struct obuf_dblk_t {
    p_obuf_dblk_t next;
    char          data[4];    // actually variable, 4 to ensure sizeof() == 8
};

typedef struct {
    uint16_t      min_free;
    uint16_t      n_failed;
    p_obuf_dblk_t oldest;
    uint16_t      n_blks;
    uint16_t      size;
    char          buf[1];     // rest of pool up to ~ 64K
} * obuf_obj_t;

/*
 * Macro to create a buffer pool
 * Round up requested size to a multiple of sizeof(obuf_dblk_t) - 8 bytes
 */
#define NEW_OBUF(poolname, bsize)                                              \
__attribute__ ((aligned(sizeof(uint32_t))))                                    \
struct {                                                                       \
    uint16_t      max_free;                                                    \
    uint16_t      n_failed;                                                    \
    void *        oldest;                                                      \
    uint16_t      n_blks;                                                      \
    uint16_t      size;                                                        \
    void *        buf[((bsize + 8 - 1) & ~7) / sizeof(void *)];                \
} _obuf_obj_##poolname = { (bsize + 8 - 1) & ~7, 0, &(_obuf_obj_##poolname.buf), 0, (bsize + 8 - 1) & ~7, { &(_obuf_obj_##poolname.buf) } }; \
obuf_obj_t const poolname = (obuf_obj_t) &_obuf_obj_##poolname

void *        obufMalloc(obuf_obj_t pool, uint32_t size);   // returns NULL on error
obuf_error_t  obufFree(obuf_obj_t pool, void * ptr);        // must free memory in the order it was malloc'd
void          obufDataPtrs(obuf_obj_t pool, void * ptr_array[], uint32_t n_ptrs);  // Return a list of valid data block pointers in the pool
void          obufMemStats(obuf_obj_t pool, int * min_free, int * failed_allocs);  // return low watermark of mem avail and num of failed alloc calls




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
              Removed modulo operations from head/tail pointers, replaced with comparisons
              Added fifoPushStr, fifoPopStr
    04/30/14  Incorporated into memory.c which includes general collection classes
    02/05/16  Added Push16/Pop16, Push32/Pop32, Push64/Pop64, PushN/PopN
    09/12/16  Separated struct TFifo from the data storage array to minimize size of initialization constant
              Changed NEW_FIFO to return a TFifo instead of an anonymous struct. This breaks the API.

 *****************************************************************************/


typedef	struct	  TFifo * TFifo;
typedef uint16_t  fifo_index_t;

struct TFifo {
    fifo_index_t  entries;
    fifo_index_t  size;
    fifo_index_t  head, tail;
    char *        element;
};

/// Macro to define the storage for a FIFO. name has global scope.
#define NEW_FIFO(name, size)                                      \
  char name##_array[size] = { 0 };                                \
  struct TFifo name##_struct = { 0, size, 0, 0, name##_array };   \
  const TFifo name = &name##_struct


#define fifoFull(fifo)      ((bool)         (((TFifo) fifo)->entries == ((TFifo) fifo)->size))
#define fifoEmpty(fifo)     ((bool)         (!((TFifo) fifo)->entries))
#define fifoEntries(fifo)   ((fifo_index_t) (((TFifo) fifo)->entries))
#define fifoRemaining(fifo) ((fifo_index_t) (((TFifo) fifo)->size - ((TFifo) fifo)->entries))
#define fifoReset(fifo)     ((void)         (((TFifo) fifo)->entries = ((TFifo) fifo)->head = ((TFifo) fifo)->tail = 0))

void  fifoFill(TFifo fifo, char c);		  	                /* fill fifo with c */
bool  fifoScan(TFifo fifo, char c);     	                /* returns TRUE if c is in fifo */
bool  fifoPush(TFifo fifo, char c);                       /* returns FALSE if fifo full */
bool  fifoPush16(TFifo fifo, uint16_t hw);
bool  fifoPush32(TFifo fifo, uint32_t w);
bool  fifoPush64(TFifo fifo, uint64_t ll);
bool  fifoPushN(TFifo fifo, uint16_t n, uint8_t * array);
bool  fifoPushStr(TFifo fifo, char * str);                /* returns false if fifo cannot contain entire string */
bool  fifoPop(TFifo fifo, char * c);     	                /* return FALSE if fifo empty */
bool  fifoPop16(TFifo fifo, uint16_t * hw);
bool  fifoPop32(TFifo fifo, uint32_t * w);
bool  fifoPop64(TFifo fifo, uint64_t * ll);
bool  fifoPopN(TFifo fifo, uint16_t n, uint8_t * array);
bool  fifoPopStr(TFifo fifo, char * str, uint16_t n);     /* pop n items into str, terminate with '\0', n may be > fifo.size */
void  fifoPopOff(TFifo fifo, fifo_index_t n);	            /* pop n items off and discard them, n may be > fifo.size */
bool  fifoArray(TFifo fifo, char *c, int32_t i);          /* treat the fifo like an array and return element [i] */
                                                          /* i may be negative where -1 is the end of the fifo */
												                                  /* return false if i is out of range */



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

   Revision History:
    09/09/16  Initial release

 *****************************************************************************/

/*
 * Constants fixed by implementation. Do not change.
 */
#define POOL_BLOCKS_MAX               64
#define POOL_PARTITIONS               8

/*
 * There are 8 partitions of 8, 16, 32, 64, 128, 256, 512, and 1024 bytes.
 * The number is blocks in each partition is application dependent and
 * set by the macros below.
 *
 * Total RAM statically allocated for the pool is the sum of the blocks
 * in each partition, plus overhead.
 */
#define POOL_PARTITION_8_BLOCKS       16      // number of 8 byte blocks
#define POOL_PARTITION_16_BLOCKS      8
#define POOL_PARTITION_32_BLOCKS      4
#define POOL_PARTITION_64_BLOCKS      2
#define POOL_PARTITION_128_BLOCKS     2
#define POOL_PARTITION_256_BLOCKS     1
#define POOL_PARTITION_512_BLOCKS     0
#define POOL_PARTITION_1024_BLOCKS    0

/* Total up number of blocks and verify it is <= allowable max of 64 */
#define POOL_BLOCKS   (POOL_PARTITION_8_BLOCKS   + POOL_PARTITION_16_BLOCKS  +    \
                       POOL_PARTITION_32_BLOCKS  + POOL_PARTITION_64_BLOCKS  +    \
                       POOL_PARTITION_128_BLOCKS + POOL_PARTITION_256_BLOCKS +    \
                       POOL_PARTITION_512_BLOCKS + POOL_PARTITION_1024_BLOCKS)

#if (POOL_BLOCKS > POOL_BLOCKS_MAX)
#error ("No more than 64 blocks can be defined in the memory pool.")
#endif

/*
 * Profiling / Statistics support
 *
 * pool_stat is a record of allocation attemts/failures for each partition
 *
 * pool_state is the bit vector maintaining the allocated/free state of each block.
 * The organization of the bit vector is dependent upon the number of blocks
 * in each partition. Bit 0 of the bit vector corresponds to the first block
 * of the smallest defined partition.
 *
 * pool_history is a fifo containing a record of the last POOL_HISTORY_DEPTH
 * memory operations (poolMalloc, poolFree). size is the block size allocated
 * or freed and may differ from size requested by poolMalloc.
 * Each entry is 16 bits of the form:
 *
 *       15   14                        0
 *     ----------------------------------
 *    | OP  |  size (allocated or freed) |
 *     ----------------------------------
 *
 *     OP = 1 for poolMalloc, 0 for poolFree
 *
 */
#define POOL_HISTORY_DEPTH            1024      // entries in pool history

typedef struct {
    uint8_t   cur_alloc;    // number of blocks currently allocated
    uint8_t   max_alloc;    // maximum number of blocks allocated at one time
    uint16_t  cnt_alloc;    // number of times a block has been allocated from this partition
    uint16_t  cnt_fail;     // number of times no blocks were available for allocation from this partition
} pool_stats_t;

typedef struct {
    volatile pool_stats_t * pool_stat;
    TFifo                   pool_history;
    volatile uint64_t *     pool_state;
} pool_profile_t;

void *            poolMalloc(size_t size);
void              poolFree(void * addr);
pool_profile_t *  poolProfile(void);


#endif  /* _memory_H_ */
