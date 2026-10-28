/** @file
  Read the UFS host and PHY registers and stop, so the panel shows whether the
  controller came out of reset before any driver touches it.

  The register block is already described by the firmware memory map ("UFS",
  0x01D84000, 0x00100000, MmIO), so a plain uncached read is enough. No clock or reset
  is programmed here on purpose: the point is to see what the bootloader left behind.
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiLib.h>

#define UFS_HOST_BASE   0x01D84000ULL
#define UFS_PHY_BASE    0x01D87000ULL

#define UFS_VER         0x0000
#define UFS_CAP         0x0008
#define UFS_HCS         0x0030
#define UFS_HCE         0x0034
#define UFS_IS          0x0020
#define UFS_IE          0x0024

EFI_STATUS
EFIAPI
UfsProbeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  UINT32  Ver;
  UINT32  Cap;
  UINT32  Hcs;
  UINT32  Hce;
  UINT32  Is;
  UINT32  Phy0;
  UINT32  Phy1;

  Ver  = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_VER);
  Cap  = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_CAP);
  Hcs  = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_HCS);
  Hce  = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_HCE);
  Is   = MmioRead32 ((UINTN)UFS_HOST_BASE + UFS_IS);
  Phy0 = MmioRead32 ((UINTN)UFS_PHY_BASE);
  Phy1 = MmioRead32 ((UINTN)UFS_PHY_BASE + 4);

  Print (L"\nUFSPROBE: host 0x%lx phy 0x%lx\n", (UINT64)UFS_HOST_BASE, (UINT64)UFS_PHY_BASE);
  Print (L"UFSPROBE: VER 0x%08x CAP 0x%08x\n", Ver, Cap);
  Print (L"UFSPROBE: HCS 0x%08x HCE 0x%08x IS 0x%08x\n", Hcs, Hce, Is);
  Print (L"UFSPROBE: PHY 0x%08x 0x%08x\n", Phy0, Phy1);
  Print (L"UFSPROBE: device-present %a, ready %a\n",
         ((Hcs & 1) != 0) ? "yes" : "no",
         ((Hcs & 2) != 0) ? "yes" : "no");

  CpuDeadLoop ();
  return EFI_SUCCESS;
}
