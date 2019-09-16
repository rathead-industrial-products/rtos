/**
 *
 *  @file  eex_bitmap.h
 *  @brief Functions to manipulate bit arrays.
 *
 *  Functions are provided which operate on general sets and thread ID sets.
 *
 *  General n-sized sets are represented by bit arrays in the range [0..n-1].
 *
 *  Thread IDs are represented by bit arrays in the range [n..1].
 *
 *  COPYRIGHT NOTICE: (c) 2016 DDPA LLC
 *  All Rights Reserved
 *
 */


#ifndef _eex_bitmap_H_
#define _eex_bitmap_H_

#include  <stdint.h>
#include  "eex_configure.h"

typedef uint32_t  eex_thread_id_t;      /// todo: temporary typedef

// ==== Typedefs ====

/// The bitmap type defines a bit array representing a general set.
typedef struct eex_bm_t {
    uint32_t  const     bits;     ///< number of bits in the bitmap
    uint32_t  volatile  word[1];  ///< bitmap as a uint32_t array - actual size is word[((bits-1)/32)+1]
} eex_bm_t ;


// ==== Defines ====

/// Storage allocator for NEW_EEX_BITMAP.
/// \note A static array the same size as the bitmap will be allocated in Flash for initialization.
#define NEW_EEX_BITMAP_ARRAY(name, bit_size)                                  \
static uint32_t name[(((bit_size)-1)/32)+2] = { bit_size }


/// New storage allocator for a bit array. The bitmap pointer (name) is a constant with static scope.
/// \note A static array the same size as the bitmap will be allocated in Flash for initialization.
#define NEW_EEX_BITMAP(name, bit_size)                                        \
/* NEW_EEX_BITMAP_ARRAY(name##_bm_array, bit_size);                              \  */  \
static uint32_t name##_bm_array[(((bit_size)-1)/32)+2] = { bit_size }         \   /* if this works we can remove NEW_EEX_BITMAP_ARRAY */ \
static eex_bm_t * const name = (eex_bm_t *) name##_bm_array


// ==== Bitmap Functions ====

/// Set a bit.
/// The set operation is atomic (and lockless) within the containing word.
void  eexBMSet(eex_bm_t * const a, uint32_t const set_bit);

/// Clear a bit.
/// The clear operation is atomic (and lockless) within the containing word.
void  eexBMClr(eex_bm_t * const a, uint32_t const clr_bit);

/// Find the state of a bit.
/// \return the state of the bit (1 or 0).
uint32_t  eexBMState(eex_bm_t const * const a, uint32_t const which_bit);

/// Find the number of zeros before the first 1 in the bitmap.
/// \return the number of zeros before the first 1 bit, or a value >= a->size if there are no 1's.
uint32_t  eexBMCLZ(eex_bm_t const * const a);

/// Test two bitmaps for equality. Bitmaps a and b must be the same size.
/// \return 1 if the bitmaps are equal, 0 otherwise.
uint32_t  eexBMEQ(eex_bm_t const * const a, eex_bm_t const * const b);

/// Logical AND of two bitmaps. Bitmaps a and b must be the same size.
/// \return the logical AND of bitmaps a and b.
void  eexBMAND(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b);

/// Logical Inclusive-OR of two bitmaps. Bitmaps a and b must be the same size.
/// \return the logical inclusive OR of bitmaps a and b.
void  eexBMOR(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b);

/// Logical Exclusive-OR of two bitmaps. Bitmaps a and b must be the same size.
/// \return the logical exclusive OR of bitmaps a and b.
void  eexBMXOR(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b);

/// Compliment all bits in a bitmap. Bitmaps nota and a must be the same size.
/// \return the compliment of bitmap a.
void  eexBMNOT(eex_bm_t * const nota, eex_bm_t const * const a);

/// Copy a bitmap into an allocated bitmap structure. Bitmaps copy and a must be the same size.
/// \return a copy of bitmap a.
void  eexBMCopy(eex_bm_t * const copy, eex_bm_t const * const a);


// ==== Thread Bitmap Functions ====

/// Set the thread_id bit. If thread_id==0 bitmap a will return unchanged.
/// The set operation is atomic (and lockless) within the containing word.
void  eexBMThreadSet(eex_bm_t * const a, eex_thread_id_t const thread_id);

/// Clear the thread_id bit. If thread_id==0 bitmap a will return unchanged.
/// The clear operation is atomic (and lockless) within the containing word.
void  eexBMThreadClr(eex_bm_t * const a, eex_thread_id_t const thread_id);

/// Find the state of the thread_id bit.
/// \return the state of the thread_id bit (1 or 0) or 0 if thread_id==0.
uint32_t  eexBMThreadState(eex_bm_t const * const a, eex_thread_id_t const thread_id);

/// Find the thread_id representing the first 1 in the bitmap.
/// \return the thread_id representing the first 1 bit (msb = EEX_CFG_USER_THREADS_MAX, lsb = 1).
eex_thread_id_t eexBMThreadFF1(eex_bm_t const * const a);



#endif  /* _eex_bitmap_H_ */


