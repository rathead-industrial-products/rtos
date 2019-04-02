
#include  <stddef.h>
#include  <sam.h>
#include  "eex_os.h"
//#include  "eex_kernel.h"

volatile int ctr;

int main(void){
    PendSV_Handler();

    for (;;) {
        ++ctr;
    }
}

eex_thread_fn_t eexScheduler(void) {
    return ((eex_thread_fn_t) 0);
}


void PendSV_Handler(void) __attribute__(( naked ));
void PendSV_Handler(void) {
    /*
     * The handler may be entered one of three ways:
     *    In thread mode as a function call, as on the initial entry to the scheduler from main()
     *    In thread mode as the return address when a thread completes
     *    In handler mode as the exception handler for the PendSV interrupt
     *
     * In thread mode, either the RTOS thread is complete, or the function call
     * will never return. Therefore no registers need to be preserved.
     *
     * In handler mode, the scratch registers are on the stack and therefore
     * do not need to be preserved.
     *
     * If in thread mode, transition to handler mode through a PendSV interrupt.
     *
     */

    __asm volatile (
        ".syntax  unified"            "\n\t"
        "mrs r0, ipsr"                "\n\t"    // in thread or handler mode?
        "bne Lin_handler_mode"        "\n\t"    // already in handler mode
                                                // in thread mode, set pendSV
        "ldr  r0, =0xe000ed00"        "\n\t"    // System Control Block Base address
        "movs r1, #1"                 "\n\t"
        "lsls r1, r1, #28"            "\n\t"    // SCB_ICSR_PENDSVSET_Pos
        "str  r1, [r0, #4]"           "\n\t"    // Interrupt Control and State Register
                                                // pendSV exception taken here

        // if handler entered from thread mode, we are now in handler mode and stacked PC will point here
        "Lin_handler_mode:"           "\n\t"

        "ldr  r0, [sp, #24]"          "\n\t"    // get pc on stack
        "ldr  r1, =Lin_handler_mode"  "\n\t"    // was exception triggered by thread mode entry?
        "cmp  r0, r1"                 "\n\t"
        "bne  Lstack_clean"           "\n\t"    // was in handler mode on entry, keep exception frame
        "add  sp, sp, #32"            "\n\t"    // remove exception frame
        "Lstack_clean:"               "\n\t"
    );

    /*
     * Now in handler mode with stack cleaned up and ready to run the scheduler.
     * On return from eexScheduler()
     *    r0 = 0 if return from interrupt
     *    r0 = thread entry point if jump to thread
     *    r1 = parameter to pass to thread function
     *
     * If jump to thread
     *    allocate exception frame on stack
     *    SP->LR = eexPendSVHandler
     *    LR = return to thread mode
     */

    __asm volatile (
        "bl   eexScheduler"           "\n\t"
        "cmp  r0, #0"                 "\n\t"
        "beq  Lreturn"                "\n\t"    // hpt thread is on top of interrupt stack
        "sub  sp, sp, #32"            "\n\t"    // create exception frame
        "str  r1, [sp, #0]"           "\n\t"    // #0 r0 is the function argument
        "str  r0, [sp, #24]"          "\n\t"    // #24 pc is thread function entry point
        "ldr  r0, =PendSV_Handler"    "\n\t"    // come back here when thread completes
        "str  r0, [sp, #20]"          "\n\t"    // #20 lr holds function return address
        "ldr  r0, =0xFFFFFFF9"        "\n\t"    // signal this is a return from exception, use MSP
        "mov  lr, r0"                 "\n\t"
        "Lreturn:"                    "\n\t"
        "bx lr"                       "\n\t"
    );
}
