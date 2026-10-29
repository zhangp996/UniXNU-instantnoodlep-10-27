/** @file
  Head-up indicator for the SPMI volume keys.

  A 500 ms timer reads PM8150 GPIO 6/7 status and repaints a small strip in the top-right
  corner, so the values stay visible over the UniXNU menu without blocking BDS:

    row 0: status square (green = SPMI read ok, red = failed), then bits 7..0 of the byte
    row 1: the same for GPIO 7 (volume down)

  A pressed volume key pulls bit 0 low, so its square turns white.
**/
#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>

#include <SpmiLib.h>

#define CELL   16

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *mGop;

/* GUID reversed from the vendor SPMIDxe.efi (.data at 0x8008). */
STATIC EFI_GUID  mSpmiDxeGuid = { 0xB898D8DC, 0x080A, 0x40F7,
  { 0x99, 0xE3, 0x31, 0x62, 0x7B, 0x80, 0x6A, 0x5A } };
STATIC EFI_EVENT                      mTimer;

STATIC
VOID
Cell (
  IN UINT32  Col,
  IN UINT32  Row,
  IN UINT8   R,
  IN UINT8   G,
  IN UINT8   B
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Pixel;
  UINT32                         X = 8 + Col * (CELL + 4);
  UINT32                         Y = 8 + Row * (CELL + 4);

  if (mGop == NULL) {
    return;
  }
  Pixel.Red = R; Pixel.Green = G; Pixel.Blue = B; Pixel.Reserved = 0;
  mGop->Blt (mGop, &Pixel, EfiBltVideoFill, 0, 0, X, Y, CELL, CELL, 0);
}

STATIC
VOID
EFIAPI
Repaint (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  UINT32      Up = 0;
  UINT32      Down = 0;
  EFI_STATUS  UpStatus;
  EFI_STATUS  DownStatus;
  UINTN       Bit;

  UpStatus   = SpmiRead (0, 0xC600, 0x10, 1, &Up);
  DownStatus = SpmiRead (0, 0xC700, 0x10, 1, &Down);

  {
    VOID  *Spmi = NULL;
    Cell (0, 2, (UINT8)(EFI_ERROR (gBS->LocateProtocol (&mSpmiDxeGuid, NULL, &Spmi)) ? 255 : 0),
          (UINT8)(EFI_ERROR (gBS->LocateProtocol (&mSpmiDxeGuid, NULL, &Spmi)) ? 0 : 200), 0);
  }
  Cell (0, 0, (UINT8)(EFI_ERROR (UpStatus) ? 255 : 0), (UINT8)(EFI_ERROR (UpStatus) ? 0 : 200), 0);
  Cell (0, 1, (UINT8)(EFI_ERROR (DownStatus) ? 255 : 0), (UINT8)(EFI_ERROR (DownStatus) ? 0 : 200), 0);
  for (Bit = 0; Bit < 8; Bit++) {
    UINT8  On;

    On = (UINT8)((Up >> (7 - Bit)) & 1);
    Cell ((UINT32)(1 + Bit), 0, (UINT8)(On ? 255 : 20), (UINT8)(On ? 255 : 20), (UINT8)(On ? 255 : 60));
    On = (UINT8)((Down >> (7 - Bit)) & 1);
    Cell ((UINT32)(1 + Bit), 1, (UINT8)(On ? 255 : 20), (UINT8)(On ? 255 : 20), (UINT8)(On ? 255 : 60));
  }
}

EFI_STATUS
EFIAPI
KeypadHudEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&mGop);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }
  SpmiInitialize ();
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Repaint, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }
  gBS->SetTimer (mTimer, TimerPeriodic, 500000);  /* 500 ms */
  return EFI_SUCCESS;
}
