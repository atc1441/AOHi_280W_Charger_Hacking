/* uart.c - companion-module UART link on the HC32F460 DDL (M4_USART1).
 *
 * Stock UART0 @0x4001D000 == M4_USART1. 8N1 at the requested baud. TX is blocking;
 * RX is drained by uart_poll() from the main loop (calls the registered byte
 * callback, i.e. wlan_rx_isr_byte), which avoids INTC wiring and keeps the same
 * data flow as the stock RX ISR. */
#include "uart.h"
#include <hc32_ddl.h>
#include <hc32f460_usart.h>
#include <hc32f460_pwc.h>
#include <hc32f460_gpio.h>
#include <hc32f460_interrupts.h>
#include <hc32f460_utility.h>

#define UART_DEV        M4_USART1
/* Stock UART_Init (0x12344): PA6 = USART1 TX (func 0x20), PA5 = USART1 RX
 * (func 0x21). These were swapped before (PA5=TX/PA6=RX) -> the module never
 * received our TX -> no WLAN link. */
#define UART_TX_PORT    PortA
#define UART_TX_PIN     Pin06
#define UART_RX_PORT    PortA
#define UART_RX_PIN     Pin05

static uart_rx_cb_t s_rx_cb;

volatile uint32_t g_uart_rx_count;     /* DIAG: total bytes received */
volatile uint32_t g_uart_rx_err;       /* DIAG: error flags cleared  */
volatile uint8_t  g_uart_rx_last;      /* DIAG: last byte received   */
static void uart_irq_noop(void) { }    /* enIrqRegistration needs non-NULL cb */

void uart_init(uint32_t baud)
{
    PWC_Fcg1PeriphClockCmd(PWC_FCG1_PERIPH_USART1, Enable);

    /* Stock UART_Init (0x12344) first configures PA4 (port0/0x10) as a GPIO output
     * driven low - a UART-adjacent control line (live A/B: stock POERA bit4=1,
     * PODRA bit4=0). Then PA5->TX func, PA6->RX func. */
    stc_port_init_t pa4 = { .enPinMode = Pin_Mode_Out, .enPinDrv = Pin_Drv_H, .enPinOType = Pin_OType_Cmos };
    PORT_Init(PortA, Pin04, &pa4);
    PORT_ResetBits(PortA, Pin04);                  /* drive low */

    PORT_SetFunc(UART_TX_PORT, UART_TX_PIN, Func_Usart1_Tx, Disable);
    PORT_SetFunc(UART_RX_PORT, UART_RX_PIN, Func_Usart1_Rx, Disable);

    const stc_usart_uart_init_t cfg = {
        .enClkMode    = UsartIntClkCkNoOutput,
        .enClkDiv     = UsartClkDiv_64,    /* stock PR=3 (PCLK/64) */
        .enDataLength = UsartDataBits8,
        .enDirection  = UsartDataLsbFirst,
        .enStopBit    = UsartOneStopBit,
        .enParity     = UsartParityNone,
        .enSampleMode = UsartSampleBit8,
        .enDetectMode = UsartStartBitFallEdge,
        .enHwFlow     = UsartRtsEnable,
    };
    USART_UART_Init(UART_DEV, &cfg);
    USART_SetBaudrate(UART_DEV, baud);
    USART_FuncCmd(UART_DEV, UsartTx, Enable);
    USART_FuncCmd(UART_DEV, UsartRx, Enable);

    /* INTERRUPT-DRIVEN RX (the fix for the WLAN data loss). Polling drained the RX
     * register from the main loop, but a loop pass takes milliseconds (display/I2C)
     * while a byte arrives every ~8.7us; the 1-deep RX register overran and ~30% of
     * frames were corrupted (frames_bad), losing the larger weather/time/status
     * frames while tiny heartbeats survived. Now the RI interrupt takes each byte
     * within its window. Strong IRQ000_Handler (below) overrides both weak handlers
     * (startup alias + DDL) so it is always the one that runs. Only the RI interrupt
     * is enabled (NOT the EI error interrupt - that earlier stormed); errors are
     * cleared inside the handler and by uart_poll (no RDR read there -> no race). */
    USART_FuncCmd(UART_DEV, UsartRxInt, Enable);
    {
        stc_irq_regi_conf_t irq;
        irq.enIntSrc    = INT_USART1_RI;
        irq.enIRQn      = Int000_IRQn;
        irq.pfnCallback = uart_irq_noop;      /* unused: strong IRQ000_Handler runs */
        enIrqRegistration(&irq);
    }
    NVIC_ClearPendingIRQ(Int000_IRQn);
    NVIC_SetPriority(Int000_IRQn, DDL_IRQ_PRIORITY_01);   /* high: never miss a byte */
    NVIC_EnableIRQ(Int000_IRQn);
}

