/* mcu_usart.c - USART driver (UART mode) (clean-room).
 *
 * Covers exactly what the WLAN companion link (M4_USART1, 8N1) needs: UART-mode
 * init, fractional-baudrate programming, per-function enable, and the status /
 * data accessors used by the TX path and the RX interrupt handler. */
#include "hc32f460_usart.h"
#include "system_hc32f460.h"

/* PCLK1 = SystemCoreClock >> CMU_SCFGR.PCLK1S ; UART clock = PCLK1 >> (2*PSC) */
static uint32_t usart_get_clk(const M4_USART_TypeDef *USARTx)
{
    uint32_t pclk1 = SystemCoreClock >> M4_SYSREG->CMU_SCFGR_f.PCLK1S;
    return pclk1 >> (2u * USARTx->PR_f.PSC);
}

en_result_t USART_UART_Init(M4_USART_TypeDef *USARTx, const stc_usart_uart_init_t *cfg)
{
    if (USARTx == NULL || cfg == NULL) return ErrorInvalidParameter;

    /* reset to documented defaults */
    USARTx->CR1 = 0x801B0000ul;
    USARTx->CR2 = 0x00000000ul;
    USARTx->CR3 = 0x00000000ul;
    USARTx->BRR = 0x0000FFFFul;
    USARTx->PR  = 0x00000000ul;

    USARTx->CR3_f.SCEN = 0u;
    USARTx->CR1_f.MS   = 0u;

    USARTx->PR_f.PSC    = cfg->enClkDiv;
    USARTx->CR1_f.M     = cfg->enDataLength;
    USARTx->CR1_f.ML    = cfg->enDirection;
    USARTx->CR2_f.STOP  = cfg->enStopBit;
    USARTx->CR2_f.CLKC  = cfg->enClkMode;

    switch (cfg->enParity)
    {
        case UsartParityNone: USARTx->CR1_f.PCE = 0u; break;
        case UsartParityEven: USARTx->CR1_f.PS = 0u; USARTx->CR1_f.PCE = 1u; break;
        case UsartParityOdd:  USARTx->CR1_f.PS = 1u; USARTx->CR1_f.PCE = 1u; break;
        default: break;
    }

    USARTx->CR3_f.CTSE  = cfg->enHwFlow;
    USARTx->CR1_f.SBS   = cfg->enDetectMode;
    USARTx->CR1_f.OVER8 = cfg->enSampleMode;
    return Ok;
}

en_result_t USART_SetBaudrate(M4_USART_TypeDef *USARTx, uint32_t u32Baudrate)
{
    uint32_t C = usart_get_clk(USARTx);
    if (C == 0u || u32Baudrate == 0u) return ErrorInvalidParameter;

    uint32_t OVER8 = USARTx->CR1_f.OVER8;
    /* DIV = C / (B * 8 * (2-OVER8)) - 1 */
    float32_t DIV = ((float)C / ((float)u32Baudrate * 8.0f * (2.0f - (float)OVER8))) - 1.0f;
    uint32_t DIV_Integer = (uint32_t)DIV;
    uint32_t DIV_Fraction = 0xFFFFFFFFul;

    if ((DIV < 0.0f) || (DIV_Integer > 0xFFu)) return ErrorInvalidParameter;

    en_result_t enRet = Ok;
    if ((DIV - (float32_t)DIV_Integer) > 0.00001f)
    {
        uint64_t t = (uint64_t)(2ul - OVER8) * ((uint64_t)DIV_Integer + 1u) * (uint64_t)u32Baudrate;
        DIV_Fraction = (uint32_t)(2048ul * t / C - 128ul);
        if (DIV_Fraction > 0x7Ful) enRet = ErrorInvalidParameter;
    }

    if (Ok == enRet)
    {
        USARTx->CR1_f.FBME = (DIV_Fraction > 0x7Ful) ? 0u : 1u;
        USARTx->BRR_f.DIV_FRACTION = DIV_Fraction;
        USARTx->BRR_f.DIV_INTEGER  = DIV_Integer;
    }
    return enRet;
}

en_result_t USART_FuncCmd(M4_USART_TypeDef *USARTx, en_usart_func_t enFunc, en_functional_state_t enCmd)
{
    switch (enFunc)
    {
        case UsartRx:            USARTx->CR1_f.RE    = enCmd; break;
        case UsartRxInt:         USARTx->CR1_f.RIE   = enCmd; break;
        case UsartTx:            USARTx->CR1_f.TE    = enCmd; break;
        case UsartTxEmptyInt:    USARTx->CR1_f.TXEIE = enCmd; break;
        case UsartTimeOut:       USARTx->CR1_f.RTOE  = enCmd; break;
        case UsartTimeOutInt:    USARTx->CR1_f.RTOIE = enCmd; break;
        case UsartSilentMode:    USARTx->CR1_f.SLME  = enCmd; break;
        case UsartParityCheck:   USARTx->CR1_f.PCE   = enCmd; break;
        case UsartNoiseFilter:   USARTx->CR1_f.NFE   = enCmd; break;
        case UsartTxCmpltInt:    USARTx->CR1_f.TCIE  = enCmd; break;
        case UsartFracBaudrate:  USARTx->CR1_f.FBME  = enCmd; break;
        case UsartMulProcessor:  USARTx->CR2_f.MPE   = enCmd; break;
        case UsartSmartCard:     USARTx->CR3_f.SCEN  = enCmd; break;
        case UsartCts:           USARTx->CR3_f.CTSE  = enCmd; break;
        case UsartTxAndTxEmptyInt:
            USARTx->CR1_f.TE = enCmd; USARTx->CR1_f.TXEIE = enCmd; break;
        default: return ErrorInvalidParameter;
    }
    return Ok;
}

en_flag_status_t USART_GetStatus(M4_USART_TypeDef *USARTx, en_usart_status_t enStatus)
{
    return (USARTx->SR & (uint32_t)enStatus) ? Set : Reset;
}

en_result_t USART_ClearStatus(M4_USART_TypeDef *USARTx, en_usart_status_t enStatus)
{
    switch (enStatus)
    {
        case UsartParityErr:  USARTx->CR1_f.CPE   = 1u; break;
        case UsartFrameErr:   USARTx->CR1_f.CFE   = 1u; break;
        case UsartOverrunErr: USARTx->CR1_f.CORE  = 1u; break;
        case UsartRxTimeOut:  USARTx->CR1_f.CRTOF = 1u; break;
        default: return ErrorInvalidParameter;
    }
    return Ok;
}

en_result_t USART_SendData(M4_USART_TypeDef *USARTx, uint16_t u16Data)
{
    USARTx->DR_f.TDR = u16Data;
    return Ok;
}

uint16_t USART_RecData(M4_USART_TypeDef *USARTx)
{
    return (uint16_t)USARTx->DR_f.RDR;
}
