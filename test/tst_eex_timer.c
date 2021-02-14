//*******************************************************************************
 *    INCLUDED FILES
 ******************************************************************************/
//-- unity: unit test framework
#include "unity.h"
#include "assert_test_helpers.h"

//-- module being tested
#include "eex_timer.h"
#include "eex_platform_mock.c"

#pragma GCC diagnostic ignored "-Wmultichar"            // to allow e.g. 'MUTX'
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"  // console 64 bit pointers don't like cast to uint32_t
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"


/*******************************************************************************
 *    DEFINITIONS
 ******************************************************************************/

// Set eex_configure.h
// #define EEX_CFG_USER_THREADS_MAX  32

#define MUTEX_TEST_THREAD_PRI_H   10
#define MUTEX_TEST_THREAD_PRI_M   7
#define MUTEX_TEST_THREAD_PRI_L   6


/*******************************************************************************
 *    MODULE INTERNAL DATA
 ******************************************************************************/
extern eex_thread_cb_t    g_thread_tcb[EEX_CFG_USER_THREADS_MAX];
extern eex_thread_list_t  g_thread_ready_list;
extern eex_thread_list_t  g_thread_waiting_list;
extern eex_thread_list_t  g_thread_interrupted_list;
extern eex_thread_list_t  g_thread_running;

extern volatile uint32_t  g_timer_ms;
extern volatile uint32_t  g_timer_us;

/*******************************************************************************
 *    PRIVATE TYPES
 ******************************************************************************/

/*******************************************************************************
 *    PRIVATE DATA
 ******************************************************************************/
EEX_SEMAPHORE_NEW(sem1, 1, 0);
EEX_SEMAPHORE_NEW(sem2, 1, 0);
EEX_MUTEX_NEW(mutex);
EEX_SIGNAL_NEW(sig_wake_up);

bool  f_g_mutex_test_thread_pri_h_done = false;
bool  f_g_mutex_test_thread_pri_m_done = false;
bool  f_g_mutex_test_thread_pri_l_done = false;


/*******************************************************************************
 *    PRIVATE FUNCTIONS
 ******************************************************************************/
// simulate eexKernelStart function dispatching threads
void dispatch(bool from_interrupt) {
    eex_thread_cb_t * tcb;

    tcb = eexScheduler(from_interrupt);
    if (g_f_pend_scheduler) {    // rerun scheduler before dispatching thread
        g_f_pend_scheduler = false;
        tcb = eexScheduler(from_interrupt);
    }
    tcb->fn_thread(tcb->arg);
}

// increment the global timer by 1 ms when there are no threads ready to dispatch
uint32_t eexIdleHook(int32_t sleep_for_ms)  {
    return (1);
}

static bool g_f_thread;
static void thread_delay(void * const argument) {
    static int tid, r;

    eexThreadEntry();
    for (;;) {
        eexDelay((uint32_t) argument);
        g_f_thread = true;
    }
}

static void thread_semaphore_post(void * const argument) {
    static eex_status_t  rtn_status;
    static uint32_t      rtn_val;

    eexThreadEntry();
    for (;;) {
        eexPost(&rtn_status, 0, 0, sem1);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
    }
}

static void thread_semaphore_pend(void * const argument) {
    static eex_status_t  rtn_status;
    static uint32_t      rtn_val;

    eexThreadEntry();
    for (;;) {
        eexPend(&rtn_status, &rtn_val, eexWaitForever, sem1);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        eexPend(&rtn_status, &rtn_val, eexWaitForever, sem2);
    }
}

static void thread_mutex_H(void * const argument) {
    static eex_status_t  rtn_status;
    static uint32_t      rtn_val;

    eexThreadEntry();
    for (;;) {
        eexPendSignal(&rtn_status, &rtn_val, eexWaitForever, 1<<MUTEX_TEST_THREAD_PRI_H, sig_wake_up);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        TEST_ASSERT_EQUAL(1<<MUTEX_TEST_THREAD_PRI_H, rtn_val);
        eexPostSignal(&rtn_status, 1<<MUTEX_TEST_THREAD_PRI_M, sig_wake_up);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        eexPend(&rtn_status, NULL, eexWaitForever, mutex);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        f_g_mutex_test_thread_pri_h_done = true;
        eexDelay(eexWaitForever);
    }
}

static void thread_mutex_M(void * const argument) {
    static eex_status_t  rtn_status;
    static uint32_t      rtn_val;

    eexThreadEntry();
    for (;;) {
        eexPendSignal(&rtn_status, &rtn_val, eexWaitForever, 1<<MUTEX_TEST_THREAD_PRI_M, sig_wake_up);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        TEST_ASSERT_EQUAL(1<<MUTEX_TEST_THREAD_PRI_M, rtn_val);
        f_g_mutex_test_thread_pri_m_done = true;
        eexDelay(eexWaitForever);
    }
}

