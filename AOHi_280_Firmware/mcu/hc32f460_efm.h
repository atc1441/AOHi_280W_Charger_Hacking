/* hc32f460_efm.h - embedded flash (program/erase) API (clean-room). */
#ifndef HC32F460_EFM_H
#define HC32F460_EFM_H

#include "hc32_common.h"

#define EFM_MODE_READONLY        (0ul)
#define EFM_MODE_SINGLEPROGRAM   (1ul)
#define EFM_MODE_SINGLEPROGRAMRB (2ul)
#define EFM_MODE_SEQUENCEPROGRAM (3ul)
#define EFM_MODE_SECTORERASE     (4ul)
#define EFM_MODE_CHIPERASE       (5ul)

#define EFM_FLAG_WRPERR          (0x00000001ul)
#define EFM_FLAG_PEPRTERR        (0x00000002ul)
#define EFM_FLAG_PGSZERR         (0x00000004ul)
#define EFM_FLAG_PGMISMTCH       (0x00000008ul)
#define EFM_FLAG_EOP             (0x00000010ul)
#define EFM_FLAG_COLERR          (0x00000020ul)

void        EFM_Unlock(void);
void        EFM_Lock(void);
en_result_t EFM_SetErasePgmMode(uint32_t u32Mode);
en_result_t EFM_SectorErase(uint32_t u32Addr);
en_result_t EFM_SingleProgram(uint32_t u32Addr, uint32_t u32Data);

#endif /* HC32F460_EFM_H */
