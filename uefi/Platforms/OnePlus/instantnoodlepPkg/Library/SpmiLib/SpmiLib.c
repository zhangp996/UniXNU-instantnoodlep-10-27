/** @file
  Minimal SPMI PMIC arbiter reader for instantnoodlep.

  The arbiter is the one at /soc/qcom,spmi@c440000: core 0xC440000, channel write window
  0xC600000, observer (read) window 0xE600000 and the configuration/mapping window
  0xC40A000. A read is a command written to the observer channel, a polled status and a
  data word; the channel index comes from the PPID mapping table.
**/
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "SpmiLib.h"

#define SPMI_CORE_BASE   0x0C440000ULL
#define SPMI_OBSVR_BASE  0x0E600000ULL
#define SPMI_CNFG_BASE   0x0C40A000ULL

#define CH_CMD           0x0000
#define CH_STATUS        0x0008
#define CH_DATA          0x0010

#define APID_MAP_OFFSET  0x0B00
#define APID_VALID       0x8000
#define STATUS_DONE      0x1
#define STATUS_FAIL      0x2
#define EE               0

EFI_STATUS
SpmiInitialize (VOID)
{
  return EFI_SUCCESS;
}

STATIC
UINT16
PpidToApid (
  IN UINT16  Ppid
  )
{
  UINT32  Entry = MmioRead32 ((UINTN)(SPMI_CNFG_BASE + APID_MAP_OFFSET + 4 * Ppid));

  if ((Entry & APID_VALID) == 0) {
    return 0xFFFF;
  }
  return (UINT16)(Entry & ~APID_VALID);
}

EFI_STATUS
SpmiRead (
  IN  UINT8   Sid,
  IN  UINT16  Address,
  IN  UINT8   Offset,
  IN  UINT8   Count,
  OUT UINT32 *Value
  )
{
  UINT16  Ppid = (UINT16)(((UINT16)Sid << 8) | ((Address >> 8) & 0xFF));
  UINT16  Apid = PpidToApid (Ppid);
  UINTN   Channel;
  UINT32  Command;
  UINT32  Status = 0;
  UINTN   Spins;

  if (Apid == 0xFFFF) {
    return EFI_NOT_FOUND;
  }
  if ((Count == 0) || (Count > 8)) {
    return EFI_INVALID_PARAMETER;
  }

  Channel = (UINTN)(0x1000ULL * EE + 0x8000ULL * Apid);
  Command = ((UINT32)(Count - 1) & 0x7) | ((UINT32)Offset << 4);
  MmioWrite32 ((UINTN)(SPMI_OBSVR_BASE + Channel + CH_CMD), Command);
  for (Spins = 0; Spins < 1000; Spins++) {
    Status = MmioRead32 ((UINTN)(SPMI_OBSVR_BASE + Channel + CH_STATUS));
    if ((Status & STATUS_DONE) != 0) {
      break;
    }
    gBS->Stall (10);
  }
  if ((Status & STATUS_DONE) == 0) {
    return EFI_TIMEOUT;
  }
  if ((Status & STATUS_FAIL) != 0) {
    return EFI_DEVICE_ERROR;
  }
  *Value = MmioRead32 ((UINTN)(SPMI_OBSVR_BASE + Channel + CH_DATA));
  return EFI_SUCCESS;
}
