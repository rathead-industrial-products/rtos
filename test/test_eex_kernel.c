/*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/

//-- unity: unit test framework
#include "unity.h"
#include "assert_test_helpers.h"

//-- module being tested
#include "eex_os.h"
#include "eex_kernel.h"

// platform specific functions defined in eex_os.h mocked up
#include "eex_platform_mock.c"

#pragma GCC diagnostic ignored "-Wmultichar"  // to allow e.g. 'MUTX'
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"  // console 64 bit pointers don't like cast to uint32_t
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"


/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/

#define EEX_EMPTY_THREAD_LIST    ((eex_thread_list_t) 0)   // const used to reset thread lists

// redefined to remove blocking function (return instruction) and allow return value to be captured
#undef EEX_PEND_POST
#define EEX_PEND_POST(p_rtn_status, p_rtn_val, timeout, val, p_kobj, action)  \
    eexPendPost(NULL, p_rtn_status, p_rtn_val, timeout, val, (eex_kobj_cb_t *) p_kobj, action)

/*******************************************************************************
 *    MODULE INTERNAL DATA
 ******************************************************************************/
extern volatile eex_thread_cb_t     g_thread_tcb[EEX_CFG_USER_THREADS_MAX+1];
extern volatile eex_thread_list_t   g_thread_ready_list;
extern volatile eex_thread_list_t   g_thread_waiting_list;
extern volatile eex_thread_list_t   g_thread_interrupted_list;
extern volatile uint32_t            g_timer_ms;
extern volatile uint32_t            g_timer_us;
extern volatile uint32_t            g_mock_interrupt_level;

extern volatile eex_kobj_cb_t       delay_kobj;

/*******************************************************************************
 *    PRIVATE TYPES
 ******************************************************************************/

/*******************************************************************************
 *    PRIVATE DATA
 ******************************************************************************/
EEX_SEMAPHORE_NEW(sema_10_10, 10, 10);
EEX_MUTEX_NEW(mutex);
EEX_SIGNAL_NEW(sig);

bool  g_all_tests_run;

/*******************************************************************************
 *    PRIVATE FUNCTIONS
 ******************************************************************************/

/*******************************************************************************
 *    SETUP, TEARDOWN
 ******************************************************************************/
void *memset(void *str, int c, size_t n);
void _eexThreadIDSet(eex_thread_id_t tid);
void setUp(void) {
    (void) memset((void *) g_thread_tcb, 0, sizeof(eex_thread_cb_t) * (EEX_CFG_USER_THREADS_MAX+1));  // reset thread control blocks
    g_thread_ready_list       = EEX_EMPTY_THREAD_LIST;
    g_thread_waiting_list     = EEX_EMPTY_THREAD_LIST;
    g_thread_interrupted_list = EEX_EMPTY_THREAD_LIST;
    g_mock_interrupt_level    = 0;   // thread mode
    g_timer_ms                = 0;
    g_timer_us                = 0;
    _eexThreadIDSet(0);               // running thread
    g_all_tests_run = false;
}

void tearDown(void) {
    TEST_ASSERT_TRUE(g_all_tests_run);
    g_all_tests_run = false;
}

/*******************************************************************************
 *    TESTS
 ******************************************************************************/

void test_kernel_timer(void) {
    uint32_t ms, us;

    g_timer_ms = 10;
    g_timer_us = 10;

    ms = eexKernelTime(&us);

    TEST_ASSERT_EQUAL(10, ms);
    TEST_ASSERT_EQUAL(10, us);

    g_timer_ms = 11;
    ms = eexKernelTime(NULL);
    TEST_ASSERT_EQUAL(11, ms);

    g_all_tests_run = true;
}

