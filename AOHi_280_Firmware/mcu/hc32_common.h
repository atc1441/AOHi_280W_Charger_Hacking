/* hc32_common.h - base types/macros shared by the MCU support layer.
 * Clean-room replacement for the vendor common header (only what the SDK uses). */
#ifndef HC32_COMMON_H
#define HC32_COMMON_H

#include <stdint.h>
#include <string.h>
#include "hc32f460.h"

typedef float float32_t;
typedef double float64_t;
typedef void (*func_ptr_t)(void);

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef enum en_functional_state
{
    Disable = 0u,
    Enable  = 1u,
} en_functional_state_t;

typedef enum en_flag_status
{
    Reset = 0u,
    Set   = 1u,
} en_flag_status_t;
typedef en_flag_status_t en_int_status_t;

typedef enum en_result
{
    Ok                       = 0u,
    Error                    = 1u,
    ErrorAddressAlignment    = 2u,
    ErrorAccessRights        = 3u,
    ErrorInvalidParameter    = 4u,
    ErrorOperationInProgress = 5u,
    ErrorInvalidMode         = 6u,
    ErrorUninitialized       = 7u,
    ErrorBufferFull          = 8u,
    ErrorTimeout             = 9u,
    ErrorNotReady            = 10u,
    OperationInProgress      = 11u,
} en_result_t;

#define MEM_ZERO_STRUCT(x)   do { memset((void *)&(x), 0, sizeof(x)); } while (0)
#define DEC2BCD(x)           ((((x) / 10u) << 4u) + ((x) % 10u))
#define BCD2DEC(x)           ((((x) >> 4u) * 10u) + ((x) & 0x0Fu))

#ifndef MIN
#define MIN(x, y)            ((x) < (y) ? (x) : (y))
#endif
#ifndef MAX
#define MAX(x, y)            ((x) > (y) ? (x) : (y))
#endif

#endif /* HC32_COMMON_H */
