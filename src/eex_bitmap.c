/*******************************************************************************

    Functions to manipulate bitmaps representing general sets and sets of threads.

    Bitmaps are in big-endian format. Diagram from http://www.ietf.org/rfc/ien/ien137.txt

                 |---word0---|---word1---|---word2---|....
                 |C0,C1,C2,C3|C0,C1,C2,C3|C0,C1,C2,C3|.....
                 |B0......B31|B0......B31|B0......B31|......

                 |T96.....T65|T64.....T33|T32......T1|......

    Note: C = Char (byte), B = bit, T = thread ID

    The CLZ algorithm will return the leftmost, or least significant bit.

    The eexBMThread_ set of functions operate on thread IDs which are
    reversed from bit position numbering and start with position 1, not 0.

    COPYRIGHT NOTICE: (c) 2016 DDPA LLC
    All Rights Reserved

 ******************************************************************************/


#include  <stdint.h>
#include  "eex_bitmap.h"
#include  "eex_cpu.h"
#include  "eex_dbc.h"


// ==== Defines ====

/// Size of word array big enough to hold all bits.
#define EEX_BM_WORDS_IN_ARRAY(bit_size)     ((((bit_size) - 1) / 32) + 1)

/// Map thread ID to bit position and vice-versa.
#define EEX_BM_BIT_TID_CONV(bit_size, bt)   ((32 * (EEX_BM_WORDS_IN_ARRAY(bit_size))) - ((uint32_t) (bt)))

/// Map bit position to a containing 32 bit word (0 - word_max) in the bitmap.
#define EEX_BM_WORD_IDX(bit_pos)            ((bit_pos) / 32)

/// Map bit position to the proper bit (0 - 31) within the containing 32 bit word in the bitmap.
#define EEX_BM_BIT_IDX(bit_pos)             ((bit_pos) & 0x001f)


// ==== Bitmap Functions ====

void  eexBMSet(eex_bm_t * const a, uint32_t const set_bit) {
    uint32_t old_bm;
    uint32_t new_bm;

    REQUIRE (a && (set_bit < (32 * EEX_BM_WORDS_IN_ARRAY(a->bits))));
    do {
        old_bm = a->word[EEX_BM_WORD_IDX(set_bit)];
        new_bm = old_bm | (0x80000000U >> EEX_BM_BIT_IDX(set_bit));
    } while(eexCPUAtomicCAS(&(a->word[EEX_BM_WORD_IDX(set_bit)]), old_bm, new_bm));
}

void  eexBMClr(eex_bm_t * const a, eex_thread_id_t const clr_bit) {
    uint32_t old_bm;
    uint32_t new_bm;

    REQUIRE (a && (clr_bit < (32 * EEX_BM_WORDS_IN_ARRAY(a->bits))));
    do {
        old_bm = a->word[EEX_BM_WORD_IDX(clr_bit)];
        new_bm = old_bm & ~(0x80000000U >> EEX_BM_BIT_IDX(clr_bit));
    } while(eexCPUAtomicCAS(&(a->word[EEX_BM_WORD_IDX(clr_bit)]), old_bm, new_bm));
}

uint32_t  eexBMState(eex_bm_t const * const a, eex_thread_id_t const which_bit) {
    REQUIRE (a && (which_bit < (32 * EEX_BM_WORDS_IN_ARRAY(a->bits))));
    return ((a->word[EEX_BM_WORD_IDX(which_bit)] & (0x80000000U >> EEX_BM_BIT_IDX(which_bit))) != 0);
}

uint32_t  eexBMCLZ(eex_bm_t const * const a) {
    uint32_t i, zeros;

    REQUIRE (a);
    i = 0;
    /// Find the first nonzero word. Don't check the last word, CLZ will do that.
    while (i < (EEX_BM_WORDS_IN_ARRAY(a->bits) - 1)) {
        if (a->word[i]) { break; }
        ++i;
    }
    zeros = 32 * i;                 // accumulate zeros from empty words
    zeros += eexCPUCLZ(a->word[i]); // there may be more zeros than bits
    return (zeros);                 // in the bitmap if there are no ones
}