// Tag initialized to 1, increments by one, rolls over at 16 bits, skips zero
eex_tagged_data_t  _eexNewTaggedData(uint16_t data);
void test_tagged_data_generator(void) {
    eex_tagged_data_t td;

    td = _eexNewTaggedData(0);      TEST_ASSERT_EQUAL_INT(1, td.tag);                               TEST_ASSERT_EQUAL_INT(0, td.data);
                                    TEST_ASSERT_EQUAL_INT(td.tag+1, _eexNewTaggedData(0xffff).tag); TEST_ASSERT_EQUAL_INT(0, td.data);
    td = _eexNewTaggedData(0x1234); TEST_ASSERT_EQUAL_INT(3, td.tag);                               TEST_ASSERT_EQUAL_INT(0x1234, td.data);

    for (int i=4; i<0xffff; ++i) { td = _eexNewTaggedData(0); }
                                    TEST_ASSERT_EQUAL_INT(0xfffe, td.tag);                          TEST_ASSERT_EQUAL_INT(0, td.data);
    td = _eexNewTaggedData(5678);   TEST_ASSERT_EQUAL_INT(0xffff, td.tag);                          TEST_ASSERT_EQUAL_INT(5678, td.data);
    td = _eexNewTaggedData(9012);   TEST_ASSERT_EQUAL_INT(1, td.tag);                               TEST_ASSERT_EQUAL_INT(9012, td.data);

    g_all_tests_run = true;
}

// Immune to rollover effects as long as the difference is less than 2^31 (half the total unsigned value)
int32_t _eexTimeDiff(uint32_t time, uint32_t ref);
void test_time_diff(void) {
    TEST_ASSERT_EQUAL_INT32( 0,           _eexTimeDiff(0, 0));    // behavior around zero
    TEST_ASSERT_EQUAL_INT32( 1,           _eexTimeDiff(1, 0));
    TEST_ASSERT_EQUAL_INT32(-1,           _eexTimeDiff(0, 1));

    TEST_ASSERT_EQUAL_INT32(0xfffe,       _eexTimeDiff(0xffff,      1));
    TEST_ASSERT_EQUAL_INT32(0xffff,       _eexTimeDiff(0x10000,     1));
    TEST_ASSERT_EQUAL_INT32(0x10000,      _eexTimeDiff(0x10001,     1));
    TEST_ASSERT_EQUAL_INT32(2147483646,   _eexTimeDiff(0x7fffffff,  1));
    TEST_ASSERT_EQUAL_INT32(2147483647,   _eexTimeDiff(0x80000000,  1));
    TEST_ASSERT_EQUAL_INT32(-2147483648,  _eexTimeDiff(0x80000001,  1));  // sign flip at diff >= 2^31
    TEST_ASSERT_EQUAL_INT32(-2147483647,  _eexTimeDiff(0x80000002,  1));

    TEST_ASSERT_EQUAL_INT32(-0xfffe,      _eexTimeDiff(1,      0xffff));
    TEST_ASSERT_EQUAL_INT32(-0xffff,      _eexTimeDiff(1,     0x10000));
    TEST_ASSERT_EQUAL_INT32(-0x10000,     _eexTimeDiff(1,     0x10001));
    TEST_ASSERT_EQUAL_INT32(-2147483646,  _eexTimeDiff(1,  0x7fffffff));
    TEST_ASSERT_EQUAL_INT32(-2147483647,  _eexTimeDiff(1,  0x80000000));
    TEST_ASSERT_EQUAL_INT32(-2147483648,  _eexTimeDiff(1,  0x80000001));
    TEST_ASSERT_EQUAL_INT32(2147483647,   _eexTimeDiff(1,  0x80000002));  // sign flip at diff >= 2^31

    g_all_tests_run = true;
}

eex_thread_cb_t * eexThreadTCB(eex_thread_id_t tid);
void test_thread_tcb(void) {
    TEST_ASSERT_EQUAL_PTR(&g_thread_tcb[0], eexThreadTCB(0));
    TEST_ASSERT_EQUAL_PTR(&g_thread_tcb[EEX_CFG_USER_THREADS_MAX], eexThreadTCB(EEX_CFG_USER_THREADS_MAX));

    g_all_tests_run = true;
}

