/* board.h - AOHi board pin map for the HC32F460xE, expressed in DDL terms.
 *
 * Derived from the verified-working hand-rolled HAL (sdk/hal). The old code used
 * raw port indices 0/1 = DDL PortA/PortB and raw GPIO/alt-func register writes; here
 * they are mapped to the DDL en_port_t / en_pin_t / en_port_func_t so the rewritten
 * HAL can use PORT_Init / PORT_SetFunc / SPI_Init etc.
 *
 * LCD bus = SPI3 (old SPI0 @0x40020000 == M4_SPI3). Pixels stream MSB-first.
 * External image flash = SPI1 (PortA, full-duplex).
 */
#ifndef BOARD_H
#define BOARD_H
#include <hc32_ddl.h>

/* ---- LCD control lines (GPIO outputs) ---- */
#define LCD_CS_PORT    PortB
#define LCD_CS_PIN     Pin01        /* old port1 0x0002 */
#define LCD_RST_PORT   PortA
#define LCD_RST_PIN    Pin08        /* old port0 0x0100 */
#define LCD_DC_PORT    PortB
#define LCD_DC_PIN     Pin15        /* old port1 0x8000 */

/* ---- LCD SPI3 pins (alternate function) ----
 * The verified hand-rolled init set: PB14 -> func 0x2B (Func_Spi3_Sck=43),
 * PB13 -> func 0x28 (Func_Spi3_Mosi=40). Replicate those exact func values. */
#define LCD_SPI         M4_SPI3
#define LCD_SPI_SCK_PORT  PortB
#define LCD_SPI_SCK_PIN   Pin14
#define LCD_SPI_SCK_FUNC  Func_Spi3_Sck     /* 43 */
#define LCD_SPI_MOSI_PORT PortB
#define LCD_SPI_MOSI_PIN  Pin13
#define LCD_SPI_MOSI_FUNC Func_Spi3_Mosi    /* 40 */

/* ---- backlight: PWM on PA7 (AF3) from the timer @0x40018000 (stock sub_D44C).
 * For bring-up we drive PA7 as a GPIO; the proper DDL timer PWM is added in pwm.c. */
#define LCD_BL_PORT    PortA
#define LCD_BL_PIN     Pin07

/* ---- external image flash: 32MB SPI-NOR on SPI1, 4-byte addressing ----
 * Func values replicated from the verified hand-rolled init (PA12->43 Spi1_Sck,
 * PA11->40 Spi1_Mosi, PA10->41 Spi1_Miso). CS is a plain GPIO output on PA9. */
#define XF_SPI          M4_SPI1
#define XF_CS_PORT      PortA
#define XF_CS_PIN       Pin09
#define XF_SCK_PORT     PortA
#define XF_SCK_PIN      Pin12
#define XF_SCK_FUNC     Func_Spi1_Sck     /* 43 */
#define XF_MOSI_PORT    PortA
#define XF_MOSI_PIN     Pin11
#define XF_MOSI_FUNC    Func_Spi1_Mosi    /* 40 */
#define XF_MISO_PORT    PortA
#define XF_MISO_PIN     Pin10
#define XF_MISO_FUNC    Func_Spi1_Miso    /* 41 */

#endif /* BOARD_H */