static void thread_mutex_L(void * const argument) {
    static eex_status_t  rtn_status;
    static uint32_t      rtn_val;

    eexThreadEntry();
    for (;;) {
        eexPend(&rtn_status, NULL, eexWaitForever, mutex);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        eexPostSignal(&rtn_status, 1<<MUTEX_TEST_THREAD_PRI_H, sig_wake_up);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        eexPost(&rtn_status, 0, 0, mutex);
        TEST_ASSERT_EQUAL(eexStatusOK, rtn_status);
        f_g_mutex_test_thread_pri_l_done = true;
        eexDelay(eexWaitForever);
    }
}


/*******************************************************************************
 *    SETUP, TEARDOWN
 ******************************************************************************/
void setUp(void) {
    (void) memset(g_thread_tcb, 0, sizeof(eex_thread_cb_t) * EEX_CFG_USER_THREADS_MAX);
    g_thread_ready_list       = 0;
    g_thread_waiting_list     = 0;
    g_thread_interrupted_list = 0;
    g_thread_running          = 0;
    g_timer_ms = 0;
    g_timer_us = 0;
}

void tearDown(void) { }

/*******************************************************************************
 *    TESTS
 ******************************************************************************/

void test_delay(void) {
    (void) eexThreadCreate(thread_delay, (void *) 5, 1, NULL);
    g_timer_ms = 0;
    while (!g_f_thread) {
        dispatch(false);
    }
    TEST_ASSERT_EQUAL(5, g_timer_ms);

}

void test_semaphore_all_threads(void) {
    /*
     * Pend all threads on a semaphore, then as thread 0 posts they will release in
     * priority order, blocking again on a second semaphore.
     */

    (void) eexThreadCreate(thread_semaphore_post, 0, 1, NULL);  // thread 1 releases all the other threads

    // create all possible working threads 32-2, then run and block on the semaphore sem1
    for (int i=1; i<=EEX_CFG_USER_THREADS_MAX; ++i) { (void) eexThreadCreate(thread_semaphore_pend, (void *) i, i, NULL); }
    TEST_ASSERT_EQUAL_HEX(0xffffffff, g_thread_ready_list);                 // all threads ready on creation
    TEST_ASSERT_EQUAL(0x00000000, g_thread_waiting_list);                   // nothing pended yet
    dispatch(false);                                                        // run thread 32, block
    dispatch(false);  TEST_ASSERT_EQUAL(0x80000000, g_thread_waiting_list); // put thread 32 on waiting list, schedule and run task 31, block
    dispatch(false);  TEST_ASSERT_EQUAL(0xc0000000, g_thread_waiting_list); // threads 32,31 on waiting list, schedule and run task 30, block
    for (int i=29; i>=2; --i) { dispatch(false); }                           // run and block threads 29-2, 2 not yet on waiting list
    TEST_ASSERT_EQUAL(0xfffffffc, g_thread_waiting_list);
    TEST_ASSERT_EQUAL(0xfffffffe, ((eex_sema_mutex_cb_t *) sem1)->cb.pend);  // all threads except 1 pending on sem1
    dispatch(false);    // thread 2 waiting, thread 1 dispatched to start posting and releasing the blocked threads
    TEST_ASSERT_EQUAL(0xfffffffe, g_thread_waiting_list); // all threads 32-2 now waiting

    dispatch(false);    // thread 32 has taken sem1 and run, is now blocked on sem2
    TEST_ASSERT_EQUAL(0x7ffffffe, ((eex_sema_mutex_cb_t *) sem1)->cb.pend);  // thread 32 no longer pending on sem1
    TEST_ASSERT_EQUAL(0x80000000, ((eex_sema_mutex_cb_t *) sem2)->cb.pend);  // thread 32 pending now on sem2
    dispatch(false);    // run thread 1 to post to sem1
    dispatch(false);    // thread 31 has taken sem1 and run, is now blocked on sem2
    TEST_ASSERT_EQUAL(0x3ffffffe, ((eex_sema_mutex_cb_t *) sem1)->cb.pend);  // thread 31 no longer pending on sem1
    TEST_ASSERT_EQUAL(0xc0000000, ((eex_sema_mutex_cb_t *) sem2)->cb.pend);  // thread 31 pending now on sem2
    for (int i=30; i>2; --i) { dispatch(false); dispatch(false); }
    TEST_ASSERT_EQUAL(0x00000002, ((eex_sema_mutex_cb_t *) sem1)->cb.pend);  // only thread 2 now pending on sem1
    TEST_ASSERT_EQUAL(0xfffffffc, ((eex_sema_mutex_cb_t *) sem2)->cb.pend);  // threads 3-32 pending now on sem2
    dispatch(false);    // run thread 1 to post to sem1 one last time, readying thread 2
    dispatch(false);    // run thread 2
    TEST_ASSERT_EQUAL(0x00000000, ((eex_sema_mutex_cb_t *) sem1)->cb.pend);  // no threads pending on sem1
    TEST_ASSERT_EQUAL(0xfffffffe, ((eex_sema_mutex_cb_t *) sem2)->cb.pend);  // all threads 32-2pending now on sem2
}