void test_thread_id_get_set(void) {
    _eexThreadIDSet(0);                           TEST_ASSERT_EQUAL_INT32(0, eexThreadID());
    _eexThreadIDSet(7);                           TEST_ASSERT_EQUAL_INT32(7, eexThreadID());
    _eexThreadIDSet(EEX_CFG_USER_THREADS_MAX-1);  TEST_ASSERT_EQUAL_INT32(EEX_CFG_USER_THREADS_MAX-1, eexThreadID());
    _eexThreadIDSet(EEX_CFG_USER_THREADS_MAX);    TEST_ASSERT_EQUAL_INT32(EEX_CFG_USER_THREADS_MAX, eexThreadID());
    TEST_ASSERTION_SHOULD_ASSERT(_eexThreadIDSet(EEX_CFG_USER_THREADS_MAX+1));    // will force an assertion
    TEST_ASSERTION_SHOULD_ASSERT(_eexThreadIDSet(-1));                            // will force an assertion

    g_all_tests_run = true;
}

void _eexBMSet(eex_bm_t * const a, uint32_t const bit);
void test_bm_set(void) {
    eex_bm_t bm = 0;
    _eexBMSet(&bm, 0);  TEST_ASSERT_EQUAL_HEX(0x00000000, bm);
    _eexBMSet(&bm, 1);  TEST_ASSERT_EQUAL_HEX(0x00000001, bm);
    _eexBMSet(&bm, 2);  TEST_ASSERT_EQUAL_HEX(0x00000003, bm);
    _eexBMSet(&bm, 3);  TEST_ASSERT_EQUAL_HEX(0x00000007, bm);
    _eexBMSet(&bm, 5);  TEST_ASSERT_EQUAL_HEX(0x00000017, bm);
    _eexBMSet(&bm, 9);  TEST_ASSERT_EQUAL_HEX(0x00000117, bm);
    _eexBMSet(&bm, 13); TEST_ASSERT_EQUAL_HEX(0x00001117, bm);
    _eexBMSet(&bm, 29); TEST_ASSERT_EQUAL_HEX(0x10001117, bm);
    _eexBMSet(&bm, 30); TEST_ASSERT_EQUAL_HEX(0x30001117, bm);
    _eexBMSet(&bm, 31); TEST_ASSERT_EQUAL_HEX(0x70001117, bm);
    _eexBMSet(&bm, 32); TEST_ASSERT_EQUAL_HEX(0xf0001117, bm);
    bm = 0;
    TEST_ASSERTION_SHOULD_ASSERT(_eexBMSet(&bm, 33));
    TEST_ASSERT_EQUAL_HEX(0, bm);

    g_all_tests_run = true;
}

void  _eexBMClr(eex_bm_t * const a, uint32_t const bit);
void test_bm_clr(void) {
    eex_bm_t bm = 0xffffffff;
    _eexBMClr(&bm, 0);  TEST_ASSERT_EQUAL_HEX(0xffffffff, bm);
    _eexBMClr(&bm, 1);  TEST_ASSERT_EQUAL_HEX(0xfffffffe, bm);
    _eexBMClr(&bm, 2);  TEST_ASSERT_EQUAL_HEX(0xfffffffc, bm);
    _eexBMClr(&bm, 3);  TEST_ASSERT_EQUAL_HEX(0xfffffff8, bm);
    _eexBMClr(&bm, 5);  TEST_ASSERT_EQUAL_HEX(0xffffffe8, bm);
    _eexBMClr(&bm, 9);  TEST_ASSERT_EQUAL_HEX(0xfffffee8, bm);
    _eexBMClr(&bm, 13); TEST_ASSERT_EQUAL_HEX(0xffffeee8, bm);
    _eexBMClr(&bm, 29); TEST_ASSERT_EQUAL_HEX(0xefffeee8, bm);
    _eexBMClr(&bm, 30); TEST_ASSERT_EQUAL_HEX(0xcfffeee8, bm);
    _eexBMClr(&bm, 31); TEST_ASSERT_EQUAL_HEX(0x8fffeee8, bm);
    _eexBMClr(&bm, 32); TEST_ASSERT_EQUAL_HEX(0x0fffeee8, bm);
    bm= 0xffffffff;
    TEST_ASSERTION_SHOULD_ASSERT(_eexBMClr(&bm, 33));
    TEST_ASSERT_EQUAL_HEX(0xffffffff, bm);

    g_all_tests_run = true;
}

