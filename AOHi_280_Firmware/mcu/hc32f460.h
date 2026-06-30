/* hc32f460.h - HC32F460xE register map (the peripherals this SDK uses).
 *
 * Clean-room replacement for the vendor device header. Only the peripheral
 * instances and register bitfields the firmware actually touches are described
 * (PORT, SPI, USART, RTC, SWDT, EFM, MSTP/FCG, INTC, SYSREG/CMU). Bitfield
 * layouts are the silicon's documented register fields; the driver logic that
 * uses them is implemented from scratch in mcu_*.c. */
#ifndef HC32F460_H
#define HC32F460_H

#include "mcu_core.h"
#include <stdint.h>

#ifndef __IO
#define __IO volatile
#define __I  volatile const
#define __O  volatile
#endif

/* ============================ GPIO / PORT ============================ *
 * Base 0x40053800. Per-port bit registers at +0x10*port; the PCR/PFSR pin
 * control block starts at +0x400 (PCRA0). The drivers reach the bit and PCR
 * registers through address macros (see mcu_gpio.c); the struct here exposes
 * PCRA0 because display.c indexes off &M4_PORT->PCRA0 directly. */
typedef struct
{
    uint8_t  RESERVED0[0x400];
    __IO uint16_t PCRA0;          /* 0x40053C00 : pin control, port A pin 0 */
} M4_PORT_TypeDef;
#define M4_PORT   ((M4_PORT_TypeDef *)0x40053800UL)

/* ============================== SPI ================================= */
typedef struct { __IO uint32_t SPIMDS:1,TXMDS:1,RES2:1,MSTR:1,SPLPBK:1,SPLPBK2:1,
                 SPE:1,CSUSPE:1,EIE:1,TXIE:1,RXIE:1,IDIE:1,MODFE:1,PATE:1,PAOE:1,
                 PAE:1,RES16:16; } stc_spi_cr1_field_t;
typedef struct { __IO uint32_t FTHLV:2,RES2:4,SPRDTD:1,RES7:1,SS0PV:1,SS1PV:1,
                 SS2PV:1,SS3PV:1,RES12:8,MSSI:3,RES23:1,MSSDL:3,RES27:1,MIDI:3,
                 RES31:1; } stc_spi_cfg1_field_t;
typedef struct { __IO uint32_t OVRERF:1; __I uint32_t IDLNF:1; __IO uint32_t MODFERF:1,
                 PERF:1,UDRERF:1,TDEF:1,RES6:1,RDFF:1,RES8:24; } stc_spi_sr_field_t;
typedef struct { __IO uint32_t CPHA:1,CPOL:1,MBR:3,SSA:3,DSIZE:4,LSBF:1,MIDIE:1,
                 MSSDLE:1,MSSIE:1,RES16:16; } stc_spi_cfg2_field_t;

typedef struct
{
    __IO uint32_t DR;                                   /* 0x00 data           */
    union { __IO uint32_t CR1;  stc_spi_cr1_field_t  CR1_f;  };   /* 0x04 */
    uint8_t RESERVED0[4];
    union { __IO uint32_t CFG1; stc_spi_cfg1_field_t CFG1_f; };   /* 0x0C */
    uint8_t RESERVED1[4];
    union { __IO uint32_t SR;   stc_spi_sr_field_t   SR_f;   };   /* 0x14 */
    union { __IO uint32_t CFG2; stc_spi_cfg2_field_t CFG2_f; };   /* 0x18 */
} M4_SPI_TypeDef;
#define M4_SPI1   ((M4_SPI_TypeDef *)0x4001C000UL)
#define M4_SPI3   ((M4_SPI_TypeDef *)0x40020000UL)

/* ============================= USART =============================== */
typedef struct { __I uint32_t PE:1,FE:1,RES2:1,ORE:1,RES4:1,RXNE:1,TC:1,TXE:1,
                 RTOF:1,RES9:7; __I uint32_t MPB:1; uint32_t RES17:15; } stc_usart_sr_field_t;
typedef struct { __IO uint32_t TDR:9,MPID:1,RES10:6,RDR:9,RES25:7; } stc_usart_dr_field_t;
typedef struct { __IO uint32_t RTOE:1,RTOIE:1,RE:1,TE:1,SLME:1,RIE:1,TCIE:1,TXEIE:1,
                 RES8:1,PS:1,PCE:1,RES11:1,M:1,RES13:2,OVER8:1; __O uint32_t CPE:1,CFE:1;
                 uint32_t RES18:1; __O uint32_t CORE:1,CRTOF:1; uint32_t RES21:3;
                 __IO uint32_t MS:1; uint32_t RES25:3; __IO uint32_t ML:1,FBME:1,NFE:1,
                 SBS:1; } stc_usart_cr1_field_t;
