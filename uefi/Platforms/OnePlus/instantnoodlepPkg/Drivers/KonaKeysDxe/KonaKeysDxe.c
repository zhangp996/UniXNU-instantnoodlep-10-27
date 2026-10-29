/** @file
  Minimal volume-key console input for instantnoodlep.

  Polls PM8150 SPMI GPIO 6/7 every 10 ms and publishes its own EFI_SIMPLE_TEXT_INPUT_PROTOCOL,
  so ConSplitter folds the volume keys into ConIn and the UniXNU menu can be driven without a
  serial port. No keypad library, no keypad Dxe: one protocol, two keys.
**/
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include <SpmiLib.h>

STATIC EFI_EVENT                mTimer;
STATIC EFI_SIMPLE_TEXT_INPUT_PROTOCOL mInput;
STATIC EFI_INPUT_KEY            mQueue[8];
STATIC UINTN                    mHead;
STATIC UINTN                    mTail;
STATIC BOOLEAN                  mUpDown[2];

STATIC
BOOLEAN
Pressed (
  IN UINT16  Address
  )
{
  UINT32  Value = 0xFFFFFFFF;

  if (EFI_ERROR (SpmiRead (0, Address, 0x10, 1, &Value))) {
    return FALSE;
  }
  return (Value & 1) == 0;
}

STATIC
VOID
EFIAPI
Poll (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  BOOLEAN  Up   = Pressed (0xC600);
  BOOLEAN  Down = Pressed (0xC700);

  if (Up && !mUpDown[0]) {
    mQueue[mTail].ScanCode    = SCAN_UP;
    mQueue[mTail].UnicodeChar = CHAR_NULL;
    mTail = (mTail + 1) % ARRAY_SIZE (mQueue);
  }
  if (Down && !mUpDown[1]) {
    mQueue[mTail].ScanCode    = SCAN_DOWN;
    mQueue[mTail].UnicodeChar = CHAR_NULL;
    mTail = (mTail + 1) % ARRAY_SIZE (mQueue);
  }
  mUpDown[0] = Up;
  mUpDown[1] = Down;
  if (mHead != mTail) {
    gBS->SignalEvent (mInput.WaitForKey);
  }
}

STATIC
EFI_STATUS
EFIAPI
InputReset (
  IN EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
  IN BOOLEAN                         ExtendedVerification
  )
{
  mHead = 0;
  mTail = 0;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ReadKeyStroke (
  IN  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
  OUT EFI_INPUT_KEY                  *Key
  )
{
  if (mHead == mTail) {
    return EFI_NOT_READY;
  }
  *Key  = mQueue[mHead];
  mHead = (mHead + 1) % ARRAY_SIZE (mQueue);
  if (mHead == mTail) {
    gBS->SignalEvent (mInput.WaitForKey);
  }
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
KonaKeysEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  UINTN       Waited = 0;

  SpmiInitialize ();

  mInput.Reset         = InputReset;
  mInput.ReadKeyStroke = ReadKeyStroke;
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, (EFI_EVENT_NOTIFY)0, NULL, &mInput.WaitForKey);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Poll, NULL, &mTimer);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = gBS->SetTimer (mTimer, TimerPeriodic, 100000);  /* 10 ms */
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ImageHandle,
                  &gEfiSimpleTextInProtocolGuid, &mInput,
                  NULL);
  (VOID)Waited;
  return Status;
}