uint32_t  _eexBMState(eex_bm_t const * const a, uint32_t const bit);
void test_bm_state(void) {
    eex_bm_t bm0 = 0;
    eex_bm_t bm1 = 0xffffffff;
    TEST_ASSERTION_SHOULD_ASSERT(_eexBMState(&bm0, 0));
    TEST_ASSERTION_SHOULD_ASSERT(_eexBMState(&bm1, 0));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 1));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 1));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 2));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 2));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 3));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 3));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 16));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 16));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 17));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 17));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 30));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 30));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 31));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 31));
    TEST_ASSERT_EQUAL(0, _eexBMState(&bm0, 32));
    TEST_ASSERT_EQUAL(1, _eexBMState(&bm1, 32));

    g_all_tests_run = true;
}

uint32_t  _eexBMFF1(const eex_bm_t a);
void test_bm_ff1(void) {
    TEST_ASSERT_EQUAL(0, _eexBMFF1(0));
    TEST_ASSERT_EQUAL(1, _eexBMFF1(1));
    TEST_ASSERT_EQUAL(2, _eexBMFF1(2));
    TEST_ASSERT_EQUAL(3, _eexBMFF1(4));
    TEST_ASSERT_EQUAL(16, _eexBMFF1(0x8000));
    TEST_ASSERT_EQUAL(17, _eexBMFF1(0x10000));
    TEST_ASSERT_EQUAL(30, _eexBMFF1(0x20000000));
    TEST_ASSERT_EQUAL(31, _eexBMFF1(0x40000000));
    TEST_ASSERT_EQUAL(32, _eexBMFF1(0x80000000));
    TEST_ASSERT_EQUAL(32, _eexBMFF1(0xffffffff));

    g_all_tests_run = true;
}

eex_thread_list_t * _eexThreadListGet(eex_thread_list_selector_t which_list);
void test_thread_list_get(void) {
    TEST_ASSERT_EQUAL_PTR(&g_thread_ready_list,       _eexThreadListGet(EEX_THREAD_READY));
    TEST_ASSERT_EQUAL_PTR(&g_thread_waiting_list,     _eexThreadListGet(EEX_THREAD_WAITING));
    TEST_ASSERT_EQUAL_PTR(&g_thread_interrupted_list, _eexThreadListGet(EEX_THREAD_INTERRUPTED));
    TEST_ASSERTION_SHOULD_ASSERT(_eexThreadListGet(-1));

    g_all_tests_run = true;
}

void _eexThreadListAdd(eex_thread_list_t *list, eex_thread_id_t tid);
void test_thread_list_add(void) {
    /* just calls _eexBMSet */

    g_all_tests_run = true;
}

void _eexThreadListDel(eex_thread_list_t *list, eex_thread_id_t tid);
void test_thread_list_del(void) {
    /* just calls _eexBMClr */

    g_all_tests_run = true;
}

bool _eexThreadListContains(const eex_thread_list_t *list, eex_thread_id_t tid);
void test_thread_list_contains(void) {
    /* just calls _eexBMState */

    g_all_tests_run = true;
}

eex_thread_id_t _eexThreadListHPT(eex_thread_list_t list, eex_thread_list_t mask);
void test_thread_list_hpt(void) {
    TEST_ASSERT_EQUAL(0,  _eexThreadListHPT(0, 0));
    TEST_ASSERT_EQUAL(1,  _eexThreadListHPT(1, 0));
    TEST_ASSERT_EQUAL(0,  _eexThreadListHPT(1, 1));
    TEST_ASSERT_EQUAL(4,  _eexThreadListHPT(0xff, 0xf0));
    TEST_ASSERT_EQUAL(17, _eexThreadListHPT(0xffffffff, 0xfffef0f0));

    g_all_tests_run = true;
}

void test_thread_timeout(void) {
    eex_thread_id_t     test_pri = EEX_CFG_USER_THREADS_MAX;

    _eexThreadIDSet(test_pri);
    g_timer_ms = 10;

    eexDelay(5);
    _eexThreadListAdd(&g_thread_waiting_list, test_pri);            // add to waiting list so timeout can find it
    TEST_ASSERT_EQUAL(0, eexThreadTimeout());
    g_timer_ms += 4;
    TEST_ASSERT_EQUAL(0, eexThreadTimeout());
    ++g_timer_ms;
    TEST_ASSERT_EQUAL(test_pri, eexThreadTimeout());

    g_all_tests_run = true;
}

