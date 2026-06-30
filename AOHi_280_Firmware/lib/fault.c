/* fault.c - DEBUG: capture the first HardFault's stacked frame into low .bss so
 * it survives the subsequent lockup and can be read out over SWD. */
#include <stdint.h>

/* Placed in .bss (low RAM, not corrupted by the rogue execution). */
volatile uint32_t g_fault[12];   /* magic,R0,R1,R2,R3,R12,LR,PC,xPSR,CFSR,HFSR,BFAR */

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst   lr, #4          \n"   /* EXC_RETURN bit2: 0=MSP,1=PSP */
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "b     hardfault_c     \n"
    );
}

void hardfault_c(uint32_t *frame)
{
    g_fault[0]  = 0xFA017ED0u;          /* magic so we know it ran */
    g_fault[1]  = frame[0];             /* R0  */
    g_fault[2]  = frame[1];             /* R1  */
    g_fault[3]  = frame[2];             /* R2  */
    g_fault[4]  = frame[3];             /* R3  */
    g_fault[5]  = frame[4];             /* R12 */
    g_fault[6]  = frame[5];             /* LR (caller return addr) */
    g_fault[7]  = frame[6];             /* PC (faulting instruction) */
    g_fault[8]  = frame[7];             /* xPSR */
    g_fault[9]  = *(volatile uint32_t *)0xE000ED28u;  /* CFSR */
    g_fault[10] = *(volatile uint32_t *)0xE000ED2Cu;  /* HFSR */
    g_fault[11] = *(volatile uint32_t *)0xE000ED38u;  /* BFAR */
    /* Mirror to the fixed diagnostic block (read over J-Link): magic, PC, CFSR. */
    *(volatile uint32_t *)0x1FFFC00Cu = 0xFA017ED0u;
    *(volatile uint32_t *)0x1FFFC010u = frame[6];     /* faulting PC */
    *(volatile uint32_t *)0x1FFFC014u = *(volatile uint32_t *)0xE000ED28u;  /* CFSR */
    for (;;) { }
}
