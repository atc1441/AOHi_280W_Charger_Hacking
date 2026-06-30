/* hc32f460_spi.h - SPI driver API (clean-room). */
#ifndef HC32F460_SPI_H
#define HC32F460_SPI_H

#include "hc32_common.h"

typedef enum { SpiParityEven = 0u, SpiParityOdd = 1u } en_spi_parity_t;
typedef enum { SpiModeSlave = 0u, SpiModeMaster = 1u } en_spi_master_slave_mode_t;
typedef enum { SpiTransFullDuplex = 0u, SpiTransOnlySend = 1u } en_spi_trans_mode_t;
typedef enum { SpiWorkMode4Line = 0u, SpiWorkMode3Line = 1u } en_spi_work_mode_t;
typedef enum { SpiSsIntervalSck1PlusPck2 = 0u } en_spi_ss_interval_time_t;
typedef enum { SpiSsSetupDelaySck1 = 0u } en_spi_ss_setup_delay_t;
typedef enum { SpiSsHoldDelaySck1 = 0u } en_spi_ss_hold_delay_t;
typedef enum { SpiSsLowValid = 0u, SpiSsHighValid = 1u } en_spi_ss_polarity_t;
typedef enum { SpiReadReceiverBuffer = 0u, SpiReadSendBuffer = 1u } en_spi_read_object_t;
typedef enum { SpiFrameNumber1 = 0u, SpiFrameNumber2 = 1u,
               SpiFrameNumber3 = 2u, SpiFrameNumber4 = 3u } en_spi_frame_number_t;
typedef enum { SpiSsSetupDelayTypicalSck1 = 0u, SpiSsSetupDelayCustomValue = 1u } en_spi_ss_setup_delay_option_t;
typedef enum { SpiSsHoldDelayTypicalSck1 = 0u, SpiSsHoldDelayCustomValue = 1u } en_spi_ss_hold_delay_option_t;
typedef enum { SpiSsIntervalTypicalSck1PlusPck2 = 0u, SpiSsIntervalCustomValue = 1u } en_spi_ss_interval_time_option_t;
typedef enum { SpiFirstBitPositionMSB = 0u, SpiFirstBitPositionLSB = 1u } en_spi_first_bit_position_t;
typedef enum { SpiDataLengthBit4 = 0u, SpiDataLengthBit8 = 4u, SpiDataLengthBit16 = 12u } en_spi_data_length_t;
typedef enum { SpiSsValidChannel0 = 0u } en_spi_ss_valid_channel_t;
typedef enum { SpiClkDiv2 = 0u, SpiClkDiv4 = 1u, SpiClkDiv8 = 2u, SpiClkDiv16 = 3u,
               SpiClkDiv32 = 4u, SpiClkDiv64 = 5u, SpiClkDiv128 = 6u, SpiClkDiv256 = 7u } en_spi_clk_div_t;
typedef enum { SpiSckIdleLevelLow = 0u, SpiSckIdleLevelHigh = 1u } en_spi_sck_polarity_t;
typedef enum { SpiSckOddSampleEvenChange = 0u, SpiSckOddChangeEvenSample = 1u } en_spi_sck_phase_t;

typedef enum en_spi_flag_type
{
    SpiFlagReceiveBufferFull = 0u,
    SpiFlagSendBufferEmpty   = 1u,
    SpiFlagUnderloadError    = 2u,
    SpiFlagParityError       = 3u,
    SpiFlagModeFaultError    = 4u,
    SpiFlagSpiIdle           = 5u,
    SpiFlagOverloadError     = 6u,
} en_spi_flag_type_t;

typedef struct stc_spi_delay_config
{
    en_spi_ss_setup_delay_option_t   enSsSetupDelayOption;
    en_spi_ss_setup_delay_t          enSsSetupDelayTime;
    en_spi_ss_hold_delay_option_t    enSsHoldDelayOption;
    en_spi_ss_hold_delay_t           enSsHoldDelayTime;
    en_spi_ss_interval_time_option_t enSsIntervalTimeOption;
    en_spi_ss_interval_time_t        enSsIntervalTime;
} stc_spi_delay_config_t;

typedef struct stc_spi_ss_config
{
    en_spi_ss_valid_channel_t enSsValidBit;
    en_spi_ss_polarity_t      enSs0Polarity;
    en_spi_ss_polarity_t      enSs1Polarity;
    en_spi_ss_polarity_t      enSs2Polarity;
    en_spi_ss_polarity_t      enSs3Polarity;
} stc_spi_ss_config_t;

typedef struct stc_spi_init_t
{
    stc_spi_delay_config_t      stcDelayConfig;
    stc_spi_ss_config_t         stcSsConfig;
    en_spi_read_object_t        enReadBufferObject;
    en_spi_sck_polarity_t       enSckPolarity;
    en_spi_sck_phase_t          enSckPhase;
    en_spi_clk_div_t            enClkDiv;
    en_spi_data_length_t        enDataLength;
    en_spi_first_bit_position_t enFirstBitPosition;
    en_spi_frame_number_t       enFrameNumber;
    en_spi_work_mode_t          enWorkMode;
    en_spi_trans_mode_t         enTransMode;
    en_spi_master_slave_mode_t  enMasterSlaveMode;
    en_functional_state_t       enCommAutoSuspendEn;
    en_functional_state_t       enModeFaultErrorDetectEn;
    en_functional_state_t       enParitySelfDetectEn;
    en_functional_state_t       enParityEn;
    en_spi_parity_t             enParity;
} stc_spi_init_t;

en_result_t      SPI_Init(M4_SPI_TypeDef *SPIx, const stc_spi_init_t *pstcSpiInitCfg);
en_result_t      SPI_Cmd(M4_SPI_TypeDef *SPIx, en_functional_state_t enNewSta);
en_result_t      SPI_SendData8(M4_SPI_TypeDef *SPIx, uint8_t u8Data);
uint8_t          SPI_ReceiveData8(const M4_SPI_TypeDef *SPIx);
en_flag_status_t SPI_GetFlag(M4_SPI_TypeDef *SPIx, en_spi_flag_type_t enFlag);

#endif /* HC32F460_SPI_H */
