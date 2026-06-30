/* mcu_core.h - minimal ARM Cortex-M4 core support for the HC32F460.
 *
 * Clean-room replacement for the CMSIS-Core headers the firmware actually used:
 * the SysTick timer, the NVIC enable/priority/pending helpers, the SCB CPACR
 * (FPU enable) and the handful of data-barrier intrinsics. Only what the SDK
 * touches is provided - this is not a full CMSIS-Core. All definitions follow
 * the public ARMv7-M architecture (Cortex-M4 r0p1, 4 NVIC priority bits, FPU). */
#ifndef MCU_CORE_H
#define MCU_CORE_H

#include <stdint.h>

#define __CM4_REV          1u
#define __MPU_PRESENT      1u
#define __NVIC_PRIO_BITS   4u
#define __FPU_PRESENT      1u
#ifndef __FPU_USED
#define __FPU_USED         1u
#endif

/* Exception / interrupt numbers. Negative = core exceptions, 0..143 = HC32F460
 * shared INTC lines (Int000_IRQn..Int143_IRQn). */
typedef enum IRQn
{
    NonMaskableInt_IRQn   = -14,
    HardFault_IRQn        = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn         = -11,
    UsageFault_IRQn       = -10,
    SVCall_IRQn           = -5,
    DebugMonitor_IRQn     = -4,
    PendSV_IRQn           = -2,
    SysTick_IRQn          = -1,
    Int000_IRQn           = 0,
    Int143_IRQn           = 143,
} IRQn_Type;

/* ---- compiler intrinsics (GCC inline asm) ---- */
#define __STATIC_INLINE static inline __attribute__((always_inline))

__STATIC_INLINE void __NOP(void) { __asm volatile ("nop"); }
__STATIC_INLINE void __DSB(void) { __asm volatile ("dsb 0xF":::"memory"); }
__STATIC_INLINE void __DMB(void) { __asm volatile ("dmb 0xF":::"memory"); }
__STATIC_INLINE void __ISB(void) { __asm volatile ("isb 0xF":::"memory"); }
__STATIC_INLINE void __enable_irq(void)  { __asm volatile ("cpsie i":::"memory"); }
__STATIC_INLINE void __disable_irq(void) { __asm volatile ("cpsid i":::"memory"); }

/* ---- System Control Block (subset: VTOR + CPACR for the FPU) ---- */
typedef struct
{
    volatile uint32_t CPUID;        /* 0x00 */
    volatile uint32_t ICSR;         /* 0x04 */
    volatile uint32_t VTOR;         /* 0x08 Vector Table Offset */
    volatile uint32_t RESERVED0[29];
    volatile uint32_t CPACR;        /* 0x88 Coprocessor Access Control */
} SCB_Type;
#define SCS_BASE   (0xE000E000UL)
#define SCB_BASE   (SCS_BASE + 0x0D00UL)
#define SCB        ((SCB_Type *)SCB_BASE)

#define SCB_VTOR_TBLOFF_Pos   7U
#define SCB_VTOR_TBLOFF_Msk   (0x1FFFFFFUL << SCB_VTOR_TBLOFF_Pos)

/* ---- SysTick ---- */
typedef struct
{
    volatile uint32_t CTRL;
    volatile uint32_t LOAD;
    volatile uint32_t VAL;
    volatile uint32_t CALIB;
} SysTick_Type;
#define SysTick_BASE   (SCS_BASE + 0x0010UL)
#define SysTick        ((SysTick_Type *)SysTick_BASE)

#define SysTick_CTRL_ENABLE_Msk      (1UL << 0)
#define SysTick_CTRL_TICKINT_Msk     (1UL << 1)
#define SysTick_CTRL_CLKSOURCE_Msk   (1UL << 2)

/* ---- NVIC (subset used: enable / clear-pending / set-priority) ---- */
typedef struct
{
    volatile uint32_t ISER[8];
    uint32_t RESERVED0[24];
    volatile uint32_t ICER[8];
    uint32_t RESERVED1[24];
    volatile uint32_t ISPR[8];
    uint32_t RESERVED2[24];
    volatile uint32_t ICPR[8];
    uint32_t RESERVED3[24];
    volatile uint32_t IABR[8];
    uint32_t RESERVED4[56];
    volatile uint8_t  IP[240];
} NVIC_Type;
#define NVIC_BASE   (SCS_BASE + 0x0100UL)
#define NVIC        ((NVIC_Type *)NVIC_BASE)

__STATIC_INLINE void NVIC_EnableIRQ(IRQn_Type IRQn)
{
    NVIC->ISER[((uint32_t)IRQn) >> 5] = (1UL << (((uint32_t)IRQn) & 0x1FUL));
}
__STATIC_INLINE void NVIC_ClearPendingIRQ(IRQn_Type IRQn)
{
    NVIC->ICPR[((uint32_t)IRQn) >> 5] = (1UL << (((uint32_t)IRQn) & 0x1FUL));
}
__STATIC_INLINE void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority)
{
    /* device IRQ: priority left-justified into the implemented high bits */
    NVIC->IP[(uint32_t)IRQn] =
        (uint8_t)((priority << (8u - __NVIC_PRIO_BITS)) & 0xFFu);
}

#endif /* MCU_CORE_H */