static void thread(void * const argument) { }
void test_create_tasks(void) {
    eex_status_t err;
    for (int i=0; i<=EEX_CFG_USER_THREADS_MAX; ++i) {
        err = eexThreadCreate(thread, NULL, i, NULL);
        TEST_ASSERT_EQUAL(eexStatusOK, err);
        if (i) { TEST_ASSERT_TRUE(g_thread_ready_list & (1<<(i-1))); }  // added to ready list when created except for 0
    }
    err = eexThreadCreate(thread, NULL, 1, NULL);
    TEST_ASSERT_EQUAL(eexStatusThreadPriorityErr, err);
    err = eexThreadCreate(thread, NULL, EEX_CFG_USER_THREADS_MAX+1, NULL);
    TEST_ASSERT_EQUAL(eexStatusThreadCreateErr, err);

    g_all_tests_run = true;
}

void _eexEventInit(void *yield_pt, eex_status_t *p_rtn_status, uint32_t *p_rtn_val, uint32_t timeout, uint32_t val, eex_kobj_cb_t *p_kobj, eex_event_action_t action);
void _eexEventRemove(eex_thread_id_t tid, eex_thread_event_t *event, eex_status_t status);
void test_event_init_remove(void) {
    eex_thread_event_t *event, int_event;
    eex_thread_id_t     test_pri = EEX_CFG_USER_THREADS_MAX;
    eex_status_t        rtn_status;
    uint32_t            rtn_val;
    eex_thread_cb_t    *tcb;

    // THREAD MODE TEST
    // initialize a semaphore event
    _eexThreadIDSet(test_pri);
    g_timer_ms = 10;

    tcb = eexThreadTCB(test_pri);
    event = &(tcb->event);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, (eex_kobj_cb_t *) sema_10_10, EEX_EVENT_PEND);

    TEST_ASSERT_EQUAL(0xabcd1234, tcb->resume_pc);
    TEST_ASSERT_EQUAL(&rtn_status, event->rslt);
    TEST_ASSERT_EQUAL(&rtn_val, event->p_val);
    TEST_ASSERT_EQUAL(10 + 5, event->timeout);
    TEST_ASSERT_EQUAL(1, event->val);
    TEST_ASSERT_EQUAL((eex_kobj_cb_t *) sema_10_10, event->kobj);
    TEST_ASSERT_EQUAL(1, _eexBMState(&(((eex_kobj_cb_t *) sema_10_10)->pend), test_pri));
    TEST_ASSERT_EQUAL(EEX_EVENT_PEND, event->action);
    TEST_ASSERT_EQUAL(eexStatusInvalid, rtn_status);


    // INTERRUPT TEST
    g_mock_interrupt_level = 14;
    g_timer_ms = 100;
    _eexEventInit((void *) &int_event, &rtn_status, &rtn_val, 0, 2, (eex_kobj_cb_t *) sema_10_10, EEX_EVENT_POST);
    TEST_ASSERT_EQUAL(&rtn_status, int_event.rslt);
    TEST_ASSERT_EQUAL(&rtn_val, int_event.p_val);
    TEST_ASSERT_EQUAL(0, int_event.timeout);
    TEST_ASSERT_EQUAL(2, int_event.val);
    TEST_ASSERT_EQUAL((eex_kobj_cb_t *) sema_10_10, int_event.kobj);
    TEST_ASSERT_EQUAL(EEX_EVENT_POST, int_event.action);
    TEST_ASSERT_EQUAL(eexStatusInvalid, rtn_status);


    // Remove thread event
    _eexEventRemove(test_pri, event, eexStatusTimerNotFound);
    TEST_ASSERT_EQUAL(0, ((eex_kobj_cb_t *) sema_10_10)->pend);
    TEST_ASSERT_NULL(event->kobj);
    TEST_ASSERT_EQUAL(EEX_EVENT_NO_ACTION, event->action);
    TEST_ASSERT_EQUAL(0, event->timeout);
    TEST_ASSERT_EQUAL(eexStatusTimerNotFound, rtn_status);

    // Remove interrupt event
    _eexEventRemove(test_pri, &int_event, eexStatusBlockErr);
    TEST_ASSERT_NULL(int_event.kobj);
    TEST_ASSERT_EQUAL(EEX_EVENT_NO_ACTION, int_event.action);
    TEST_ASSERT_EQUAL(0, int_event.timeout);
    TEST_ASSERT_EQUAL(eexStatusBlockErr, rtn_status);

    g_all_tests_run = true;
}