/* RI receive interrupt: read every byte the USART holds and hand it to the ringbuf.
 * Strong definition overrides the weak startup alias + the weak DDL dispatcher. */
void IRQ000_Handler(void)
{
    while (USART_GetStatus(UART_DEV, UsartRxNoEmpty) == Set) {
        uint8_t b = (uint8_t)USART_RecData(UART_DEV);
        g_uart_rx_count++;
        g_uart_rx_last = b;
        if (s_rx_cb) s_rx_cb(b);
    }
    if (USART_GetStatus(UART_DEV, UsartOverrunErr) == Set) USART_ClearStatus(UART_DEV, UsartOverrunErr);
    if (USART_GetStatus(UART_DEV, UsartFrameErr)   == Set) USART_ClearStatus(UART_DEV, UsartFrameErr);
    if (USART_GetStatus(UART_DEV, UsartParityErr)  == Set) USART_ClearStatus(UART_DEV, UsartParityErr);
}

int uart_tx_ready(void)
{
    return USART_GetStatus(UART_DEV, UsartTxEmpty) == Set;
}

void uart_putc(char c)
{
    while (USART_GetStatus(UART_DEV, UsartTxEmpty) != Set) { }
    USART_SendData(UART_DEV, (uint16_t)(uint8_t)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

void uart_send_buf(const void *buf, uint16_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    for (uint16_t i = 0; i < len; i++) {
        while (USART_GetStatus(UART_DEV, UsartTxEmpty) != Set) { }
        USART_SendData(UART_DEV, (uint16_t)p[i]);
    }
    while (USART_GetStatus(UART_DEV, UsartTxComplete) != Set) { }
}

uint8_t uart_read_byte(void)
{
    return (uint8_t)USART_RecData(UART_DEV);
}

void uart_set_rx_callback(uart_rx_cb_t cb) { s_rx_cb = cb; }

/* No NVIC wiring: RX is polled from the main loop. Kept for API compatibility. */
void uart_enable_rx_irq(void) { }

/* Drain any received bytes to the registered callback (call each main loop).
 *
 * CRITICAL: on the HC32F460 USART a parity/frame/over-run error sets a STICKY
 * status flag and BLOCKS further reception (RDRF stops updating) until that flag
 * is cleared. The stock RX path (RIE interrupt) clears them in its ISR; a poller
 * MUST do the same or RX dies permanently after the first line glitch/overrun ->
 * exactly the user's "nur eine Richtung" (TX works, nothing ever received back).
 * One stray error at link-up (idle-line noise, the module's first byte while we
 * were mid-init) is enough to wedge RX forever without this. */
/* Backstop only: clear sticky line errors so RX can never wedge. The RI interrupt
 * (IRQ000_Handler) owns the RX data register; uart_poll must NOT read it (that would
 * race the ISR and inject stale bytes). Safe to call from the main loop. */
void uart_poll(void)
{
    if (USART_GetStatus(UART_DEV, UsartParityErr)  == Set) { USART_ClearStatus(UART_DEV, UsartParityErr);  g_uart_rx_err++; }
    if (USART_GetStatus(UART_DEV, UsartFrameErr)   == Set) { USART_ClearStatus(UART_DEV, UsartFrameErr);   g_uart_rx_err++; }
    if (USART_GetStatus(UART_DEV, UsartOverrunErr) == Set) { USART_ClearStatus(UART_DEV, UsartOverrunErr); g_uart_rx_err++; }
}

void uart_rx_isr(void) { }   /* unused now (real RX is IRQ000_Handler) */

/* sub_124B0 (0x124B0): hardware power/reset control of the companion WLAN module
 * via PA4 (port0 pin4). off=1 -> release the UART (RX/TX off), settle, drive PA4
 * HIGH = module powered down / held in reset. off=0 -> drive PA4 LOW = module
 * powered, settle, re-init the UART (stock calls UART_Init @0x12344). This is the
 * HARDWARE wlan reset reached from the menu double-click confirm on page 11 (the
 * soft cmd 0x0D reset is separate). Stock: off path bl 0x12420(deinit pins)+
 * delay(5)+PA4 set; on path PA4 clear+delay(20)+UART_Init. */
void wlan_module_reset(uint8_t off)
{
    if (off & 1u) {
        USART_FuncCmd(UART_DEV, UsartTx, Disable);
        USART_FuncCmd(UART_DEV, UsartRx, Disable);
        Ddl_Delay1ms(5);
        PORT_SetBits(PortA, Pin04);          /* PA4 high = module off/reset */
    } else {
        PORT_ResetBits(PortA, Pin04);        /* PA4 low = module powered */
        Ddl_Delay1ms(20);
        uart_init(115200);                   /* re-init USART1 (stock 0x12344) */
    }
}