typedef struct { __IO uint32_t MPE:1,RES1:10,CLKC:2,STOP:1,RES14:18; } stc_usart_cr2_field_t;
typedef struct { uint32_t RES0:5; __IO uint32_t SCEN:1; uint32_t RES6:3; __IO uint32_t CTSE:1;
                 uint32_t RES10:11; __IO uint32_t BCN:3; uint32_t RES24:8; } stc_usart_cr3_field_t;
typedef struct { __IO uint32_t PSC:2,RES2:30; } stc_usart_pr_field_t;
typedef struct { __IO uint32_t DIV_FRACTION:7,RES7:1,DIV_INTEGER:8,RES16:16; } stc_usart_brr_field_t;

typedef struct
{
    union { __IO uint32_t SR;  stc_usart_sr_field_t  SR_f;  };    /* 0x00 */
    union { __IO uint32_t DR;  stc_usart_dr_field_t  DR_f;  };    /* 0x04 */
    union { __IO uint32_t BRR; stc_usart_brr_field_t BRR_f; };    /* 0x08 */
    union { __IO uint32_t CR1; stc_usart_cr1_field_t CR1_f; };    /* 0x0C */
    union { __IO uint32_t CR2; stc_usart_cr2_field_t CR2_f; };    /* 0x10 */
    union { __IO uint32_t CR3; stc_usart_cr3_field_t CR3_f; };    /* 0x14 */
    union { __IO uint32_t PR;  stc_usart_pr_field_t  PR_f;  };    /* 0x18 */
} M4_USART_TypeDef;
#define M4_USART1   ((M4_USART_TypeDef *)0x4001D000UL)

/* ============================== RTC ================================ *
 * 8-bit registers on a 4-byte stride. Calendar regs (SEC..YEAR) are read/written
 * directly by hal/rtc.c; the control regs are driven by the RTC driver. */
typedef struct { __IO uint8_t RESET:1,RES1:7; } stc_rtc_cr0_field_t;
typedef struct { __IO uint8_t PRDS:3,AMPM:1,ALMFCLR:1,ONEHZOE:1,ONEHZSEL:1,START:1; } stc_rtc_cr1_field_t;
typedef struct { __IO uint8_t RWREQ:1,RWEN:1,RES2:1,ALMF:1,RES4:1,PRDIE:1,ALMIE:1,ALME:1; } stc_rtc_cr2_field_t;
typedef struct { uint8_t RES0:4; __IO uint8_t LRCEN:1; uint8_t RES5:2; __IO uint8_t RCKSEL:1; } stc_rtc_cr3_field_t;
typedef struct { __IO uint8_t COMP8:1; uint8_t RES1:6; __IO uint8_t COMPEN:1; } stc_rtc_errcrh_field_t;

typedef struct
{
    union { __IO uint8_t CR0; stc_rtc_cr0_field_t CR0_f; }; uint8_t RES0[3];   /* 0x00 */
    union { __IO uint8_t CR1; stc_rtc_cr1_field_t CR1_f; }; uint8_t RES1[3];   /* 0x04 */
    union { __IO uint8_t CR2; stc_rtc_cr2_field_t CR2_f; }; uint8_t RES2[3];   /* 0x08 */
    union { __IO uint8_t CR3; stc_rtc_cr3_field_t CR3_f; }; uint8_t RES3[3];   /* 0x0C */
    __IO uint8_t SEC;  uint8_t RES4[3];                                        /* 0x10 */
    __IO uint8_t MIN;  uint8_t RES5[3];                                        /* 0x14 */
    __IO uint8_t HOUR; uint8_t RES6[3];                                        /* 0x18 */
    __IO uint8_t WEEK; uint8_t RES7[3];                                        /* 0x1C */
    __IO uint8_t DAY;  uint8_t RES8[3];                                        /* 0x20 */
    __IO uint8_t MON;  uint8_t RES9[3];                                        /* 0x24 */
    __IO uint8_t YEAR; uint8_t RES10[3];                                       /* 0x28 */
    uint8_t RES11[12];                                                         /* 0x2C */
    union { __IO uint8_t ERRCRH; stc_rtc_errcrh_field_t ERRCRH_f; }; uint8_t RES12[3]; /* 0x38 */
    __IO uint8_t ERRCRL; uint8_t RES13[3];                                     /* 0x3C */
} M4_RTC_TypeDef;
#define M4_RTC   ((M4_RTC_TypeDef *)0x4004C000UL)