bool _eexSignalTry(eex_thread_event_t *event);
void test_signal_try(void) {
    eex_thread_id_t     test_pri = EEX_CFG_USER_THREADS_MAX;
    eex_thread_event_t *event    = &(eexThreadTCB(test_pri)->event);
    eex_status_t        rtn_status;
    uint32_t            rtn_val;

    _eexThreadIDSet(test_pri);

    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0xffffffff, sig, EEX_EVENT_PEND);    // mask = 0xffffffff
    TEST_ASSERTION_SHOULD_NOT_ASSERT(_eexSignalTry(event)); TEST_ASSERT_EQUAL(0, rtn_val);            // initial value
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sig, EEX_EVENT_POST);             // signal now 0x00000001
    TEST_ASSERTION_SHOULD_NOT_ASSERT(_eexSignalTry(event));
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sig, EEX_EVENT_NO_ACTION);
    TEST_ASSERTION_SHOULD_ASSERT(_eexSignalTry(event));

    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0x10101010, sig, EEX_EVENT_POST);    // signal now 0x10101011
    TEST_ASSERT_TRUE(_eexSignalTry(event));
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sig, EEX_EVENT_PEND);             // set mask = 0
    TEST_ASSERT_FALSE(_eexSignalTry(event));          TEST_ASSERT_EQUAL(0, rtn_val);                  // 0x10101011 & 0 = 0
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0x00000010, sig, EEX_EVENT_PEND);    // mask = 0x00000010
    TEST_ASSERT_TRUE(_eexSignalTry(event));           TEST_ASSERT_EQUAL(0x00000010, rtn_val);         // signal now 0x10101001
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0x01010101, sig, EEX_EVENT_PEND);    // mask = 0x01010101
    TEST_ASSERT_TRUE(_eexSignalTry(event));          TEST_ASSERT_EQUAL(1, rtn_val);                   // signal now 0x10101000
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0xffffffff, sig, EEX_EVENT_PEND);    // mask = 0xffffffff
    TEST_ASSERT_TRUE(_eexSignalTry(event));           TEST_ASSERT_EQUAL(0x10101000, rtn_val);         // signal now 0
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0xffffffff, sig, EEX_EVENT_PEND);    // mask = 0xffffffff
    TEST_ASSERT_FALSE(_eexSignalTry(event));          TEST_ASSERT_EQUAL(0, rtn_val);                  // signal now 0

    g_all_tests_run = true;
}

bool _eexSemaMutexTry(eex_thread_event_t *event);
void test_semaphore_try(void) {
    eex_thread_id_t     test_pri = EEX_CFG_USER_THREADS_MAX;
    eex_thread_event_t *event    = &(eexThreadTCB(test_pri)->event);
    eex_status_t        rtn_status;
    uint32_t            rtn_val;

    _eexThreadIDSet(test_pri);

    TEST_ASSERT_EQUAL('SEMA', ((eex_sema_mutex_cb_t *) sema_10_10)->cb.type);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_NO_ACTION);
    TEST_ASSERTION_SHOULD_ASSERT(_eexSemaMutexTry(event));                                          // must be pend or post
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_PEND);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));       TEST_ASSERT_EQUAL_UINT16(9, rtn_val);          // decrement
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_POST);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));       TEST_ASSERT_EQUAL_UINT16(10, rtn_val);         // increment is always successful
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_PEND);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));       TEST_ASSERT_EQUAL_UINT16(9, rtn_val);          // decrement
    for (int i=8; i>=0; --i) {                                                                      // decrement to zero
        _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_PEND);
        TEST_ASSERT_TRUE(_eexSemaMutexTry(event));   TEST_ASSERT_EQUAL_UINT16(i, rtn_val);          // always successful
    }
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_PEND);
    TEST_ASSERT_FALSE(_eexSemaMutexTry(event));      TEST_ASSERT_EQUAL_UINT16(0, rtn_val);          // can't decrement, counter = 0
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_POST);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));       TEST_ASSERT_EQUAL_UINT16(1, rtn_val);          // inc
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, sema_10_10, EEX_EVENT_PEND);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));       TEST_ASSERT_EQUAL_UINT16(0, rtn_val);          // dec

    g_all_tests_run = true;
}

