/* mcu_spi.c - SPI driver (clean-room).
 *
 * Master-mode init mapping the config struct onto CR1/CFG1/CFG2, plus the
 * byte-wide data/flag accessors the LCD (SPI3, send-only) and the external
 * flash (SPI1, full-duplex) use. */
#include "hc32f460_spi.h"

en_result_t SPI_Init(M4_SPI_TypeDef *SPIx, const stc_spi_init_t *cfg)
{
    if (SPIx == NULL || cfg == NULL) return ErrorInvalidParameter;

    /* master delay/SS timing (slave-irrelevant here; driven from the struct) */
    SPIx->CFG2_f.MSSIE  = cfg->stcDelayConfig.enSsSetupDelayOption;
    SPIx->CFG2_f.MSSDLE = cfg->stcDelayConfig.enSsHoldDelayOption;
    SPIx->CFG2_f.MIDIE  = cfg->stcDelayConfig.enSsIntervalTimeOption;
    SPIx->CFG1_f.MSSI   = cfg->stcDelayConfig.enSsSetupDelayTime;
    SPIx->CFG1_f.MSSDL  = cfg->stcDelayConfig.enSsHoldDelayTime;
    SPIx->CFG1_f.MIDI   = cfg->stcDelayConfig.enSsIntervalTime;

    /* SS polarity/channel (only meaningful in 4-line mode) */
    SPIx->CFG2_f.SSA   = cfg->stcSsConfig.enSsValidBit;
    SPIx->CFG1_f.SS0PV = cfg->stcSsConfig.enSs0Polarity;
    SPIx->CFG1_f.SS1PV = cfg->stcSsConfig.enSs1Polarity;
    SPIx->CFG1_f.SS2PV = cfg->stcSsConfig.enSs2Polarity;
    SPIx->CFG1_f.SS3PV = cfg->stcSsConfig.enSs3Polarity;

    /* config register 1 */
    SPIx->CFG1_f.SPRDTD = cfg->enReadBufferObject;
    SPIx->CFG1_f.FTHLV  = cfg->enFrameNumber;

    /* config register 2 */
    SPIx->CFG2_f.LSBF  = cfg->enFirstBitPosition;
    SPIx->CFG2_f.DSIZE = cfg->enDataLength;
    SPIx->CFG2_f.MBR   = cfg->enClkDiv;
    SPIx->CFG2_f.CPOL  = cfg->enSckPolarity;
    SPIx->CFG2_f.CPHA  = cfg->enSckPhase;

    /* control register */
    SPIx->CR1_f.SPIMDS = cfg->enWorkMode;
    SPIx->CR1_f.TXMDS  = cfg->enTransMode;
    SPIx->CR1_f.MSTR   = cfg->enMasterSlaveMode;
    SPIx->CR1_f.CSUSPE = cfg->enCommAutoSuspendEn;
    SPIx->CR1_f.MODFE  = cfg->enModeFaultErrorDetectEn;
    SPIx->CR1_f.PATE   = cfg->enParitySelfDetectEn;
    SPIx->CR1_f.PAE    = cfg->enParityEn;
    SPIx->CR1_f.PAOE   = cfg->enParity;
    return Ok;
}

en_result_t SPI_Cmd(M4_SPI_TypeDef *SPIx, en_functional_state_t enNewSta)
{
    SPIx->CR1_f.SPE = enNewSta;
    return Ok;
}

en_result_t SPI_SendData8(M4_SPI_TypeDef *SPIx, uint8_t u8Data)
{
    SPIx->DR = u8Data;
    return Ok;
}

uint8_t SPI_ReceiveData8(const M4_SPI_TypeDef *SPIx)
{
    return (uint8_t)SPIx->DR;
}

en_flag_status_t SPI_GetFlag(M4_SPI_TypeDef *SPIx, en_spi_flag_type_t enFlag)
{
    switch (enFlag)
    {
        case SpiFlagReceiveBufferFull: return SPIx->SR_f.RDFF    ? Set : Reset;
        case SpiFlagSendBufferEmpty:   return SPIx->SR_f.TDEF    ? Set : Reset;
        case SpiFlagUnderloadError:    return SPIx->SR_f.UDRERF  ? Set : Reset;
        case SpiFlagParityError:       return SPIx->SR_f.PERF    ? Set : Reset;
        case SpiFlagModeFaultError:    return SPIx->SR_f.MODFERF ? Set : Reset;
        case SpiFlagSpiIdle:           return SPIx->SR_f.IDLNF   ? Reset : Set;
        case SpiFlagOverloadError:     return SPIx->SR_f.OVRERF  ? Set : Reset;
        default: return Reset;
    }
}
