
#include  <stddef.h>
#include  <sam.h>
#include  "eex_os.h"
#include  "samd_clk.h"
#include  "samd_port.h"
#include  "samd_rtc.h"
#include  "logging.h"

#if (__CORTEX_M == 0)
#define LED       SAMD_PORT_PIN(SAMD_PORTA, 5)
#else
#define LED       SAMD_PORT_PIN(SAMD_PORTA, 23)
#define NEOPIXEL  SAMD_PORT_PIN(SAMD_PORTB,  3)
#endif


void resetNeoPixel(void) {
    samd_port_Init(NEOPIXEL, 0, SAMD_PORT_OUTPUT_ENABLE, SAMD_PORT_INPUT_ENABLE, SAMD_PORT_DRIVE_STRENGTH_NORMAL, SAMD_PORT_PULL_OFF, SAMD_PORT_PERIPHERAL_FUNC_GPIO);

    // drive low for 80 us
    // RESET

    // transmit 24 zeros
    // 0 high time - 0.3 us
    // 0 low time  - 0.9 us
    // determined empirically

    for (int i=0; i<1000; ++i) {
        SAMD_PORT_PIN_PUT(NEOPIXEL, 0);   // RESET, drive low for 80 us
    }

    for (int i=0; i<24; ++i) {
        SAMD_PORT_PIN_PUT(NEOPIXEL, 1);   // 0 high time = 0.3 us
        SAMD_PORT_PIN_PUT(NEOPIXEL, 0);   // 0 low time  = 0.9 us
    }
}

static int led_state = 0;

uint32_t eexIdleHook(int32_t sleep_for_ms) {
    uint32_t time_asleep;

    if (sleep_for_ms < 0)  {      // thread has timed out, return to scheduler
        return (0);
    }
    if (sleep_for_ms == 0) {      // no timeouts pending, sleep until a non-RTC interrupt occurs
        samd_RTCInterruptDisable();
        time_asleep = samd_RTCSleep();
    }
    else {                        // sleep until next thread timeout
        time_asleep = samd_RTCSleepFor((uint32_t) sleep_for_ms/4);
    }

    return (time_asleep);
}

static void thread_blink(void * const argument) {

    eexThreadEntry();
    for (;;) {
        led_state = !led_state;
        SAMD_PORT_PIN_PUT(LED, led_state);
        eexDelay(500);
    }
}

static void thread_count(void * const argument) {
    static int cnt = 0;

    eexThreadEntry();
    for (;;) {
        ++cnt;
        eexDelay(500);
    }
}

void main(void){
    samd_clkInit();
    samd_RTCInit();
    //samd_RTCInterruptEvery(10, NULL);
    samd_port_Init(LED, 0, SAMD_PORT_OUTPUT_ENABLE, SAMD_PORT_INPUT_ENABLE, SAMD_PORT_DRIVE_STRENGTH_NORMAL, SAMD_PORT_PULL_OFF, SAMD_PORT_PERIPHERAL_FUNC_GPIO);

    resetNeoPixel();    // turn neopixel off

    EEX_PROFILE_INIT;   // configure and init profiler (systemview)

    (void) eexThreadCreate(thread_blink, NULL, 1, "thread blink");
    //(void) eexThreadCreate(thread_count, NULL, 2, "thread count");


    eexKernelStart();

}


