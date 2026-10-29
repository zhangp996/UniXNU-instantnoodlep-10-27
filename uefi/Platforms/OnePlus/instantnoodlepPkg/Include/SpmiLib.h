#ifndef SPMI_LIB_H_
#define SPMI_LIB_H_

#include <Uefi.h>

EFI_STATUS
SpmiInitialize (VOID);

EFI_STATUS
SpmiRead (
  IN  UINT8   Sid,
  IN  UINT16  Address,
  IN  UINT8   Offset,
  IN  UINT8   Count,
  OUT UINT32 *Value
  );

#endif