void test_mutex_priority_inversion(void) {
    (void) eexThreadCreate(thread_mutex_H, NULL, MUTEX_TEST_THREAD_PRI_H, NULL);
    (void) eexThreadCreate(thread_mutex_M, NULL, MUTEX_TEST_THREAD_PRI_M, NULL);
    (void) eexThreadCreate(thread_mutex_L, NULL, MUTEX_TEST_THREAD_PRI_L, NULL);

    dispatch(false);                                                              // dispatch high priority thread H
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_H, eexThreadID());                    // H running
    TEST_ASSERT_TRUE(g_thread_ready_list & (1 << (MUTEX_TEST_THREAD_PRI_M-1)));   // M ready
    TEST_ASSERT_TRUE(g_thread_ready_list & (1 << (MUTEX_TEST_THREAD_PRI_L-1)));   // L ready

    dispatch(false);                                                              // H blocks on signal, M dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_M, eexThreadID());                    // M running
    TEST_ASSERT_TRUE(g_thread_waiting_list & (1 << (MUTEX_TEST_THREAD_PRI_H-1))); // H waiting

    dispatch(false);                                                              // M blocks on signal, L dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_L, eexThreadID());                    // L running
    TEST_ASSERT_TRUE(g_thread_waiting_list & (1 << (MUTEX_TEST_THREAD_PRI_M-1))); // M waiting
                                                                                  // L acquires mutex

    dispatch(false);                                                              // L signals H and is preempted, H dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_H, eexThreadID());                    // H running
    TEST_ASSERT_TRUE(g_thread_ready_list & (1 << (MUTEX_TEST_THREAD_PRI_L-1)));   // L ready (was preempted, not blocked)
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_L,  ((eex_sema_mutex_cb_t *) mutex)->owner_id); // L owns mutex
                                                                                  // H signals M

    dispatch(false);                                                              // H blocks on mutex, L is hoisted and dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_L, eexThreadID());                    // L running
    TEST_ASSERT_TRUE(g_thread_waiting_list & (1 << (MUTEX_TEST_THREAD_PRI_H-1))); // H waiting
    TEST_ASSERT_TRUE(((eex_sema_mutex_cb_t *) mutex)->cb.pend & (1 << (MUTEX_TEST_THREAD_PRI_H-1))); // H pending on mutex
    TEST_ASSERT_EQUAL(0,  ((eex_sema_mutex_cb_t *) mutex)->owner_id);             // mutex released, has no owner

    dispatch(false);                                                              // L released mutex and was preempted, H dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_H, eexThreadID());                    // H running
    TEST_ASSERT_TRUE(g_thread_ready_list & (1 << (MUTEX_TEST_THREAD_PRI_L-1)));   // L ready (was preempted, not blocked)
                                                                                  // H acquires mutex
                                                                                  // H sets f_g_mutex_test_thread_pri_h_done

    dispatch(false);                                                              // H blocks on delay, M dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_M, eexThreadID());                    // M running
    TEST_ASSERT_TRUE(g_thread_waiting_list & (1 << (MUTEX_TEST_THREAD_PRI_H-1))); // H waiting
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_H,  ((eex_sema_mutex_cb_t *) mutex)->owner_id); // H owns mutex
    TEST_ASSERT_TRUE(f_g_mutex_test_thread_pri_h_done);                           // H done
                                                                                  // M sets f_g_mutex_test_thread_pri_m_done

    dispatch(false);                                                              // M blocks on delay, L dispatches
    TEST_ASSERT_EQUAL(MUTEX_TEST_THREAD_PRI_L, eexThreadID());                    // L running
    TEST_ASSERT_TRUE(g_thread_waiting_list & (1 << (MUTEX_TEST_THREAD_PRI_M-1))); // M waiting
    TEST_ASSERT_TRUE(f_g_mutex_test_thread_pri_m_done);                           // M done
                                                                                  // L sets f_g_mutex_test_thread_pri_l_done
}