void test_mutex_try(void) {
    eex_thread_event_t *event    = &(eexThreadTCB(EEX_CFG_USER_THREADS_MAX)->event);
    eex_status_t        rtn_status;
    uint32_t            rtn_val;

    _eexThreadIDSet(EEX_CFG_USER_THREADS_MAX);

    TEST_ASSERT_EQUAL('MUTX', ((eex_sema_mutex_cb_t *) mutex)->cb.type);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_NO_ACTION);
    TEST_ASSERTION_SHOULD_ASSERT(_eexSemaMutexTry(event));
    TEST_ASSERT_EQUAL(0, ((eex_sema_mutex_cb_t *) mutex)->owner_id);                          // mutex available, no owner
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_PEND);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));          TEST_ASSERT_EQUAL_UINT16(0, rtn_val); // acquire
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_POST);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));          TEST_ASSERT_EQUAL_UINT16(1, rtn_val); // release
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_PEND);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));          TEST_ASSERT_EQUAL_UINT16(0, rtn_val); // acquire
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_PEND);
    TEST_ASSERT_FALSE(_eexSemaMutexTry(event));         TEST_ASSERT_EQUAL_UINT16(0, rtn_val); // mutex already taken
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_POST);
    TEST_ASSERT_TRUE(_eexSemaMutexTry(event));          TEST_ASSERT_EQUAL_UINT16(1, rtn_val); // release
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 0, mutex, EEX_EVENT_POST);
    TEST_ASSERTION_SHOULD_ASSERT(_eexSemaMutexTry(event));                                    // recursive mutexes are NOT supported

    g_all_tests_run = true;
}

eex_thread_id_t _eexEventTry(eex_thread_id_t evt_thread_priority, const eex_thread_event_t *event);
void test_event_try(void) {
    eex_thread_event_t *event, int_event;
    eex_thread_id_t     tid, test_pri = EEX_CFG_USER_THREADS_MAX;
    eex_status_t        rtn_status;
    uint32_t            rtn_val;
    eex_thread_cb_t    *tcb;

    // try a semaphore event
    // sema_10_10 is 0 from test_semaphore_try, above
    // thread mode
    _eexThreadIDSet(test_pri);
    g_timer_ms = 10;
    tcb = eexThreadTCB(test_pri);
    event = &(tcb->event);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sema_10_10, EEX_EVENT_PEND);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(0, tid);            // pend unsuccessful, timeout != 0, block
    // would normally block so there is no rtn_status or rtn_val until it unblocks
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sema_10_10, EEX_EVENT_POST);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // post successful
    TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
    TEST_ASSERT_EQUAL(1, rtn_val);        // semaphore value
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sema_10_10, EEX_EVENT_PEND);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // pend successful
    TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
    TEST_ASSERT_EQUAL(0, rtn_val);        // semaphore value

    // interrupt mode
    g_mock_interrupt_level = 14;
    g_timer_ms = 100;
    event = &int_event;
    _eexEventInit((void *) event, &rtn_status, &rtn_val, 0, 1, sema_10_10, EEX_EVENT_PEND);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // pend unsuccessful but nonblocking
    TEST_ASSERT_EQUAL(eexStatusEventNotReady, rtn_status);
    _eexEventInit((void *) event, &rtn_status, &rtn_val, 0, 1, sema_10_10, EEX_EVENT_POST);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // post successful
    TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
    _eexEventInit((void *) event, &rtn_status, &rtn_val, 0, 1, sema_10_10, EEX_EVENT_PEND);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // pend successful
    TEST_ASSERT_EQUAL(eexStatusOK , rtn_status);
    TEST_ASSERT_EQUAL(0, rtn_val);        // semaphore value

    // unblock a higher priority thread
    _eexThreadIDSet(test_pri);
    g_mock_interrupt_level = 0;  // thread mode
    g_timer_ms = 1000;
    tcb = eexThreadTCB(test_pri);
    event = &(tcb->event);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sema_10_10, EEX_EVENT_PEND);
    tid = _eexEventTry(test_pri, event);
    TEST_ASSERT_EQUAL(0, tid);            // pend unsuccessful, timeout != 0, block

    _eexThreadIDSet(test_pri-1);          // post from a lower priority thread
    tcb = eexThreadTCB(test_pri-1);
    event = &(tcb->event);
    _eexEventInit((void *) 0xabcd1234, &rtn_status, &rtn_val, 5, 1, sema_10_10, EEX_EVENT_POST);
    tid = _eexEventTry(test_pri-1, event);
    TEST_ASSERT_EQUAL(test_pri, tid);     // post successful, higher priority thread unblocked

    g_all_tests_run = true;
}