/* ============================== SWDT =============================== */
typedef struct
{
    uint8_t RESERVED0[4];
    __IO uint32_t SR;     /* 0x04 */
    __IO uint32_t RR;     /* 0x08 refresh */
} M4_SWDT_TypeDef;
#define M4_SWDT   ((M4_SWDT_TypeDef *)0x40049400UL)

/* ============================== EFM ================================ */
typedef struct { __IO uint32_t PEMODE:1,RES1:3,PEMOD:3,RES7:1,BUSHLDCTL:1,RES9:23; } stc_efm_fwmc_field_t;
typedef struct { __I uint32_t PEWERR:1,PEPRTERR:1,PGSZERR:1,PGMISMTCH:1,OPTEND:1,COLERR:1,
                 RES6:2,RDY:1,RES9:23; } stc_efm_fsr_field_t;
typedef struct { __IO uint32_t SLPMD:1,RES1:3,FLWT:4,LVM:1,RES9:7,CACHE:1,RES17:7,CRST:1,RES25:7; } stc_efm_frmc_field_t;

typedef struct
{
    __IO uint32_t FAPRT;                                /* 0x00 */
    __IO uint32_t FSTP;                                 /* 0x04 */
    union { __IO uint32_t FRMC; stc_efm_frmc_field_t FRMC_f; };   /* 0x08 */
    union { __IO uint32_t FWMC; stc_efm_fwmc_field_t FWMC_f; };   /* 0x0C */
    union { __IO uint32_t FSR;  stc_efm_fsr_field_t  FSR_f;  };   /* 0x10 */
    __IO uint32_t FSCLR;                                /* 0x14 */
} M4_EFM_TypeDef;
#define M4_EFM   ((M4_EFM_TypeDef *)0x40010400UL)

/* ===================== MSTP (peripheral clock gate) ================ */
typedef struct
{
    __IO uint32_t FCG0;   /* 0x00 */
    __IO uint32_t FCG1;   /* 0x04 */
    __IO uint32_t FCG2;   /* 0x08 */
    __IO uint32_t FCG3;   /* 0x0C */
} M4_MSTP_TypeDef;
#define M4_MSTP   ((M4_MSTP_TypeDef *)0x40048000UL)

/* ===================== INTC (shared interrupt select) ============== */
typedef struct { __IO uint32_t INTSEL:9,RES9:23; } stc_intc_sel_field_t;
typedef struct
{
    uint8_t RESERVED0[0x5C];
    union { __IO uint32_t SEL0; stc_intc_sel_field_t SEL0_f; };   /* 0x5C */
} M4_INTC_TypeDef;
#define M4_INTC   ((M4_INTC_TypeDef *)0x40051000UL)

/* ===================== SYSREG / CMU (clock readback) ============== */
typedef struct { __IO uint32_t PCLK0S:3,RES3:1,PCLK1S:3,RES7:1,PCLK2S:3,RES11:1,
                 PCLK3S:3,RES15:1,PCLK4S:3,RES19:1,EXCKS:3,RES23:1,HCLKS:3,RES27:5; } stc_cmu_scfgr_field_t;
typedef struct { __IO uint8_t CKSW:3,RES3:5; } stc_cmu_ckswr_field_t;
typedef struct { __IO uint32_t MPLLM:5,RES5:2,PLLSRC:1,MPLLN:9,RES17:3,MPLLR:4,
                 MPLLQ:4,MPLLP:4; } stc_cmu_pllcfgr_field_t;

typedef struct
{
    uint8_t RESERVED0[0x20];
    union { __IO uint32_t CMU_SCFGR; stc_cmu_scfgr_field_t CMU_SCFGR_f; };  /* 0x20 */
    uint8_t RESERVED1[2];
    union { __IO uint8_t CMU_CKSWR; stc_cmu_ckswr_field_t CMU_CKSWR_f; };   /* 0x26 */
    uint8_t RESERVED2[0x100 - 0x27];
    union { __IO uint32_t CMU_PLLCFGR; stc_cmu_pllcfgr_field_t CMU_PLLCFGR_f; }; /* 0x100 */
} M4_SYSREG_TypeDef;
#define M4_SYSREG   ((M4_SYSREG_TypeDef *)0x40054000UL)

#endif /* HC32F460_H */
