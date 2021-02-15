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

extern eex_timer_cb_t *g_active_timer_list_head;
extern eex_timer_cb_t *g_add_timer_list_head;
extern eex_signal_cb_t *sig_timer;


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
    g_active_timer_list_head = NULL;
    g_add_timer_list_head    = NULL;
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

void test_add_timer(void) {
    eex_timer_cb_t  timer1_cb = { NULL, NULL, NULL, -1, 100,-1, -1, NULL };
    eex_timer_cb_t  timer2_cb = { NULL, NULL, NULL, -1, 100,-1, -1, NULL };

    // timer with NULL function pointer won't get added
    sig_timer->signal = 0;
    eexTimerAdd(&timer1_cb);
    TEST_ASSERT_EQUAL (NULL, g_add_timer_list_head);
    TEST_ASSERT_EQUAL (0, sig_timer->signal);

    // add timer1
    timer1_cb.fn_timer = (eex_timer_fn_t) 0x11111111;
    timer1_cb.next     = (eex_timer_cb_t *) -1;   // initialize to non-null
    eexTimerAdd(&timer1_cb);
    TEST_ASSERT_EQUAL (&timer1_cb, g_add_timer_list_head);
    TEST_ASSERT_EQUAL (NULL, timer1_cb.next);
    TEST_ASSERT_EQUAL (1, sig_timer->signal);

    // add timer2
    sig_timer->signal = 0;
    timer2_cb.fn_timer = (eex_timer_fn_t) 0x22222222;
    timer2_cb.next     = (eex_timer_cb_t *) -1;   // initialize to non-null
    eexTimerAdd(&timer2_cb);
    TEST_ASSERT_EQUAL (&timer2_cb, g_add_timer_list_head);  // timers insert into list at head
    TEST_ASSERT_EQUAL (&timer1_cb, timer2_cb.next);
    TEST_ASSERT_EQUAL (NULL, timer1_cb.next);
    TEST_ASSERT_EQUAL (1, sig_timer->signal);

    g_all_tests_run = true;
}






