void test_thread0_block(void) {
    eex_status_t        rtn_status;
    uint32_t            rtn_val;

    _eexThreadIDSet(0);
    eexPendSignal(&rtn_status, &rtn_val, 1, 0xffffffff, sig);    // non-zero timeout
    TEST_ASSERT_EQUAL(eexStatusBlockErr, rtn_status);
    g_all_tests_run = true;
}

void test_scheduler(void) {
    eex_thread_id_t     test_pri = EEX_CFG_USER_THREADS_MAX-2;
    eex_thread_cb_t    *tcb;

    // thread interrupted
    _eexThreadIDSet(test_pri);
    g_thread_ready_list = 1 << (EEX_CFG_USER_THREADS_MAX-1);
    tcb = eexScheduler(true);
    TEST_ASSERT_TRUE((1 << (test_pri-1)) & g_thread_interrupted_list);  // thread test_pri interrupted
    TEST_ASSERT_EQUAL(eexThreadTCB(EEX_CFG_USER_THREADS_MAX), tcb);     // thread EEX_CFG_USER_THREADS_MAX scheduled

    // thread blocked
    _eexThreadIDSet(test_pri);                                          // previously set to EEX_CFG_USER_THREADS_MAX by call to eexScheduler
    g_thread_interrupted_list = EEX_EMPTY_THREAD_LIST;
    g_thread_ready_list = 1 << (EEX_CFG_USER_THREADS_MAX-1);
    eexThreadTCB(test_pri)->event.action = EEX_EVENT_POST;              // thread event is pending
    tcb = eexScheduler(false);
    TEST_ASSERT_TRUE((1 << (test_pri-1)) & g_thread_waiting_list);      // thread test_pri blocked
    TEST_ASSERT_EQUAL(eexThreadTCB(EEX_CFG_USER_THREADS_MAX), tcb);     // thread EEX_CFG_USER_THREADS_MAX scheduled

    // thread not blocked, but released a higher priority thread
    _eexThreadIDSet(test_pri);                                          // previously set to EEX_CFG_USER_THREADS_MAX by call to eexScheduler
    g_thread_waiting_list = EEX_EMPTY_THREAD_LIST;
    g_thread_ready_list = 1 << (EEX_CFG_USER_THREADS_MAX-1);
    eexThreadTCB(test_pri)->event.action = EEX_EVENT_NO_ACTION;         // thread event was successful and removed, free a higher priority thread
    tcb = eexScheduler(false);
    TEST_ASSERT_TRUE((1 << (test_pri-1)) & g_thread_ready_list);        // thread test_pri ready but preempted
    TEST_ASSERT_EQUAL(eexThreadTCB(EEX_CFG_USER_THREADS_MAX), tcb);     // thread EEX_CFG_USER_THREADS_MAX scheduled

    g_all_tests_run = true;
}














