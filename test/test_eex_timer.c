/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/
//-- unity: unit test framework
#include "unity.h"
#include "assert_test_helpers.h"

//-- module being tested
#include "eex_os.h"
#include "eex_timer.h"
#include "eex_platform_mock.c"

#pragma GCC diagnostic ignored "-Wmultichar"            // to allow e.g. 'MUTX'
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"  // console 64 bit pointers don't like cast to uint32_t
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"


/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/



/*******************************************************************************
 *    MODULE INTERNAL DATA
 ******************************************************************************/

extern volatile uint32_t  g_timer_ms;


/*******************************************************************************
 *    PRIVATE TYPES
 ******************************************************************************/

/*******************************************************************************
 *    PRIVATE DATA
 ******************************************************************************/

bool  g_all_tests_run;

/*******************************************************************************
 *    PRIVATE FUNCTIONS
 ******************************************************************************/

/*******************************************************************************
 *    SETUP, TEARDOWN
 ******************************************************************************/
void setUp(void) {
    g_all_tests_run = false;
}

void tearDown(void) {
    TEST_ASSERT_TRUE(g_all_tests_run);
    g_all_tests_run = false;
}

/*******************************************************************************
 *    TESTS
 ******************************************************************************/

void  _atomicBitSet(uint32_t * const field, uint32_t const bit);
void test_bit_set(void) {
    uint32_t field = 0;
    _atomicBitSet(&field, 0);   TEST_ASSERT_EQUAL_HEX(0x00000000, field);
    _atomicBitSet(&field, 1);   TEST_ASSERT_EQUAL_HEX(0x00000001, field);
    _atomicBitSet(&field, 2);   TEST_ASSERT_EQUAL_HEX(0x00000003, field);
    _atomicBitSet(&field, 3);   TEST_ASSERT_EQUAL_HEX(0x00000007, field);
    _atomicBitSet(&field, 5);   TEST_ASSERT_EQUAL_HEX(0x00000017, field);
    _atomicBitSet(&field, 9);   TEST_ASSERT_EQUAL_HEX(0x00000117, field);
    _atomicBitSet(&field, 13);  TEST_ASSERT_EQUAL_HEX(0x00001117, field);
    _atomicBitSet(&field, 29);  TEST_ASSERT_EQUAL_HEX(0x10001117, field);
    _atomicBitSet(&field, 30);  TEST_ASSERT_EQUAL_HEX(0x30001117, field);
    _atomicBitSet(&field, 31);  TEST_ASSERT_EQUAL_HEX(0x70001117, field);
    _atomicBitSet(&field, 32);  TEST_ASSERT_EQUAL_HEX(0xf0001117, field);
    field = 0;
    TEST_ASSERTION_SHOULD_ASSERT(_atomicBitSet(NULL, 0));
    TEST_ASSERTION_SHOULD_ASSERT(_atomicBitSet(&field, 33));
    TEST_ASSERT_EQUAL_HEX(0, field);

    g_all_tests_run = true;
}

void  _atomicBitClr(uint32_t * const field, uint32_t const bit);
void test_bit_clr(void) {
    uint32_t field = 0xffffffff;
    _atomicBitClr(&field, 0);  TEST_ASSERT_EQUAL_HEX(0xffffffff, field);
    _atomicBitClr(&field, 1);  TEST_ASSERT_EQUAL_HEX(0xfffffffe, field);
    _atomicBitClr(&field, 2);  TEST_ASSERT_EQUAL_HEX(0xfffffffc, field);
    _atomicBitClr(&field, 3);  TEST_ASSERT_EQUAL_HEX(0xfffffff8, field);
    _atomicBitClr(&field, 5);  TEST_ASSERT_EQUAL_HEX(0xffffffe8, field);
    _atomicBitClr(&field, 9);  TEST_ASSERT_EQUAL_HEX(0xfffffee8, field);
    _atomicBitClr(&field, 13); TEST_ASSERT_EQUAL_HEX(0xffffeee8, field);
    _atomicBitClr(&field, 29); TEST_ASSERT_EQUAL_HEX(0xefffeee8, field);
    _atomicBitClr(&field, 30); TEST_ASSERT_EQUAL_HEX(0xcfffeee8, field);
    _atomicBitClr(&field, 31); TEST_ASSERT_EQUAL_HEX(0x8fffeee8, field);
    _atomicBitClr(&field, 32); TEST_ASSERT_EQUAL_HEX(0x0fffeee8, field);
    field = 0xffffffff;
    TEST_ASSERTION_SHOULD_ASSERT(_atomicBitClr(NULL, 0));
    TEST_ASSERTION_SHOULD_ASSERT(_atomicBitClr(&field, 33));
    TEST_ASSERT_EQUAL_HEX(0xffffffff, field);

    g_all_tests_run = true;
}




