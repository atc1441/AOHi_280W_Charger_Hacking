/* hc32f460_usart.h - USART driver API (clean-room). */
#ifndef HC32F460_USART_H
#define HC32F460_USART_H

#include "hc32_common.h"

typedef enum en_usart_status
{
    UsartParityErr  = (1u << 0),
    UsartFrameErr   = (1u << 1),
    UsartOverrunErr = (1u << 3),
    UsartRxNoEmpty  = (1u << 5),
    UsartTxComplete = (1u << 6),
    UsartTxEmpty    = (1u << 7),
    UsartRxTimeOut  = (1u << 8),
    UsartRxMpb      = (1u << 16),
} en_usart_status_t;

typedef enum en_usart_func
{
    UsartRx = 0u, UsartRxInt = 1u, UsartTx = 2u, UsartTxEmptyInt = 3u,
    UsartTimeOut = 4u, UsartTimeOutInt = 5u, UsartSilentMode = 6u,
    UsartTxCmpltInt = 7u, UsartTxAndTxEmptyInt = 8u, UsartParityCheck = 9u,
    UsartNoiseFilter = 10u, UsartFracBaudrate = 11u, UsartMulProcessor = 12u,
    UsartSmartCard = 13u, UsartCts = 14u,
} en_usart_func_t;

typedef enum { UsartIntClkCkNoOutput = 0u, UsartIntClkCkOutput = 1u,
               UsartExtClk = 2u } en_usart_clk_mode_t;
typedef enum { UsartClkDiv_1 = 0u, UsartClkDiv_4 = 1u, UsartClkDiv_16 = 2u,
               UsartClkDiv_64 = 3u } en_usart_clk_div_t;
typedef enum { UsartDataBits8 = 0u, UsartDataBits9 = 1u } en_usart_data_len_t;
typedef enum { UsartDataLsbFirst = 0u, UsartDataMsbFirst = 1u } en_usart_data_dir_t;
typedef enum { UsartOneStopBit = 0u, UsartTwoStopBit = 1u } en_usart_stop_bit_t;
typedef enum { UsartParityNone = 0u, UsartParityEven = 1u, UsartParityOdd = 2u } en_usart_parity_t;
typedef enum { UsartSampleBit16 = 0u, UsartSampleBit8 = 1u } en_usart_sample_mode_t;
typedef enum { UsartStartBitLowLvl = 0u, UsartStartBitFallEdge = 1u } en_usart_sb_detect_mode_t;
typedef enum { UsartRtsEnable = 0u, UsartCtsEnable = 1u } en_usart_hw_flow_ctrl_t;

typedef struct stc_usart_uart_init
{
    en_usart_clk_mode_t       enClkMode;
    en_usart_clk_div_t        enClkDiv;
    en_usart_data_len_t       enDataLength;
    en_usart_data_dir_t       enDirection;
    en_usart_stop_bit_t       enStopBit;
    en_usart_parity_t         enParity;
    en_usart_sample_mode_t    enSampleMode;
    en_usart_sb_detect_mode_t enDetectMode;
    en_usart_hw_flow_ctrl_t   enHwFlow;
} stc_usart_uart_init_t;

en_result_t      USART_UART_Init(M4_USART_TypeDef *USARTx, const stc_usart_uart_init_t *pstcInitCfg);
en_result_t      USART_SetBaudrate(M4_USART_TypeDef *USARTx, uint32_t u32Baudrate);
en_result_t      USART_FuncCmd(M4_USART_TypeDef *USARTx, en_usart_func_t enFunc, en_functional_state_t enCmd);
en_flag_status_t USART_GetStatus(M4_USART_TypeDef *USARTx, en_usart_status_t enStatus);
en_result_t      USART_ClearStatus(M4_USART_TypeDef *USARTx, en_usart_status_t enStatus);
en_result_t      USART_SendData(M4_USART_TypeDef *USARTx, uint16_t u16Data);
uint16_t         USART_RecData(M4_USART_TypeDef *USARTx);

#endif /* HC32F460_USART_H */