uint32_t  eexBMEQ(eex_bm_t const * const a, eex_bm_t const * const b) {
    REQUIRE (a && b);
    REQUIRE (a->bits == b->bits);
    for (uint32_t i=0; i<EEX_BM_WORDS_IN_ARRAY(a->bits); ++i) {
        if (a->word[i] != b->word[i]) {
            return (0);
        }
    }
    return (1);
}

void  eexBMAND(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b) {
    REQUIRE (rslt && a && b);
    REQUIRE (a->bits == b->bits);
    REQUIRE (a->bits == rslt->bits);
    for (uint32_t i=0; i<EEX_BM_WORDS_IN_ARRAY(a->bits); ++i) {
        rslt->word[i] = a->word[i] & b->word[i];
    }
}

void  eexBMOR(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b) {
    REQUIRE (rslt && a && b);
    REQUIRE (a->bits == b->bits);
    REQUIRE (a->bits == rslt->bits);
    for (uint32_t i=0; i<EEX_BM_WORDS_IN_ARRAY(a->bits); ++i) {
        rslt->word[i] = a->word[i] | b->word[i];
    }
}

void  eexBMXOR(eex_bm_t * const rslt, eex_bm_t const * const a, eex_bm_t const * const b) {
    REQUIRE (rslt && a && b);
    REQUIRE (a->bits == b->bits);
    REQUIRE (a->bits == rslt->bits);
    for (uint32_t i=0; i<EEX_BM_WORDS_IN_ARRAY(a->bits); ++i) {
        rslt->word[i] = a->word[i] ^ b->word[i];
    }
}

void  eexBMNOT(eex_bm_t * const nota, eex_bm_t const * const a) {
    REQUIRE (nota && a);
    REQUIRE (nota->bits == a->bits);
    for (uint32_t i=0; i<EEX_BM_WORDS_IN_ARRAY(a->bits); ++i) {
        nota->word[i] = ~(a->word[i]);
    }
}

void  eexBMCopy(eex_bm_t * const copy, eex_bm_t const * const a) {
    eexBMOR(copy,  a,  a);
}


// ==== Thread Bitmap Functions ====



void  eexBMThreadSet(eex_bm_t * const a, eex_thread_id_t const thread_id) {
    if (thread_id == 0) { return; }
    REQUIRE (a);
    uint32_t bit = EEX_BM_BIT_TID_CONV(a->bits, thread_id);
    eexBMSet(a, bit);
}

void  eexBMThreadClr(eex_bm_t * const a, eex_thread_id_t const thread_id) {
    if (thread_id == 0) { return; }
    REQUIRE (a);
    uint32_t bit = EEX_BM_BIT_TID_CONV(a->bits, thread_id);
    eexBMClr(a, bit);
}

uint32_t  eexBMThreadState(eex_bm_t const * const a, eex_thread_id_t const thread_id) {
    if (thread_id == 0) { return (0); }
    REQUIRE (a);
    uint32_t bit = EEX_BM_BIT_TID_CONV(a->bits, thread_id);
    return (eexBMState(a, bit));
}

/*
 * The bits in the bitmap must be temporarily increased to include all
 * bits in all words in the array or else the CLZ algorithm may improperly
 * mask some of them out.
 */
eex_thread_id_t  eexBMThreadFF1(eex_bm_t const * const a) {
    REQUIRE (a);
    uint32_t zeros = eexBMCLZ(a);
    if (zeros == 32 * EEX_BM_WORDS_IN_ARRAY(a->bits)) { return (0); }  // empty set
    return ((eex_thread_id_t) (EEX_BM_BIT_TID_CONV(a->bits, zeros)));
}




