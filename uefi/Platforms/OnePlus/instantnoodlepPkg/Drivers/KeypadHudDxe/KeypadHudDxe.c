/** @file
  SPMI parameter matrix on the panel.

  The volume keys read back as failures, so this walks the two parameters that decide the
  arbiter access - the APID mapping window and the execution environment - and paints one row
  per combination. The first cell of a row is green when the transaction completed without a
  failure bit, red otherwise; the eight cells after it are the data byte.
**/
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#define CELL          44
#define SPMI_OBSVR    0x0E600000ULL
#define SPMI_CNFG     0x0C40A000ULL
#define CH_CMD        0x0000
#define CH_STATUS     0x0008
#define CH_DATA       0x0010
#define APID_VALID    0x8000
#define STATUS_DONE   0x1
#define STATUS_FAIL   0x2

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;
STATIC EFI_EVENT                      mTimer;

typedef struct { UINT32 MapOffset; UINT32 Ee; } SPMI_CASE;
STATIC SPMI_CASE  mCases[] = {
  { 0x0B00, 0 }, { 0x0B00, 1 }, { 0x0900, 0 }, { 0x0000, 0 },
};

STATIC VOID Cell (IN UINT32 Col, IN UINT32 Row, IN UINT8 R, IN UINT8 G, IN UINT8 B)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;
  UINT32                         X = 24 + Col * (CELL + 6);
  UINT32                         Y = 24 + Row * (CELL + 6);

  if (mGop == NULL) return;
  Pixel.Red = R; Pixel.Green = G; Pixel.Blue = B; Pixel.Reserved = 0;
  mGop->Blt (mGop, &Pixel, EfiBltVideoFill, 0, 0, X, Y, CELL, CELL, 0);
}

STATIC EFI_STATUS Transaction (IN UINT32 MapOffset, IN UINT32 Ee, OUT UINT32 *Data)
{
  UINT16  Ppid = 0x00C6;
  UINT32  Entry;
  UINT16  Apid;
  UINTN   Channel;
  UINT32  Status = 0;
  UINTN   Spins;

  Entry = MmioRead32 ((UINTN)(SPMI_CNFG + MapOffset + 4 * Ppid));
  if ((Entry & APID_VALID) == 0) { *Data = Entry; return EFI_NOT_FOUND; }
  Apid    = (UINT16)(Entry & ~APID_VALID);
  Channel = (UINTN)(0x1000ULL * Ee + 0x8000ULL * Apid);
  MmioWrite32 ((UINTN)(SPMI_OBSVR + Channel + CH_CMD), 0x00000100U);
  for (Spins = 0; Spins < 500; Spins++) {
    Status = MmioRead32 ((UINTN)(SPMI_OBSVR + Channel + CH_STATUS));
    if ((Status & STATUS_DONE) != 0) break;
    gBS->Stall (100);
  }
  *Data = MmioRead32 ((UINTN)(SPMI_OBSVR + Channel + CH_DATA));
  if ((Status & STATUS_DONE) == 0) return EFI_TIMEOUT;
  if ((Status & STATUS_FAIL) != 0) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}

STATIC VOID EFIAPI Repaint (IN EFI_EVENT Event, IN VOID *Context)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Panel;
  UINTN                          Index;

  Panel.Red = 0; Panel.Green = 0; Panel.Blue = 0; Panel.Reserved = 0;
  mGop->Blt (mGop, &Panel, EfiBltVideoFill, 0, 0, 16, 16, 500, 4 * (CELL + 6) + 16, 0);
  for (Index = 0; Index < ARRAY_SIZE (mCases); Index++) {
    UINT32      Data = 0;
    EFI_STATUS  Status;
    UINTN       Bit;

    Status = Transaction (mCases[Index].MapOffset, mCases[Index].Ee, &Data);
    Cell (0, (UINT32)Index,
          (UINT8)(EFI_ERROR (Status) ? 255 : 0),
          (UINT8)(EFI_ERROR (Status) ? 0 : 220), 0);
    for (Bit = 0; Bit < 8; Bit++) {
      UINT8  On = (UINT8)((Data >> (7 - Bit)) & 1);
      Cell ((UINT32)(1 + Bit), (UINT32)Index,
            (UINT8)(On ? 255 : 25), (UINT8)(On ? 255 : 25), (UINT8)(On ? 255 : 70));
    }
  }
}

EFI_STATUS EFIAPI KeypadHudEntry (IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status)) return EFI_SUCCESS;
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Repaint, NULL, &mTimer);
  if (EFI_ERROR (Status)) return EFI_SUCCESS;
  gBS->SetTimer (mTimer, TimerPeriodic, 500000);
  return EFI_SUCCESS;
}